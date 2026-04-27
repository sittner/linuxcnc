import 'dart:typed_data';

import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart';
import 'waveform_painter.dart';

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

  void _connect() {
    _client?.dispose();

    _serverUrl = _urlController.text;
    _client = HalscopeWsClient(url: _serverUrl);
    _client!.connect();

    // Subscribe to state updates
    _client!.watchWatchState(
      rateMs: 100,
      onData: (status) {
        setState(() {
          _status = status;
        });
      },
    );

    // Subscribe to sample data (binary)
    _client!.watchWatchSamples(
      rateMs: 100,
      onData: (data) {
        setState(() {
          _sampleData = data;
        });
      },
    );

    setState(() {
      _connected = true;
    });
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

  Future<void> _arm() async {
    if (_client == null) return;
    await _client!.arm();
  }

  Future<void> _reset() async {
    if (_client == null) return;
    await _client!.reset();
  }

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

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('HAL Oscilloscope'),
        actions: [
          // Connection status indicator
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
          // Connection bar
          _buildConnectionBar(),
          // Status bar
          if (_status != null) _buildStatusBar(),
          // Waveform display
          Expanded(child: _buildWaveformDisplay()),
          // Control bar
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
        _status?.state == ScopeState.done;
    final isCapturing = _status != null &&
        _status!.state != ScopeState.idle &&
        _status!.state != ScopeState.done;

    return Padding(
      padding: const EdgeInsets.all(8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          ElevatedButton.icon(
            onPressed: _connected && isIdle ? _arm : null,
            icon: const Icon(Icons.play_arrow),
            label: const Text('Arm'),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.green.shade800,
            ),
          ),
          const SizedBox(width: 12),
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
