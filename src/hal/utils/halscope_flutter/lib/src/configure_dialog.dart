import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart' as ws;

/// Dialog to configure capture parameters: thread, record length, pre-trigger.
class ConfigureDialog extends StatefulWidget {
  final ws.HalscopeWsClient client;
  final ws.ScopeStatus? currentStatus;

  const ConfigureDialog({
    super.key,
    required this.client,
    this.currentStatus,
  });

  @override
  State<ConfigureDialog> createState() => _ConfigureDialogState();
}

class _ConfigureDialogState extends State<ConfigureDialog> {
  late final TextEditingController _threadController;
  late final TextEditingController _recLenController;
  late final TextEditingController _preTrigController;
  late final TextEditingController _multController;
  String? _error;

  @override
  void initState() {
    super.initState();
    final s = widget.currentStatus;
    _threadController = TextEditingController(text: 'servo-thread');
    _recLenController =
        TextEditingController(text: '${s?.recLen ?? 4000}');
    _preTrigController =
        TextEditingController(text: '${s?.preTrig ?? 2000}');
    _multController = TextEditingController(text: '1');
  }

  @override
  void dispose() {
    _threadController.dispose();
    _recLenController.dispose();
    _preTrigController.dispose();
    _multController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Configure Capture'),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextField(
              controller: _threadController,
              decoration: const InputDecoration(
                labelText: 'Thread name',
                hintText: 'servo-thread',
                border: OutlineInputBorder(),
              ),
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _recLenController,
              decoration: const InputDecoration(
                labelText: 'Record length (samples)',
                border: OutlineInputBorder(),
              ),
              keyboardType: TextInputType.number,
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _preTrigController,
              decoration: const InputDecoration(
                labelText: 'Pre-trigger (samples)',
                border: OutlineInputBorder(),
              ),
              keyboardType: TextInputType.number,
            ),
            const SizedBox(height: 12),
            TextField(
              controller: _multController,
              decoration: const InputDecoration(
                labelText: 'Sample period multiplier',
                border: OutlineInputBorder(),
              ),
              keyboardType: TextInputType.number,
            ),
            if (_error != null) ...[
              const SizedBox(height: 12),
              Text(_error!, style: const TextStyle(color: Colors.red)),
            ],
          ],
        ),
      ),
      actions: [
        TextButton(
          onPressed: () => Navigator.pop(context),
          child: const Text('Cancel'),
        ),
        FilledButton(
          onPressed: _apply,
          child: const Text('Apply'),
        ),
      ],
    );
  }

  Future<void> _apply() async {
    final recLen = int.tryParse(_recLenController.text);
    final preTrig = int.tryParse(_preTrigController.text);
    final mult = int.tryParse(_multController.text);
    final thread = _threadController.text.trim();

    if (recLen == null || preTrig == null || mult == null || thread.isEmpty) {
      setState(() => _error = 'Invalid input');
      return;
    }

    try {
      await widget.client.configure(
        config: ws.CaptureConfig(
          threadName: thread,
          recLen: recLen,
          samplePeriodMult: mult,
          preTrig: preTrig,
        ),
      );
      if (mounted) Navigator.pop(context, true);
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    }
  }
}
