import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart' as ws;

/// Dialog to configure capture parameters: thread, record length, pre-trigger.
class ConfigureDialog extends StatefulWidget {
  final ws.HalscopeWsClient client;
  final ws.ScopeStatus? currentStatus;
  final String currentThread;

  const ConfigureDialog({
    super.key,
    required this.client,
    this.currentStatus,
    this.currentThread = '',
  });

  @override
  State<ConfigureDialog> createState() => _ConfigureDialogState();
}

class _ConfigureDialogState extends State<ConfigureDialog> {
  late final TextEditingController _recLenController;
  late final TextEditingController _preTrigController;
  late final TextEditingController _multController;
  String? _error;
  List<ws.ThreadInfo>? _threads;
  String? _selectedThread;

  @override
  void initState() {
    super.initState();
    final s = widget.currentStatus;
    _recLenController =
        TextEditingController(text: '${s?.recLen ?? 4000}');
    _preTrigController =
        TextEditingController(text: '${s?.preTrig ?? 2000}');
    _multController = TextEditingController(text: '1');
    _selectedThread =
        widget.currentThread.isNotEmpty ? widget.currentThread : null;
    _fetchThreads();
  }

  Future<void> _fetchThreads() async {
    try {
      final threads = await widget.client.listThreads();
      if (mounted) {
        setState(() {
          _threads = threads;
          // Auto-select first thread if nothing selected
          if (_selectedThread == null && threads.isNotEmpty) {
            _selectedThread = threads.first.name;
          }
        });
      }
    } catch (e) {
      if (mounted) setState(() => _error = 'Failed to list threads: $e');
    }
  }

  @override
  void dispose() {
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
            // Thread selector
            if (_threads == null)
              const Padding(
                padding: EdgeInsets.only(bottom: 12),
                child: LinearProgressIndicator(),
              )
            else
              Padding(
                padding: const EdgeInsets.only(bottom: 12),
                child: DropdownButtonFormField<String>(
                  value: _selectedThread,
                  decoration: const InputDecoration(
                    labelText: 'Thread',
                    border: OutlineInputBorder(),
                  ),
                  items: _threads!
                      .map((t) => DropdownMenuItem(
                            value: t.name,
                            child: Text(
                                '${t.name} (${(t.periodNs / 1000000).toStringAsFixed(1)} ms)'),
                          ))
                      .toList(),
                  onChanged: (v) => setState(() => _selectedThread = v),
                ),
              ),
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

    if (recLen == null || preTrig == null || mult == null) {
      setState(() => _error = 'Invalid input');
      return;
    }

    if (_selectedThread == null || _selectedThread!.isEmpty) {
      setState(() => _error = 'Please select a thread');
      return;
    }

    try {
      await widget.client.configure(
        config: ws.CaptureConfig(
          threadName: _selectedThread!,
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
