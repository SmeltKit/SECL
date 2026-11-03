// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:convert';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/instance_resources.dart';

Future<String> iconPng(Color color) async {
  final recorder = ui.PictureRecorder();
  Canvas(recorder)
      .drawRect(const Rect.fromLTWH(0, 0, 2, 2), Paint()..color = color);
  final picture = recorder.endRecording();
  final image = await picture.toImage(2, 2);
  try {
    final data = await image.toByteData(format: ui.ImageByteFormat.png);
    return base64Encode(data!.buffer.asUint8List());
  } finally {
    image.dispose();
    picture.dispose();
  }
}

void main() {
  testWidgets('polling rebuilds retain icon cache key; new content replaces it',
      (t) async {
    final first = (await t.runAsync(() => iconPng(Colors.red)))!;
    final second = (await t.runAsync(() => iconPng(Colors.blue)))!;
    Future<void> show(String encoded) => t.pumpWidget(MaterialApp(
        home: LocalResourceIcon({'icon_base64': encoded, 'name': 'Test mod'})));
    Future<Object> imageKey() => t
        .widget<Image>(find.byType(Image))
        .image
        .obtainKey(ImageConfiguration.empty);
    await show(first);
    final original = await imageKey();
    for (var i = 0; i < 5; i++) {
      await show(first);
      expect(await imageKey(), original,
          reason: 'Unchanged inventory must reuse its decoded image cache key');
    }
    await show(second);
    final replacement = await imageKey();
    expect(replacement, isNot(original));
    await show(second);
    expect(await imageKey(), replacement);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('empty and malformed local icons use the shared fallback',
      (t) async {
    for (final encoded in ['', 'not a base64 image']) {
      await t.pumpWidget(
          MaterialApp(home: LocalResourceIcon({'icon_base64': encoded})));
      expect(find.byIcon(Icons.inventory_2_outlined), findsOneWidget);
      expect(t.takeException(), isNull);
    }
  });
}
