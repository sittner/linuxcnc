import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart' as ws;

/// Dialog to configure trigger settings.
class TriggerDialog extends StatefulWidget {
  final ws.HalscopeWsClient client;
  final List<ws.ChannelInfo> activeChannels;

  const TriggerDialog({
    super.key,
    required this.client,
    required this.activeChannels,
  });

  @override
  State<TriggerDialog> createState() => _TriggerDialogState();
}

class _TriggerDialogState extends State<TriggerDialog> {
  late int _channel;
  final _levelController = TextEditingController(text: '0.0');
  ws.TrigEdge _edge = ws.TrigEdge.rising;
  bool _force = false;
  bool _autoTrig = true;
  String? _error;

  @override
  void initState() {
    super.initState();
    _channel = widget.activeChannels.isNotEmpty
        ? widget.activeChannels.first.channel
        : 0;
  }

  @override
  void dispose() {
    _levelController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AlertDialog(
      title: const Text('Trigger Setup'),
      content: SingleChildScrollView(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // Channel selector
            DropdownButtonFormField<int>(
              value: _channel,
              decoration: const InputDecoration(
                labelText: 'Trigger channel',
                border: OutlineInputBorder(),
              ),
              items: widget.activeChannels.isEmpty
                  ? [
                      const DropdownMenuItem(
                          value: 0, child: Text('CH 0 (no channels)'))
                    ]
                  : widget.activeChannels.map((ch) {
                      return DropdownMenuItem(
                        value: ch.channel,
                        child: Text('CH ${ch.channel}: ${ch.pinName}'),
                      );
                    }).toList(),
              onChanged: (v) {
                if (v != null) setState(() => _channel = v);
              },
            ),
            const SizedBox(height: 12),
            // Level
            TextField(
              controller: _levelController,
              decoration: const InputDecoration(
                labelText: 'Trigger level',
                border: OutlineInputBorder(),
              ),
              keyboardType:
                  const TextInputType.numberWithOptions(decimal: true),
            ),
            const SizedBox(height: 12),
            // Edge
            DropdownButtonFormField<ws.TrigEdge>(
              value: _edge,
              decoration: const InputDecoration(
                labelText: 'Edge',
                border: OutlineInputBorder(),
              ),
              items: const [
                DropdownMenuItem(
                    value: ws.TrigEdge.rising, child: Text('Rising')),
                DropdownMenuItem(
                    value: ws.TrigEdge.falling, child: Text('Falling')),
              ],
              onChanged: (v) {
                if (v != null) setState(() => _edge = v);
              },
            ),
            const SizedBox(height: 12),
            // Toggles
            SwitchListTile(
              title: const Text('Force trigger'),
              subtitle: const Text('Trigger immediately'),
              value: _force,
              onChanged: (v) => setState(() => _force = v),
            ),
            SwitchListTile(
              title: const Text('Auto trigger'),
              subtitle: const Text('Auto-trigger after timeout'),
              value: _autoTrig,
              onChanged: (v) => setState(() => _autoTrig = v),
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
    final level = double.tryParse(_levelController.text);
    if (level == null) {
      setState(() => _error = 'Invalid trigger level');
      return;
    }

    try {
      await widget.client.setTrigger(
        trig: ws.TriggerConfig(
          channel: _channel,
          level: level,
          edge: _edge,
          force: _force,
          autoTrig: _autoTrig,
        ),
      );
      if (mounted) Navigator.pop(context, true);
    } catch (e) {
      if (mounted) setState(() => _error = e.toString());
    }
  }
}
