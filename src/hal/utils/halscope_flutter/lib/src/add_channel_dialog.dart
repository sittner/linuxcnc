import 'package:flutter/material.dart';
import '../../generated/halscope_client.dart';
import '../../generated/halscope_watch_client.dart' as ws;

/// Dialog to search and select a HAL pin, then assign it to a scope channel.
class AddChannelDialog extends StatefulWidget {
  final ws.HalscopeWsClient client;
  final List<ws.ChannelInfo> activeChannels;

  const AddChannelDialog({
    super.key,
    required this.client,
    required this.activeChannels,
  });

  @override
  State<AddChannelDialog> createState() => _AddChannelDialogState();
}

class _AddChannelDialogState extends State<AddChannelDialog> {
  final _searchController = TextEditingController(text: '*');
  List<String> _pins = [];
  bool _loading = false;
  String? _error;
  String? _selectedPin;
  int _selectedChannel = 0;

  @override
  void initState() {
    super.initState();
    // Find first free channel slot
    final used = widget.activeChannels.map((c) => c.channel).toSet();
    for (int i = 0; i < ws.maxChannels; i++) {
      if (!used.contains(i)) {
        _selectedChannel = i;
        break;
      }
    }
    _search();
  }

  @override
  void dispose() {
    _searchController.dispose();
    super.dispose();
  }

  Future<void> _search() async {
    setState(() {
      _loading = true;
      _error = null;
    });
    try {
      final pattern = _searchController.text.isEmpty ? '*' : _searchController.text;
      final result = await widget.client.listPins(pattern: pattern);
      setState(() {
        _pins = result;
        _loading = false;
        _selectedPin = null;
      });
    } catch (e) {
      setState(() {
        _error = e.toString();
        _loading = false;
      });
    }
  }

  Set<int> get _usedChannels =>
      widget.activeChannels.map((c) => c.channel).toSet();

  @override
  Widget build(BuildContext context) {
    return Dialog(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 500, maxHeight: 600),
        child: Padding(
          padding: const EdgeInsets.all(16),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text('Add Channel',
                  style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 12),
              // Search bar
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _searchController,
                      decoration: const InputDecoration(
                        labelText: 'Pin pattern (glob)',
                        hintText: 'joint.0.*',
                        isDense: true,
                        border: OutlineInputBorder(),
                        prefixIcon: Icon(Icons.search),
                      ),
                      onSubmitted: (_) => _search(),
                    ),
                  ),
                  const SizedBox(width: 8),
                  IconButton(
                    onPressed: _loading ? null : _search,
                    icon: const Icon(Icons.refresh),
                  ),
                ],
              ),
              const SizedBox(height: 8),
              // Channel selector
              Row(
                children: [
                  const Text('Channel: '),
                  const SizedBox(width: 8),
                  DropdownButton<int>(
                    value: _selectedChannel,
                    items: List.generate(ws.maxChannels, (i) {
                      final inUse = _usedChannels.contains(i);
                      return DropdownMenuItem(
                        value: i,
                        child: Text(
                          'CH $i${inUse ? " (in use)" : ""}',
                          style: TextStyle(
                            color: inUse ? Colors.grey : null,
                          ),
                        ),
                      );
                    }),
                    onChanged: (v) {
                      if (v != null) setState(() => _selectedChannel = v);
                    },
                  ),
                ],
              ),
              const SizedBox(height: 8),
              if (_error != null)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Text(_error!,
                      style: const TextStyle(color: Colors.red)),
                ),
              // Pin list
              Expanded(
                child: _loading
                    ? const Center(child: CircularProgressIndicator())
                    : _pins.isEmpty
                        ? const Center(
                            child: Text('No pins found',
                                style: TextStyle(color: Colors.white38)))
                        : ListView.builder(
                            itemCount: _pins.length,
                            itemBuilder: (context, index) {
                              final pin = _pins[index];
                              return ListTile(
                                dense: true,
                                title: Text(pin,
                                    style: const TextStyle(
                                        fontFamily: 'monospace',
                                        fontSize: 13)),
                                selected: pin == _selectedPin,
                                selectedTileColor:
                                    Colors.blueGrey.withOpacity(0.3),
                                onTap: () =>
                                    setState(() => _selectedPin = pin),
                              );
                            },
                          ),
              ),
              const SizedBox(height: 12),
              // Actions
              Row(
                mainAxisAlignment: MainAxisAlignment.end,
                children: [
                  TextButton(
                    onPressed: () => Navigator.pop(context),
                    child: const Text('Cancel'),
                  ),
                  const SizedBox(width: 8),
                  FilledButton(
                    onPressed: _selectedPin == null ? null : _add,
                    child: const Text('Add'),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }

  Future<void> _add() async {
    try {
      await widget.client.setChannel(
        ch: ws.ChannelConfig(
          channel: _selectedChannel,
          pinName: _selectedPin!,
        ),
      );
      if (mounted) Navigator.pop(context, true);
    } catch (e) {
      if (mounted) {
        setState(() => _error = e.toString());
      }
    }
  }
}
