import 'dart:typed_data';
import 'dart:ui';

import 'package:flutter/material.dart';
import '../../generated/halscope_watch_client.dart' show ChannelInfo;

/// Channel colors — matches classic halscope
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

/// Renders captured waveform data from binary WebSocket frames.
///
/// Binary layout (from halscope_rt cmod):
///   [4 bytes: sample_count (uint32 LE)]
///   [4 bytes: sample_len   (uint32 LE)]  — channels per sample
///   [4 bytes: start_offset (uint32 LE)]
///   [4 bytes: reserved]
///   [sample_count × sample_len × 8 bytes: scope_data_t values]
///
/// Each scope_data_t is an 8-byte union; we interpret as float64 (double)
/// for display. The actual type per channel would come from channel config.
class WaveformPainter extends CustomPainter {
  final Uint8List? sampleData;
  final int sampleLen;

  /// Horizontal zoom (1.0 = fit all, higher = zoom in).
  final double hZoom;
  /// Horizontal position (0.0 = left, 1.0 = right) within zoomed view.
  final double hPosition;

  /// Per-channel vertical scale (log steps, 0 = auto).
  final Map<int, double> vScales;
  /// Per-channel vertical position (-1..1, 0 = center).
  final Map<int, double> vPositions;
  /// Per-channel vertical offset.
  final Map<int, double> vOffsets;
  /// Per-channel AC coupling (subtract mean).
  final Map<int, bool> acCoupling;

  /// Cursor X position (widget-local), null if not hovering.
  final double? cursorX;

  /// Trigger level for visual marker.
  final double trigLevel;
  /// Trigger channel for visual marker.
  final int trigChannel;

  /// Active channel info (for cursor readout labels).
  final List<ChannelInfo> channels;

  WaveformPainter({
    this.sampleData,
    required this.sampleLen,
    this.hZoom = 1.0,
    this.hPosition = 0.5,
    this.vScales = const {},
    this.vPositions = const {},
    this.vOffsets = const {},
    this.acCoupling = const {},
    this.cursorX,
    this.trigLevel = 0.0,
    this.trigChannel = 0,
    this.channels = const [],
  });

  @override
  void paint(Canvas canvas, Size size) {
    // Draw grid
    _drawGrid(canvas, size);

    if (sampleData == null || sampleData!.length < 16 || sampleLen == 0) {
      // No data — draw placeholder text
      final textPainter = TextPainter(
        text: const TextSpan(
          text: 'No capture data',
          style: TextStyle(color: Colors.white24, fontSize: 16),
        ),
        textDirection: TextDirection.ltr,
      );
      textPainter.layout();
      textPainter.paint(
        canvas,
        Offset(
          (size.width - textPainter.width) / 2,
          (size.height - textPainter.height) / 2,
        ),
      );
      return;
    }

    // Parse header
    final byteData = ByteData.sublistView(sampleData!);
    final sampleCount = byteData.getUint32(0, Endian.little);
    final chCount = byteData.getUint32(4, Endian.little);
    // final startOffset = byteData.getUint32(8, Endian.little);
    // final reserved = byteData.getUint32(12, Endian.little);

    if (sampleCount == 0 || chCount == 0) return;

    const headerSize = 16;
    final dataBytes = sampleData!.length - headerSize;
    final expectedBytes = sampleCount * chCount * 8;
    if (dataBytes < expectedBytes) return;

    // Draw each channel
    for (int ch = 0; ch < chCount && ch < 16; ch++) {
      _drawChannel(
        canvas, size, byteData, headerSize,
        sampleCount, chCount, ch,
      );
    }

    // Draw cursor crosshair and readout
    if (cursorX != null) {
      _drawCursor(canvas, size, byteData, headerSize, sampleCount, chCount);
    }
  }

  void _drawGrid(Canvas canvas, Size size) {
    final paint = Paint()
      ..color = Colors.white10
      ..strokeWidth = 0.5;

    // Vertical divisions (10)
    for (int i = 1; i < 10; i++) {
      final x = size.width * i / 10;
      canvas.drawLine(Offset(x, 0), Offset(x, size.height), paint);
    }

    // Horizontal divisions (8)
    for (int i = 1; i < 8; i++) {
      final y = size.height * i / 8;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), paint);
    }

    // Center cross (brighter)
    final centerPaint = Paint()
      ..color = Colors.white24
      ..strokeWidth = 1;
    canvas.drawLine(
      Offset(size.width / 2, 0),
      Offset(size.width / 2, size.height),
      centerPaint,
    );
    canvas.drawLine(
      Offset(0, size.height / 2),
      Offset(size.width, size.height / 2),
      centerPaint,
    );
  }

  void _drawChannel(
    Canvas canvas,
    Size size,
    ByteData byteData,
    int headerSize,
    int sampleCount,
    int chCount,
    int ch,
  ) {
    final paint = Paint()
      ..color = _channelColors[ch % _channelColors.length]
      ..strokeWidth = 1.5
      ..style = PaintingStyle.stroke
      ..isAntiAlias = true;

    // Extract sample values for this channel (interpret as float64)
    final values = <double>[];
    for (int i = 0; i < sampleCount; i++) {
      final offset = headerSize + (i * chCount + ch) * 8;
      if (offset + 8 <= sampleData!.length) {
        values.add(byteData.getFloat64(offset, Endian.little));
      }
    }

    if (values.isEmpty) return;

    // Apply horizontal zoom: select visible sample range
    final totalSamples = values.length;
    final visibleSamples = (totalSamples / hZoom).clamp(1, totalSamples).toInt();
    final maxStart = totalSamples - visibleSamples;
    final startIdx = (maxStart * hPosition).round().clamp(0, maxStart);
    final endIdx = (startIdx + visibleSamples).clamp(0, totalSamples);
    final visibleValues = values.sublist(startIdx, endIdx);

    if (visibleValues.isEmpty) return;

    // Apply vertical offset
    final vOff = vOffsets[ch] ?? 0.0;
    List<double> adjusted = visibleValues.map((v) => v - vOff).toList();

    // AC coupling: subtract mean
    if (acCoupling[ch] == true && adjusted.isNotEmpty) {
      double sum = 0;
      for (final v in adjusted) sum += v;
      final mean = sum / adjusted.length;
      adjusted = adjusted.map((v) => v - mean).toList();
    }

    // Auto-scale: find min/max of visible data
    double vMin = adjusted[0];
    double vMax = adjusted[0];
    for (final v in adjusted) {
      if (v < vMin) vMin = v;
      if (v > vMax) vMax = v;
    }

    // Add 5% margin
    double range = vMax - vMin;
    if (range < 1e-15) {
      vMin -= 1;
      vMax += 1;
      range = 2.0;
    } else {
      vMin -= range * 0.05;
      vMax += range * 0.05;
      range = vMax - vMin;
    }

    // Apply vertical scale: scale step adjusts the visible range
    //  0 = auto, negative = zoom out, positive = zoom in
    final vScaleStep = vScales[ch] ?? 0.0;
    if (vScaleStep != 0.0) {
      final factor = 1.0 / (1.0 + vScaleStep * 0.2).clamp(0.1, 10.0);
      final center = (vMin + vMax) / 2;
      final halfRange = range * factor / 2;
      vMin = center - halfRange;
      vMax = center + halfRange;
    }

    // Apply vertical position shift
    final vPos = vPositions[ch] ?? 0.0;
    if (vPos != 0.0) {
      final shift = (vMax - vMin) * vPos * 0.5;
      vMin += shift;
      vMax += shift;
    }

    // Build path
    final path = Path();
    final count = adjusted.length;
    for (int i = 0; i < count; i++) {
      final x = size.width * i / (count - 1).clamp(1, count);
      final y = size.height * (1.0 - (adjusted[i] - vMin) / (vMax - vMin));

      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }

    canvas.drawPath(path, paint);
  }

  /// Draw cursor crosshair and per-channel value readout at cursor position.
  void _drawCursor(
    Canvas canvas,
    Size size,
    ByteData byteData,
    int headerSize,
    int sampleCount,
    int chCount,
  ) {
    final cx = cursorX!.clamp(0.0, size.width);

    // Vertical crosshair line
    final crossPaint = Paint()
      ..color = Colors.white38
      ..strokeWidth = 0.5;
    canvas.drawLine(Offset(cx, 0), Offset(cx, size.height), crossPaint);

    // Horizontal line at cursor Y would be confusing with multiple channels,
    // so only draw vertical.

    // Compute per-channel value at cursor X
    final readouts = <_CursorReadout>[];
    for (int ch = 0; ch < chCount && ch < 16; ch++) {
      // Extract all values for this channel
      final values = <double>[];
      for (int i = 0; i < sampleCount; i++) {
        final offset = headerSize + (i * chCount + ch) * 8;
        if (offset + 8 <= sampleData!.length) {
          values.add(byteData.getFloat64(offset, Endian.little));
        }
      }
      if (values.isEmpty) continue;

      // Apply horizontal zoom to get visible range
      final totalSamples = values.length;
      final visibleSamples = (totalSamples / hZoom).clamp(1, totalSamples).toInt();
      final maxStart = totalSamples - visibleSamples;
      final startIdx = (maxStart * hPosition).round().clamp(0, maxStart);
      final endIdx = (startIdx + visibleSamples).clamp(0, totalSamples);
      final visibleValues = values.sublist(startIdx, endIdx);
      if (visibleValues.isEmpty) continue;

      // Map cursor X to sample index
      final fraction = cx / size.width;
      final sampleIdx = (fraction * (visibleValues.length - 1)).round().clamp(0, visibleValues.length - 1);
      final rawValue = visibleValues[sampleIdx];

      readouts.add(_CursorReadout(
        channel: ch,
        value: rawValue,
        color: _channelColors[ch % _channelColors.length],
      ));
    }

    // Draw readout labels at top-left
    double labelY = 4;
    for (final r in readouts) {
      final tp = TextPainter(
        text: TextSpan(
          text: 'CH${r.channel}: ${r.value.toStringAsFixed(4)}',
          style: TextStyle(
            color: r.color,
            fontSize: 10,
            fontFamily: 'monospace',
            backgroundColor: Colors.black87,
          ),
        ),
        textDirection: TextDirection.ltr,
      );
      tp.layout();
      tp.paint(canvas, Offset(4, labelY));
      labelY += tp.height + 2;
    }
  }

  @override
  bool shouldRepaint(WaveformPainter oldDelegate) {
    return sampleData != oldDelegate.sampleData ||
        sampleLen != oldDelegate.sampleLen ||
        hZoom != oldDelegate.hZoom ||
        hPosition != oldDelegate.hPosition ||
        vScales != oldDelegate.vScales ||
        vPositions != oldDelegate.vPositions ||
        vOffsets != oldDelegate.vOffsets ||
        acCoupling != oldDelegate.acCoupling ||
        cursorX != oldDelegate.cursorX ||
        trigLevel != oldDelegate.trigLevel ||
        trigChannel != oldDelegate.trigChannel;
  }
}

class _CursorReadout {
  final int channel;
  final double value;
  final Color color;
  const _CursorReadout({required this.channel, required this.value, required this.color});
}
