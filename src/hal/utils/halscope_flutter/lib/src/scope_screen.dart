import 'dart:typed_data';

import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart';
import 'add_channel_dialog.dart';
import 'configure_dialog.dart';
import 'trigger_dialog.dart';
import 'waveform_painter.dart';

/// Channel colors — matches classic halscope (same as waveform_painter).
const _channelColors = [
  Colors.yellow,
  Colors.cyan,
  Colors.green,
  Colors.red,
  Colors.white,
  Colors.orange,
  Colors.pink,
  Colors.lightBlue,
  Colors.lime,
  Colors.purple,
  Colors.teal,
  Colors.amber,
  Colors.indigo,
  Colors.deepOrange,
  Colors.lightGreen,
  Colors.brown,
];

/// Main oscilloscope screen: waveform display + controls.
class ScopeScreen extends StatefulWidget {
  const ScopeScreen({super.key});

  @override
  State<ScopeScreen> createState() => _ScopeScreenState();
}

class _ScopeScreenState extends State<ScopeScreen> {
  HalscopeWsClient? _client;
  ScopeStatus? _status;
  Uint8List? _sampleData;
  bool _connected = false;
  String _serverUrl = 'ws://localhost:5080/api/v1/watch';

  final _urlController = TextEditingController();

  @override
  void initState() {
    super.initState();
    _urlController.text = _serverUrl;
  }

  @override
  void dispose() {
    _client?.dispose();
    _urlController.dispose();
    super.dispose();
  }

  // --- Connection ---

  void _connect() {
    _client?.dispose();

    _serverUrl = _urlController.text;
    _client = HalscopeWsClient(url: _serverUrl);
    _client!.connect();

    _client!.watchWatchState(
      rateMs: 100,
      onData: (status) {
        setState(() => _status = status);
      },
    );

    _client!.watchWatchSamples(
      rateMs: 100,
      onData: (data) {
        setState(() => _sampleData = data);
      },
    );

    setState(() => _connected = true);
  }

  void _disconnect() {
    _client?.dispose();
    _client = null;
    setState(() {
      _connected = false;
      _status = null;
      _sampleData = null;
    });
  }

  // --- Actions with error handling ---

  void _showError(Object e) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(e.toString()),
        backgroundColor: Colors.red.shade800,
        duration: const Duration(seconds: 4),
      ),
    );
  }

  Future<void> _arm() async {
    try {
      await _client?.arm();
    } catch (e) {
      _showError(e);
    }
  }

  Future<void> _reset() async {
    try {
      await _client?.reset();
    } catch (e) {
      _showError(e);
    }
  }

  Future<void> _clearChannel(int channel) async {
    try {
      await _client?.clearChannel(channel: channel);
    } catch (e) {
      _showError(e);
    }
  }

  // --- Dialogs ---

  Future<void> _showAddChannelDialog() async {
    if (_client == null) return;
    await showDialog<bool>(
      context: context,
      builder: (_) => AddChannelDialog(
        client: _client!,
        activeChannels: _status?.channels ?? [],
      ),
    );
  }

  Future<void> _showConfigureDialog() async {
    if (_client == null) return;
    await showDialog<bool>(
      context: context,
      builder: (_) => ConfigureDialog(
        client: _client!,
        currentStatus: _status,
      ),
    );
  }

  Future<void> _showTriggerDialog() async {
    if (_client == null) return;
    await showDialog<bool>(
      context: context,
      builder: (_) => TriggerDialog(
        client: _client!,
        activeChannels: _status?.channels ?? [],
      ),
    );
  }

  // --- Helpers ---

  String _stateLabel(ScopeState? state) {
    if (state == null) return '—';
    switch (state) {
      case ScopeState.idle:
        return 'IDLE';
      case ScopeState.init:
        return 'INIT';
      case ScopeState.preTrig:
        return 'PRE-TRIG';
      case ScopeState.trigWait:
        return 'TRIG WAIT';
      case ScopeState.postTrig:
        return 'POST-TRIG';
      case ScopeState.done:
        return 'DONE';
      case ScopeState.reset:
        return 'RESET';
    }
  }

  Color _stateColor(ScopeState? state) {
    if (state == null) return Colors.grey;
    switch (state) {
      case ScopeState.idle:
        return Colors.grey;
      case ScopeState.done:
        return Colors.green;
      case ScopeState.reset:
        return Colors.orange;
      default:
        return Colors.amber;
    }
  }

  // --- Build ---

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('HAL Oscilloscope'),
        actions: [
          IconButton(
            onPressed: _connected ? _showConfigureDialog : null,
            icon: const Icon(Icons.settings),
            tooltip: 'Configure',
          ),
          IconButton(
            onPressed: _connected ? _showTriggerDialog : null,
            icon: const Icon(Icons.bolt),
            tooltip: 'Trigger',
          ),
          const SizedBox(width: 8),
          Container(
            margin: const EdgeInsets.symmetric(horizontal: 8),
            child: Icon(
              _connected ? Icons.link : Icons.link_off,
              color: _connected ? Colors.green : Colors.red,
            ),
          ),
        ],
      ),
      body: Column(
        children: [
          _buildConnectionBar(),
          if (_status != null) _buildStatusBar(),
          if (_status != null && _status!.channels.isNotEmpty)
            _buildChannelBar(),
          Expanded(child: _buildWaveformDisplay()),
          _buildControlBar(),
        ],
      ),
    );
  }

  Widget _buildConnectionBar() {
    return Padding(
      padding: const EdgeInsets.all(8),
      child: Row(
        children: [
          Expanded(
            child: TextField(
              controller: _urlController,
              decoration: const InputDecoration(
                labelText: 'Server URL',
                isDense: true,
                border: OutlineInputBorder(),
              ),
              enabled: !_connected,
            ),
          ),
          const SizedBox(width: 8),
          ElevatedButton.icon(
            onPressed: _connected ? _disconnect : _connect,
            icon: Icon(_connected ? Icons.link_off : Icons.link),
            label: Text(_connected ? 'Disconnect' : 'Connect'),
          ),
        ],
      ),
    );
  }

  Widget _buildStatusBar() {
    final s = _status!;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
      color: _stateColor(s.state).withOpacity(0.15),
      child: Row(
        children: [
          _statusChip('State', _stateLabel(s.state), _stateColor(s.state)),
          const SizedBox(width: 16),
          _statusChip('Samples', '${s.samples}/${s.recLen}', null),
          const SizedBox(width: 16),
          _statusChip('Pre-trig', '${s.preTrig}', null),
          const SizedBox(width: 16),
          _statusChip('Channels', '${s.sampleLen}', null),
        ],
      ),
    );
  }

  Widget _statusChip(String label, String value, Color? color) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Text('$label: ',
            style: Theme.of(context)
                .textTheme
                .bodySmall
                ?.copyWith(color: Colors.white70)),
        Text(value,
            style: TextStyle(
              fontWeight: FontWeight.bold,
              color: color ?? Colors.white,
              fontFamily: 'monospace',
            )),
      ],
    );
  }

  Widget _buildChannelBar() {
    final channels = _status!.channels;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      child: Row(
        children: [
          const Icon(Icons.timeline, size: 16, color: Colors.white54),
          const SizedBox(width: 8),
          Expanded(
            child: Wrap(
              spacing: 8,
              runSpacing: 4,
              children: channels.map((ch) {
                final color =
                    _channelColors[ch.channel % _channelColors.length];
                return Chip(
                  avatar: CircleAvatar(
                    backgroundColor: color,
                    radius: 6,
                  ),
                  label: Text(
                    'CH${ch.channel}: ${ch.pinName}',
                    style: TextStyle(
                      fontSize: 12,
                      fontFamily: 'monospace',
                      color: color,
                    ),
                  ),
                  deleteIcon:
                      const Icon(Icons.close, size: 14),
                  onDeleted: () => _clearChannel(ch.channel),
                  materialTapTargetSize:
                      MaterialTapTargetSize.shrinkWrap,
                  visualDensity: VisualDensity.compact,
                );
              }).toList(),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildWaveformDisplay() {
    return Container(
      margin: const EdgeInsets.all(8),
      decoration: BoxDecoration(
        color: Colors.black,
        border: Border.all(color: Colors.blueGrey.shade700),
        borderRadius: BorderRadius.circular(4),
      ),
      child: ClipRRect(
        borderRadius: BorderRadius.circular(4),
        child: CustomPaint(
          painter: WaveformPainter(
            sampleData: _sampleData,
            sampleLen: _status?.sampleLen ?? 0,
          ),
          size: Size.infinite,
        ),
      ),
    );
  }

  Widget _buildControlBar() {
    final isIdle = _status?.state == ScopeState.idle ||
        _status?.state == ScopeState.done ||
        _status == null;
    final isCapturing = _status != null &&
        _status!.state != ScopeState.idle &&
        _status!.state != ScopeState.done;

    return Padding(
      padding: const EdgeInsets.all(8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          // Add Channel
          OutlinedButton.icon(
            onPressed: _connected ? _showAddChannelDialog : null,
            icon: const Icon(Icons.add, size: 18),
            label: const Text('Channel'),
          ),
          const SizedBox(width: 12),
          // Arm
          ElevatedButton.icon(
            onPressed: _connected && isIdle ? _arm : null,
            icon: const Icon(Icons.play_arrow),
            label: const Text('Arm'),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.green.shade800,
            ),
          ),
          const SizedBox(width: 12),
          // Reset
          ElevatedButton.icon(
            onPressed: _connected && isCapturing ? _reset : null,
            icon: const Icon(Icons.stop),
            label: const Text('Reset'),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.red.shade800,
            ),
          ),
        ],
      ),
    );
  }
}
