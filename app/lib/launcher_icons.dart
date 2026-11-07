// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:flutter/material.dart';

/// Navigation symbols share a 24-unit grid and one rounded outline weight.
/// Keeping these as paths also avoids platform-dependent icon font rendering.
enum LauncherIconKind {
  home,
  library,
  mod,
  shader,
  resourcepack,
  download,
  java,
  settings,
  refresh,
  close,
  chevronDown,
}

class LauncherIcon extends StatelessWidget {
  const LauncherIcon(
    this.kind, {
    super.key,
    this.size = 22,
    this.color,
    this.semanticLabel,
  });

  final LauncherIconKind kind;
  final double size;
  final Color? color;
  final String? semanticLabel;

  @override
  Widget build(BuildContext context) {
    final icon = SizedBox.square(
      dimension: size,
      child: CustomPaint(
        painter: _LauncherIconPainter(
          kind,
          color ?? IconTheme.of(context).color ?? const Color(0xff243347),
        ),
      ),
    );
    return semanticLabel == null
        ? icon
        : Semantics(label: semanticLabel, image: true, child: icon);
  }
}

class _LauncherIconPainter extends CustomPainter {
  const _LauncherIconPainter(this.kind, this.color);

  final LauncherIconKind kind;
  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.save();
    canvas.scale(size.width / 24, size.height / 24);
    final pen = Paint()
      ..color = color
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.7
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..isAntiAlias = true;

    void path(Path shape) => canvas.drawPath(shape, pen);
    void line(double x1, double y1, double x2, double y2) =>
        canvas.drawLine(Offset(x1, y1), Offset(x2, y2), pen);

    switch (kind) {
      case LauncherIconKind.home:
        path(Path()
          ..moveTo(3, 10.3)
          ..lineTo(12, 3.3)
          ..lineTo(21, 10.3));
        path(Path()
          ..moveTo(5.2, 8.8)
          ..lineTo(5.2, 20.5)
          ..lineTo(9.7, 20.5)
          ..lineTo(9.7, 14)
          ..lineTo(14.3, 14)
          ..lineTo(14.3, 20.5)
          ..lineTo(18.8, 20.5)
          ..lineTo(18.8, 8.8));
      case LauncherIconKind.library:
        for (final x in [3.7, 13.7]) {
          for (final y in [3.7, 13.7]) {
            canvas.drawRRect(
                RRect.fromRectAndRadius(
                    Rect.fromLTWH(x, y, 6.6, 6.6), const Radius.circular(1.2)),
                pen);
          }
        }
      case LauncherIconKind.mod:
        path(Path()
          ..moveTo(12, 3.2)
          ..lineTo(20.5, 8.1)
          ..lineTo(20.5, 15.9)
          ..lineTo(12, 20.8)
          ..lineTo(3.5, 15.9)
          ..lineTo(3.5, 8.1)
          ..close());
        path(Path()
          ..moveTo(3.5, 8.1)
          ..lineTo(12, 13)
          ..lineTo(20.5, 8.1));
        line(12, 13, 12, 20.8);
        line(7.8, 5.6, 16.3, 10.5);
      case LauncherIconKind.shader:
        canvas.drawCircle(const Offset(12, 10), 4, pen);
        for (var i = 0; i < 8; i++) {
          final angle = i * math.pi / 4;
          line(12 + math.cos(angle) * 6.1, 10 + math.sin(angle) * 6.1,
              12 + math.cos(angle) * 7.9, 10 + math.sin(angle) * 7.9);
        }
        line(5, 21, 19, 21);
      case LauncherIconKind.resourcepack:
        path(Path()
          ..moveTo(12, 3.2)
          ..lineTo(20.8, 8)
          ..lineTo(12, 12.8)
          ..lineTo(3.2, 8)
          ..close());
        for (final y in [12.1, 16.2]) {
          path(Path()
            ..moveTo(3.2, y)
            ..lineTo(12, y + 4.8)
            ..lineTo(20.8, y));
        }
      case LauncherIconKind.download:
        line(12, 3.2, 12, 14.3);
        path(Path()
          ..moveTo(7.6, 9.9)
          ..lineTo(12, 14.3)
          ..lineTo(16.4, 9.9));
        path(Path()
          ..moveTo(3.6, 15.2)
          ..lineTo(3.6, 19.5)
          ..quadraticBezierTo(3.6, 20.5, 4.6, 20.5)
          ..lineTo(19.4, 20.5)
          ..quadraticBezierTo(20.4, 20.5, 20.4, 19.5)
          ..lineTo(20.4, 15.2));
      case LauncherIconKind.java:
        path(Path()
          ..moveTo(4.3, 8.7)
          ..lineTo(16.3, 8.7)
          ..lineTo(16.3, 15.4)
          ..quadraticBezierTo(16.3, 18.5, 13.2, 18.5)
          ..lineTo(7.4, 18.5)
          ..quadraticBezierTo(4.3, 18.5, 4.3, 15.4)
          ..close());
        path(Path()
          ..moveTo(16.3, 9.4)
          ..lineTo(18.4, 9.4)
          ..cubicTo(22.2, 9.4, 22.2, 14.5, 18.4, 14.5)
          ..lineTo(16.3, 14.5));
        for (final x in [8.1, 12.8]) {
          path(Path()
            ..moveTo(x, 6)
            ..cubicTo(x - 1.8, 4.4, x + 1.8, 4.1, x, 2.5));
        }
        line(3.3, 21.2, 19.5, 21.2);
      case LauncherIconKind.settings:
        final gear = Path();
        for (var i = 0; i < 32; i++) {
          final angle = -math.pi / 2 + i * math.pi / 16;
          final radius = i % 4 == 1 || i % 4 == 2 ? 9.0 : 7.3;
          final x = 12 + math.cos(angle) * radius;
          final y = 12 + math.sin(angle) * radius;
          if (i == 0) {
            gear.moveTo(x, y);
          } else {
            gear.lineTo(x, y);
          }
        }
        gear.close();
        path(gear);
        canvas.drawCircle(const Offset(12, 12), 3, pen);
      case LauncherIconKind.refresh:
        path(Path()
          ..moveTo(19.7, 9.4)
          ..cubicTo(18.5, 5.1, 14.3, 3.1, 10.1, 4.2)
          ..cubicTo(6.0, 5.2, 3.6, 9.1, 4.3, 13.3)
          ..cubicTo(5.0, 17.6, 9.0, 20.5, 13.2, 19.8)
          ..cubicTo(16.3, 19.3, 18.6, 17.4, 19.5, 14.7));
        path(Path()
          ..moveTo(15, 9.4)
          ..lineTo(20, 9.4)
          ..lineTo(20, 4.4));
      case LauncherIconKind.close:
        line(6, 6, 18, 18);
        line(18, 6, 6, 18);
      case LauncherIconKind.chevronDown:
        path(Path()
          ..moveTo(6.5, 9.3)
          ..lineTo(12, 14.8)
          ..lineTo(17.5, 9.3));
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(_LauncherIconPainter oldDelegate) =>
      oldDelegate.kind != kind || oldDelegate.color != color;
}
