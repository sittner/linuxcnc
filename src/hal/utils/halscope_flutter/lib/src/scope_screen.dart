import 'dart:convert';
import 'dart:io' show File, Platform;
import 'dart:typed_data';

import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import '../../generated/halscope_client.dart' show HalscopeClient, ApiError;
import '../../generated/halscope_watch_client.dart';
import 'add_channel_dialog.dart';
import 'buffer_overview.dart';
import 'configure_dialog.dart';
import 'waveform_painter.dart';

const _defaultRestUrl = 'http://127.0.0.1:5080';

/// Derive the WebSocket watch URL from GMC_REST_URL (or default).
String _wsUrl() {
  final rest = (Platform.environment['GMC_REST_URL'] ?? _defaultRestUrl)
      .replaceFirst('https://', 'wss://')
      .replaceFirst('http://', 'ws://');
  final base = rest.endsWith('/') ? rest.substring(0, rest.length - 1) : rest;
  return '$base/api/v1/watch';
}

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

/// Run modes matching old GTK halscope.
enum RunMode { normal, single, roll, stop }

/// Main oscilloscope screen: waveform display + controls.
///
/// Layout mirrors the classic GTK halscope:
///  ┌──────┬────────────────────────────┬───────────┐
///  │ Vert │      Waveform Display      │  Trigger  │
///  │ Scale│                            │  -------  │
///  │  Pos │                            │  Source   │
///  │      │                            │  Level    │
///  │      │                            │  Edge     │
///  │      │                            │  Force    │
///  ├──────┴────────────────────────────┴───────────┤
///  │ CH [1][2][3]... [+]  | Run [N][S][R][St]      │
///  │ H-Zoom [slider] H-Pos [slider] | [Arm][Reset] │
///  └───────────────────────────────────────────────┘
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
  bool _disposed = false;
  late final String _serverUrl;

  // --- Display state (client-side) ---
  int _selectedChannel = -1; // which channel's vertical controls are active
  RunMode _runMode = RunMode.normal;

  // Per-channel vertical scale (log steps: -5..+5, 0 = auto)
  final Map<int, double> _vScale = {};
  // Per-channel vertical position (0.0 = center, -1.0 = bottom, 1.0 = top)
  final Map<int, double> _vPosition = {};
  // Per-channel vertical offset
  final Map<int, double> _vOffset = {};
  // Per-channel AC coupling
  final Map<int, bool> _acCoupling = {};

  // Horizontal zoom (1.0 = fit all, higher = zoom in)
  double _hZoom = 1.0;
  // Horizontal position (0.0 = left edge, 1.0 = right edge)
  double _hPosition = 0.5;

  // Current thread assignment
  String _threadName = '';

  // Trigger controls (local state, sent on change)
  int _trigChannel = 0;
  double _trigLevel = 0.0;
  TrigEdge _trigEdge = TrigEdge.rising;
  bool _trigForce = false;
  bool _trigAuto = true;

  // Cursor readout
  Offset? _cursorPosition; // in widget-local coords
  final GlobalKey _waveformKey = GlobalKey();

  @override
  void initState() {
    super.initState();
    _serverUrl = _wsUrl();
    _loadConfig();
    WidgetsBinding.instance.addPostFrameCallback((_) => _connect());
  }

  @override
  void dispose() {
    _disposed = true;
    _saveConfig();
    // dispose() is sync but client.dispose() is async — grab the client
    // reference, null it out to prevent further use, then fire-and-forget
    // the cleanup (subscription cancel + sink close).  The _disposed flag
    // ensures no callbacks call setState after this point.
    final client = _client;
    _client = null;
    client?.dispose();
    super.dispose();
  }

  // --- Connection ---

  Future<void> _connect() async {
    // Probe the REST API first to detect server/module availability.
    final restUrl = Platform.environment['GMC_REST_URL'] ?? _defaultRestUrl;
    final restClient = HalscopeClient(baseUrl: restUrl);
    try {
      await restClient.getStatus();
    } on ApiError catch (e) {
      if (e.statusCode == 404 && mounted) {
        _showModuleNotLoaded();
        return;
      }
      rethrow;
    } catch (e) {
      if (mounted) {
        _showError('Cannot reach server at $restUrl: $e');
      }
      return;
    }

    _client?.dispose();
    _client = HalscopeWsClient(url: _serverUrl);
    _client!.connect();

    _client!.watchWatchState(
      rateMs: 100,
      onData: (status) {
        if (_disposed || !mounted) return;
        setState(() {
          _status = status;
          // Auto-rearm in normal mode when capture completes
          if (_runMode == RunMode.normal &&
              status.state == ScopeState.done) {
            _arm();
          }
        });
      },
    );

    _client!.watchWatchSamples(
      rateMs: 100,
      onData: (data) {
        if (_disposed || !mounted) return;
        setState(() => _sampleData = data);
      },
    );

    setState(() => _connected = true);
  }

  void _showModuleNotLoaded() {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => AlertDialog(
        title: const Text('Halscope module not loaded'),
        content: const Text(
          'The server is running but the halscope API was not found.\n\n'
          'Add the following to your HAL configuration:\n\n'
          '  load halscope\n\n'
          'Then restart LinuxCNC.',
        ),
        actions: [
          TextButton(
            onPressed: () {
              Navigator.of(ctx).pop();
              _connect(); // retry
            },
            child: const Text('Retry'),
          ),
        ],
      ),
    );
  }

  // --- Actions with error handling ---

  void _showError(Object e) {
    if (_disposed || !mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(e.toString()),
        backgroundColor: Colors.red.shade800,
        duration: const Duration(seconds: 4),
      ),
    );
  }

  Future<void> _arm() async {
    if (_disposed) return;
    try {
      // Ensure trigger config is pushed before arming.
      await _applyTrigger();
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
      setState(() {
        _vScale.remove(channel);
        _vPosition.remove(channel);
        _vOffset.remove(channel);
        _acCoupling.remove(channel);
        if (_selectedChannel == channel) _selectedChannel = -1;
      });
    } catch (e) {
      _showError(e);
    }
  }

  Future<void> _applyTrigger() async {
    try {
      // C backend uses 1-based trigger channel (0 = disabled).
      // UI uses 0-based channel numbers matching set_channel().
      await _client?.setTrigger(
        trig: TriggerConfig(
          channel: _trigChannel + 1,
          level: _trigLevel,
          edge: _trigEdge,
          force: _trigForce,
          autoTrig: _trigAuto,
        ),
      );
    } catch (e) {
      _showError(e);
    }
  }

  Future<void> _forceTrigger() async {
    try {
      await _client?.setTrigger(
        trig: TriggerConfig(
          channel: _trigChannel + 1,
          level: _trigLevel,
          edge: _trigEdge,
          force: true,
          autoTrig: _trigAuto,
        ),
      );
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
        currentThread: _threadName,
      ),
    );
  }

  // --- Autosave / Autoload ---

  static String get _configPath {
    final home = Platform.environment['HOME'] ?? '.';
    return '$home/.halscope.json';
  }

  void _saveConfig() {
    try {
      final config = <String, dynamic>{
        'hZoom': _hZoom,
        'hPosition': _hPosition,
        'runMode': _runMode.index,
        'trigChannel': _trigChannel,
        'trigLevel': _trigLevel,
        'trigEdge': _trigEdge.value,
        'trigAuto': _trigAuto,
        'vScale': _vScale.map((k, v) => MapEntry(k.toString(), v)),
        'vPosition': _vPosition.map((k, v) => MapEntry(k.toString(), v)),
        'vOffset': _vOffset.map((k, v) => MapEntry(k.toString(), v)),
        'acCoupling': _acCoupling.map((k, v) => MapEntry(k.toString(), v)),
      };
      File(_configPath).writeAsStringSync(jsonEncode(config));
    } catch (_) {}
  }

  void _loadConfig() {
    try {
      final file = File(_configPath);
      if (!file.existsSync()) return;
      final config = jsonDecode(file.readAsStringSync()) as Map<String, dynamic>;
      setState(() {
        _hZoom = (config['hZoom'] as num?)?.toDouble() ?? 1.0;
        _hPosition = (config['hPosition'] as num?)?.toDouble() ?? 0.5;
        _runMode = RunMode.values[(config['runMode'] as int?) ?? 0];
        _trigChannel = (config['trigChannel'] as int?) ?? 0;
        _trigLevel = (config['trigLevel'] as num?)?.toDouble() ?? 0.0;
        _trigEdge = TrigEdge.fromValue((config['trigEdge'] as int?) ?? 1);
        _trigAuto = (config['trigAuto'] as bool?) ?? true;
        _loadMap(config, 'vScale', _vScale);
        _loadMap(config, 'vPosition', _vPosition);
        _loadMap(config, 'vOffset', _vOffset);
        _loadBoolMap(config, 'acCoupling', _acCoupling);
      });
    } catch (_) {}
  }

  void _loadMap(Map<String, dynamic> config, String key, Map<int, double> target) {
    final m = config[key] as Map<String, dynamic>?;
    if (m == null) return;
    target.clear();
    m.forEach((k, v) {
      final ch = int.tryParse(k);
      if (ch != null && v is num) target[ch] = v.toDouble();
    });
  }

  void _loadBoolMap(Map<String, dynamic> config, String key, Map<int, bool> target) {
    final m = config[key] as Map<String, dynamic>?;
    if (m == null) return;
    target.clear();
    m.forEach((k, v) {
      final ch = int.tryParse(k);
      if (ch != null && v is bool) target[ch] = v;
    });
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

  List<ChannelInfo> get _channels => _status?.channels ?? [];

  // --- Build ---

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('HAL Oscilloscope'),
        titleTextStyle: const TextStyle(fontSize: 14, fontWeight: FontWeight.bold),
        toolbarHeight: 36,
        actions: [
          if (_status != null) ...[
            _statusChip(_stateLabel(_status!.state), _stateColor(_status!.state)),
            const SizedBox(width: 8),
          ],
          IconButton(
            onPressed: _connected ? _showConfigureDialog : null,
            icon: const Icon(Icons.settings, size: 18),
            tooltip: 'Configure capture',
            visualDensity: VisualDensity.compact,
          ),
          Container(
            margin: const EdgeInsets.symmetric(horizontal: 4),
            child: Icon(
              _connected ? Icons.link : Icons.link_off,
              size: 16,
              color: _connected ? Colors.green : Colors.red,
            ),
          ),
        ],
      ),
      body: Column(
        children: [
          // Main area: vertical panel | waveform | trigger panel
          Expanded(
            child: Row(
              children: [
                _buildVerticalPanel(),
                Expanded(child: _buildWaveformDisplay()),
                _buildTriggerPanel(),
              ],
            ),
          ),
          // Buffer overview / position indicator
          BufferOverview(
            hZoom: _hZoom,
            hPosition: _hPosition,
            samples: _status?.samples ?? 0,
            recLen: _status?.recLen ?? 0,
            preTrig: _status?.preTrig ?? 0,
            onPositionChanged: (v) => setState(() => _hPosition = v),
            onZoomChanged: (v) => setState(() => _hZoom = v),
          ),
          // Bottom controls
          _buildBottomBar(),
        ],
      ),
    );
  }

  Widget _statusChip(String text, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      decoration: BoxDecoration(
        color: color.withOpacity(0.15),
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(text,
          style: TextStyle(
            fontSize: 11,
            fontFamily: 'monospace',
            fontWeight: FontWeight.bold,
            color: color,
          )),
    );
  }

  // ──────────────────────────────────────────────
  // LEFT: Vertical controls (per selected channel)
  // ──────────────────────────────────────────────

  Widget _buildVerticalPanel() {
    final hasCh = _selectedChannel >= 0 &&
        _channels.any((c) => c.channel == _selectedChannel);
    final chColor = hasCh
        ? _channelColors[_selectedChannel % _channelColors.length]
        : Colors.grey;

    return Container(
      width: 64,
      decoration: BoxDecoration(
        border: Border(right: BorderSide(color: Colors.blueGrey.shade800)),
      ),
      child: Column(
        children: [
          // Header
          Container(
            padding: const EdgeInsets.symmetric(vertical: 4),
            color: chColor.withOpacity(0.15),
            child: Center(
              child: Text(
                hasCh ? 'CH${_selectedChannel}' : 'VERT',
                style: TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.bold,
                  color: chColor,
                ),
              ),
            ),
          ),
          // Scale label
          const Padding(
            padding: EdgeInsets.only(top: 4),
            child: Text('Scale', style: TextStyle(fontSize: 9, color: Colors.white54)),
          ),
          // Scale slider (vertical)
          Expanded(
            child: RotatedBox(
              quarterTurns: 3,
              child: Slider(
                value: hasCh ? (_vScale[_selectedChannel] ?? 0.0) : 0.0,
                min: -5.0,
                max: 5.0,
                divisions: 20,
                onChanged: hasCh
                    ? (v) => setState(() => _vScale[_selectedChannel] = v)
                    : null,
                activeColor: chColor,
              ),
            ),
          ),
          // Position label
          const Text('Pos', style: TextStyle(fontSize: 9, color: Colors.white54)),
          // Position slider (vertical)
          Expanded(
            child: RotatedBox(
              quarterTurns: 3,
              child: Slider(
                value: hasCh ? (_vPosition[_selectedChannel] ?? 0.0) : 0.0,
                min: -1.0,
                max: 1.0,
                onChanged: hasCh
                    ? (v) => setState(() => _vPosition[_selectedChannel] = v)
                    : null,
                activeColor: chColor,
              ),
            ),
          ),
          // Offset label + value
          const Text('Offset', style: TextStyle(fontSize: 9, color: Colors.white54)),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 2, vertical: 2),
            child: SizedBox(
              height: 24,
              child: TextField(
                enabled: hasCh,
                controller: TextEditingController(
                  text: hasCh
                      ? (_vOffset[_selectedChannel] ?? 0.0).toStringAsFixed(3)
                      : '0',
                ),
                style: const TextStyle(fontSize: 10, fontFamily: 'monospace'),
                decoration: const InputDecoration(
                  isDense: true,
                  contentPadding: EdgeInsets.symmetric(horizontal: 4, vertical: 2),
                  border: OutlineInputBorder(),
                ),
                onSubmitted: hasCh
                    ? (v) {
                        final val = double.tryParse(v);
                        if (val != null) {
                          setState(() => _vOffset[_selectedChannel] = val);
                        }
                      }
                    : null,
              ),
            ),
          ),
          // AC coupling toggle
          Row(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              SizedBox(
                width: 20,
                height: 20,
                child: Checkbox(
                  value: hasCh ? (_acCoupling[_selectedChannel] ?? false) : false,
                  onChanged: hasCh
                      ? (v) => setState(() => _acCoupling[_selectedChannel] = v ?? false)
                      : null,
                  visualDensity: VisualDensity.compact,
                ),
              ),
              const SizedBox(width: 2),
              Text('AC', style: TextStyle(fontSize: 9, color: chColor)),
            ],
          ),
          const SizedBox(height: 4),
        ],
      ),
    );
  }

  // ──────────────────────────────────────────────
  // RIGHT: Trigger controls (always visible)
  // ──────────────────────────────────────────────

  Widget _buildTriggerPanel() {
    final channels = _channels;

    return Container(
      width: 130,
      decoration: BoxDecoration(
        border: Border(left: BorderSide(color: Colors.blueGrey.shade800)),
      ),
      child: SingleChildScrollView(
        child: Padding(
          padding: const EdgeInsets.all(6),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              const Center(
                child: Text('TRIGGER',
                    style: TextStyle(
                        fontSize: 10,
                        fontWeight: FontWeight.bold,
                        color: Colors.white54)),
              ),
              const Divider(height: 8),
              // Source channel
              const Text('Source', style: TextStyle(fontSize: 9, color: Colors.white54)),
              const SizedBox(height: 2),
              DropdownButton<int>(
                value: channels.any((c) => c.channel == _trigChannel)
                    ? _trigChannel
                    : (channels.isNotEmpty ? channels.first.channel : 0),
                isDense: true,
                isExpanded: true,
                style: const TextStyle(fontSize: 11, fontFamily: 'monospace'),
                items: channels.isEmpty
                    ? [const DropdownMenuItem(value: 0, child: Text('—'))]
                    : channels.map((ch) {
                        final color =
                            _channelColors[ch.channel % _channelColors.length];
                        return DropdownMenuItem(
                          value: ch.channel,
                          child: Text('CH${ch.channel}',
                              style: TextStyle(color: color, fontSize: 11)),
                        );
                      }).toList(),
                onChanged: _connected
                    ? (v) {
                        if (v != null) {
                          setState(() => _trigChannel = v);
                          _applyTrigger();
                        }
                      }
                    : null,
              ),
              const SizedBox(height: 6),
              // Level
              const Text('Level', style: TextStyle(fontSize: 9, color: Colors.white54)),
              const SizedBox(height: 2),
              SizedBox(
                height: 24,
                child: TextField(
                  controller: TextEditingController(
                      text: _trigLevel.toStringAsFixed(3)),
                  style: const TextStyle(fontSize: 10, fontFamily: 'monospace'),
                  decoration: const InputDecoration(
                    isDense: true,
                    contentPadding:
                        EdgeInsets.symmetric(horizontal: 4, vertical: 2),
                    border: OutlineInputBorder(),
                  ),
                  onSubmitted: (v) {
                    final val = double.tryParse(v);
                    if (val != null) {
                      setState(() => _trigLevel = val);
                      _applyTrigger();
                    }
                  },
                ),
              ),
              const SizedBox(height: 6),
              // Edge toggle
              const Text('Edge', style: TextStyle(fontSize: 9, color: Colors.white54)),
              const SizedBox(height: 2),
              SegmentedButton<TrigEdge>(
                segments: const [
                  ButtonSegment(value: TrigEdge.rising, label: Text('↑', style: TextStyle(fontSize: 14))),
                  ButtonSegment(value: TrigEdge.falling, label: Text('↓', style: TextStyle(fontSize: 14))),
                ],
                selected: {_trigEdge},
                onSelectionChanged: _connected
                    ? (v) {
                        setState(() => _trigEdge = v.first);
                        _applyTrigger();
                      }
                    : null,
                style: ButtonStyle(
                  visualDensity: VisualDensity.compact,
                  tapTargetSize: MaterialTapTargetSize.shrinkWrap,
                ),
              ),
              const SizedBox(height: 8),
              // Auto trigger toggle
              Row(
                children: [
                  SizedBox(
                    width: 24,
                    height: 24,
                    child: Checkbox(
                      value: _trigAuto,
                      onChanged: _connected
                          ? (v) {
                              setState(() => _trigAuto = v ?? true);
                              _applyTrigger();
                            }
                          : null,
                      visualDensity: VisualDensity.compact,
                    ),
                  ),
                  const SizedBox(width: 4),
                  const Text('Auto', style: TextStyle(fontSize: 10)),
                ],
              ),
              const SizedBox(height: 6),
              // Force trigger button
              SizedBox(
                height: 28,
                child: ElevatedButton(
                  onPressed: _connected ? _forceTrigger : null,
                  style: ElevatedButton.styleFrom(
                    backgroundColor: Colors.orange.shade900,
                    padding: EdgeInsets.zero,
                    textStyle: const TextStyle(fontSize: 10),
                  ),
                  child: const Text('Force'),
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  // ──────────────────────────────────────────
  // CENTER: Waveform display
  // ──────────────────────────────────────────

  Widget _buildWaveformDisplay() {
    return MouseRegion(
      onHover: (event) => setState(() => _cursorPosition = event.localPosition),
      onExit: (_) => setState(() => _cursorPosition = null),
      child: Container(
        key: _waveformKey,
        margin: const EdgeInsets.all(4),
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
              hZoom: _hZoom,
              hPosition: _hPosition,
              vScales: Map.from(_vScale),
              vPositions: Map.from(_vPosition),
              vOffsets: Map.from(_vOffset),
              acCoupling: Map.from(_acCoupling),
              cursorX: _cursorPosition?.dx,
              trigLevel: _trigLevel,
              trigChannel: _trigChannel,
              channels: _channels,
            ),
            size: Size.infinite,
          ),
        ),
      ),
    );
  }

  // ──────────────────────────────────────────
  // BOTTOM: Channel buttons + H controls + Run mode + Arm/Reset
  // ──────────────────────────────────────────

  Widget _buildBottomBar() {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      decoration: BoxDecoration(
        border: Border(top: BorderSide(color: Colors.blueGrey.shade800)),
      ),
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          // Row 1: Channel buttons
          _buildChannelButtons(),
          const SizedBox(height: 4),
          // Row 2: Horizontal controls + Run mode + Arm/Reset
          _buildHorizontalAndRunControls(),
        ],
      ),
    );
  }

  Widget _buildChannelButtons() {
    final channels = _channels;
    return Row(
      children: [
        // Numbered channel buttons
        Expanded(
          child: Wrap(
            spacing: 2,
            runSpacing: 2,
            children: [
              ...channels.map((ch) {
                final color =
                    _channelColors[ch.channel % _channelColors.length];
                final selected = ch.channel == _selectedChannel;
                return _ChannelButton(
                  channel: ch.channel,
                  color: color,
                  selected: selected,
                  pinName: ch.pinName,
                  onTap: () => setState(() =>
                      _selectedChannel =
                          selected ? -1 : ch.channel),
                  onDelete: () => _clearChannel(ch.channel),
                );
              }),
              // Add channel button
              SizedBox(
                height: 28,
                child: OutlinedButton.icon(
                  onPressed: _connected ? _showAddChannelDialog : null,
                  icon: const Icon(Icons.add, size: 14),
                  label: const Text('CH', style: TextStyle(fontSize: 10)),
                  style: OutlinedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(horizontal: 8),
                    visualDensity: VisualDensity.compact,
                  ),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildHorizontalAndRunControls() {
    final isIdle = _status?.state == ScopeState.idle ||
        _status?.state == ScopeState.done ||
        _status == null;
    final isCapturing = _status != null &&
        _status!.state != ScopeState.idle &&
        _status!.state != ScopeState.done;

    return Row(
      children: [
        // Horizontal zoom
        const Text('Zoom', style: TextStyle(fontSize: 9, color: Colors.white54)),
        SizedBox(
          width: 100,
          child: Slider(
            value: _hZoom,
            min: 1.0,
            max: 32.0,
            onChanged: (v) => setState(() => _hZoom = v),
            activeColor: Colors.blueGrey.shade300,
          ),
        ),
        const SizedBox(width: 8),
        // Separator
        Container(width: 1, height: 24, color: Colors.blueGrey.shade800),
        const SizedBox(width: 8),
        // Run mode
        const Text('Run', style: TextStyle(fontSize: 9, color: Colors.white54)),
        const SizedBox(width: 4),
        SegmentedButton<RunMode>(
          segments: const [
            ButtonSegment(value: RunMode.normal, label: Text('N', style: TextStyle(fontSize: 10))),
            ButtonSegment(value: RunMode.single, label: Text('S', style: TextStyle(fontSize: 10))),
            ButtonSegment(value: RunMode.roll, label: Text('R', style: TextStyle(fontSize: 10))),
            ButtonSegment(value: RunMode.stop, label: Text('St', style: TextStyle(fontSize: 10))),
          ],
          selected: {_runMode},
          onSelectionChanged: (v) {
            setState(() => _runMode = v.first);
            if (_runMode == RunMode.stop) {
              _reset();
            }
          },
          style: ButtonStyle(
            visualDensity: VisualDensity.compact,
            tapTargetSize: MaterialTapTargetSize.shrinkWrap,
          ),
        ),
        const Spacer(),
        // Arm
        SizedBox(
          height: 28,
          child: ElevatedButton.icon(
            onPressed: _connected && isIdle && _runMode != RunMode.stop
                ? _arm
                : null,
            icon: const Icon(Icons.play_arrow, size: 16),
            label: const Text('Arm', style: TextStyle(fontSize: 11)),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.green.shade800,
              padding: const EdgeInsets.symmetric(horizontal: 10),
            ),
          ),
        ),
        const SizedBox(width: 6),
        // Reset
        SizedBox(
          height: 28,
          child: ElevatedButton.icon(
            onPressed: _connected && isCapturing ? _reset : null,
            icon: const Icon(Icons.stop, size: 16),
            label: const Text('Reset', style: TextStyle(fontSize: 11)),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.red.shade800,
              padding: const EdgeInsets.symmetric(horizontal: 10),
            ),
          ),
        ),
      ],
    );
  }
}

/// Compact channel toggle button with color coding and right-click delete.
class _ChannelButton extends StatelessWidget {
  final int channel;
  final Color color;
  final bool selected;
  final String pinName;
  final VoidCallback onTap;
  final VoidCallback onDelete;

  const _ChannelButton({
    required this.channel,
    required this.color,
    required this.selected,
    required this.pinName,
    required this.onTap,
    required this.onDelete,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onSecondaryTap: onDelete,
      child: Tooltip(
        message: '$pinName (right-click to remove)',
        child: Material(
          color: selected ? color.withOpacity(0.3) : Colors.transparent,
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(4),
            side: BorderSide(color: color, width: selected ? 2 : 1),
          ),
          child: InkWell(
            onTap: onTap,
            borderRadius: BorderRadius.circular(4),
            child: SizedBox(
              width: 28,
              height: 28,
              child: Center(
                child: Text(
                  '${channel}',
                  style: TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.bold,
                    color: color,
                  ),
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
