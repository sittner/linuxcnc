import 'dart:typed_data';
import 'dart:ui';

import 'package:flutter/material.dart';

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

  WaveformPainter({this.sampleData, required this.sampleLen});

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

    // Auto-scale: find min/max
    double vMin = values[0];
    double vMax = values[0];
    for (final v in values) {
      if (v < vMin) vMin = v;
      if (v > vMax) vMax = v;
    }

    // Add 5% margin
    final range = vMax - vMin;
    if (range < 1e-15) {
      vMin -= 1;
      vMax += 1;
    } else {
      vMin -= range * 0.05;
      vMax += range * 0.05;
    }

    // Build path
    final path = Path();
    for (int i = 0; i < values.length; i++) {
      final x = size.width * i / (values.length - 1).clamp(1, values.length);
      final y = size.height * (1.0 - (values[i] - vMin) / (vMax - vMin));

      if (i == 0) {
        path.moveTo(x, y);
      } else {
        path.lineTo(x, y);
      }
    }

    canvas.drawPath(path, paint);
  }

  @override
  bool shouldRepaint(WaveformPainter oldDelegate) {
    return sampleData != oldDelegate.sampleData ||
        sampleLen != oldDelegate.sampleLen;
  }
}
