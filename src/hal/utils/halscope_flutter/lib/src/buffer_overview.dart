import 'package:flutter/material.dart';

/// Minimap-style buffer overview bar showing record extent, fill progress,
/// trigger position, and a draggable viewport rectangle.
///
/// Layout (horizontal bar, ~28px tall):
///  ┌─────────────────────────────────────────────────────────────────┐
///  │ ┌──record extent──┐   ▼ trigger   ┌──viewport──┐              │
///  │ ├═══filled════════─┤              │            │              │
///  └─────────────────────────────────────────────────────────────────┘
///
/// - Record extent: thin rectangle showing total record length
/// - Filled: solid portion showing captured samples
/// - Trigger: vertical line at trigger position (pre_trig / rec_len)
/// - Viewport: larger semi-transparent rectangle showing current view
///
/// The viewport can be dragged horizontally to change [hPosition].
class BufferOverview extends StatefulWidget {
  final double hZoom;
  final double hPosition;
  final int samples;
  final int recLen;
  final int preTrig;
  final ValueChanged<double> onPositionChanged;
  final ValueChanged<double> onZoomChanged;

  const BufferOverview({
    super.key,
    required this.hZoom,
    required this.hPosition,
    required this.samples,
    required this.recLen,
    required this.preTrig,
    required this.onPositionChanged,
    required this.onZoomChanged,
  });

  @override
  State<BufferOverview> createState() => _BufferOverviewState();
}

class _BufferOverviewState extends State<BufferOverview> {
  bool _dragging = false;
  double _dragStartPos = 0;
  double _dragStartHPos = 0;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onHorizontalDragStart: _onDragStart,
      onHorizontalDragUpdate: _onDragUpdate,
      onHorizontalDragEnd: (_) => _dragging = false,
      onTapDown: _onTap,
      child: Container(
        height: 28,
        margin: const EdgeInsets.symmetric(horizontal: 4),
        decoration: BoxDecoration(
          color: Colors.grey.shade900,
          border: Border.all(color: Colors.blueGrey.shade700, width: 0.5),
          borderRadius: BorderRadius.circular(3),
        ),
        child: CustomPaint(
          painter: _BufferOverviewPainter(
            hZoom: widget.hZoom,
            hPosition: widget.hPosition,
            samples: widget.samples,
            recLen: widget.recLen,
            preTrig: widget.preTrig,
          ),
          size: Size.infinite,
        ),
      ),
    );
  }

  void _onTap(TapDownDetails details) {
    // Tap anywhere → center viewport there
    final box = context.findRenderObject() as RenderBox;
    final w = box.size.width;
    if (w <= 0) return;
    final pos = (details.localPosition.dx / w).clamp(0.0, 1.0);
    widget.onPositionChanged(pos);
  }

  void _onDragStart(DragStartDetails details) {
    _dragging = true;
    _dragStartPos = details.localPosition.dx;
    _dragStartHPos = widget.hPosition;
  }

  void _onDragUpdate(DragUpdateDetails details) {
    if (!_dragging) return;
    final box = context.findRenderObject() as RenderBox;
    final w = box.size.width;
    if (w <= 0) return;
    final delta = (details.localPosition.dx - _dragStartPos) / w;
    final newPos = (_dragStartHPos + delta).clamp(0.0, 1.0);
    widget.onPositionChanged(newPos);
  }
}

class _BufferOverviewPainter extends CustomPainter {
  final double hZoom;
  final double hPosition;
  final int samples;
  final int recLen;
  final int preTrig;

  _BufferOverviewPainter({
    required this.hZoom,
    required this.hPosition,
    required this.samples,
    required this.recLen,
    required this.preTrig,
  });

  @override
  void paint(Canvas canvas, Size size) {
    if (size.width <= 0 || size.height <= 0) return;

    final w = size.width;
    final h = size.height;
    final midY = h / 2;

    // Record extent box (thin, ±3px from center)
    final recPaint = Paint()
      ..color = Colors.blueGrey.shade600
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.0;

    canvas.drawRect(
      Rect.fromLTRB(0, midY - 3, w, midY + 3),
      recPaint,
    );

    // Filled portion (solid bar within record box)
    if (recLen > 0 && samples > 0) {
      final fillFrac = (samples / recLen).clamp(0.0, 1.0);
      final fillPaint = Paint()
        ..color = Colors.blueGrey.shade400
        ..style = PaintingStyle.fill;
      canvas.drawRect(
        Rect.fromLTRB(0, midY - 2, w * fillFrac, midY + 2),
        fillPaint,
      );
    }

    // Trigger position line (pre_trig samples from start = trigger point)
    if (recLen > 0) {
      final trigX = (preTrig / recLen).clamp(0.0, 1.0) * w;
      final trigPaint = Paint()
        ..color = Colors.red.shade400
        ..strokeWidth = 1.5;
      canvas.drawLine(Offset(trigX, 1), Offset(trigX, h - 1), trigPaint);
    }

    // Viewport rectangle (larger box showing visible portion)
    final viewWidth = (1.0 / hZoom) * w;
    final maxStart = w - viewWidth;
    final viewLeft = hPosition * maxStart;

    final viewPaint = Paint()
      ..color = Colors.white.withOpacity(0.15)
      ..style = PaintingStyle.fill;
    canvas.drawRect(
      Rect.fromLTRB(viewLeft, 1, viewLeft + viewWidth, h - 1),
      viewPaint,
    );

    final viewBorderPaint = Paint()
      ..color = Colors.white.withOpacity(0.6)
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.0;
    canvas.drawRect(
      Rect.fromLTRB(viewLeft, 1, viewLeft + viewWidth, h - 1),
      viewBorderPaint,
    );
  }

  @override
  bool shouldRepaint(covariant _BufferOverviewPainter old) =>
      hZoom != old.hZoom ||
      hPosition != old.hPosition ||
      samples != old.samples ||
      recLen != old.recLen ||
      preTrig != old.preTrig;
}
