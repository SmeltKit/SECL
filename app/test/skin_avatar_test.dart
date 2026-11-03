// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/skin_avatar.dart';
import 'package:secl/main.dart';
import 'widget_test.dart' show TestCore;

final skinUrl = 'https://textures.minecraft.net/texture/${'a' * 64}';
Future<ui.Image> skin() async {
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder);
  canvas.drawRect(
      const Rect.fromLTWH(0, 16, 64, 48), Paint()..color = Colors.pink);
  canvas.drawRect(const Rect.fromLTWH(8, 8, 8, 8),
      Paint()..color = const Color(0xFF00FF00));
  canvas.drawRect(const Rect.fromLTWH(40, 8, 1, 8),
      Paint()..color = const Color(0xFF0000FF));
  final picture = recorder.endRecording();
  try {
    return await picture.toImage(64, 64);
  } finally {
    picture.dispose();
  }
}

Future<Uint8List> skinPng() async {
  final image = await skin();
  try {
    return (await image.toByteData(format: ui.ImageByteFormat.png))!
        .buffer
        .asUint8List();
  } finally {
    image.dispose();
  }
}

Future<void> flushImage(WidgetTester t) async {
  await t
      .runAsync(() => Future<void>.delayed(const Duration(milliseconds: 80)));
  await t.pump();
}

Finder head() => find
    .byWidgetPredicate((w) => w is CustomPaint && w.painter is SkinHeadPainter);

void main() {
  test('only canonical official HTTPS skin URLs can be fetched', () {
    expect(isOfficialSkinUrl(skinUrl), isTrue);
    for (final value in [
      'http://textures.minecraft.net/texture/${'a' * 64}',
      'https://textures.minecraft.net.evil.example/texture/${'a' * 64}',
      '$skinUrl?access_token=secret',
      'https://example.org/head.png'
    ]) {
      expect(isOfficialSkinUrl(value), isFalse);
    }
  });
  test('hat uses a 9-unit square around the centered 8-unit face', () async {
    final texture = await skin();
    final recorder = ui.PictureRecorder();
    SkinHeadPainter(texture).paint(Canvas(recorder), const Size(90, 90));
    final picture = recorder.endRecording();
    final image = await picture.toImage(90, 90);
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!;
    int pixel(int x, int y) => bytes.getUint32((y * 90 + x) * 4);
    expect(pixel(0, 0), 0x0000FFFF); // Hat overlay reaches square corner.
    expect(pixel(10, 40), 0x0000FFFF);
    expect(pixel(12, 40), 0x00FF00FF); // Sharp edge, not a smoothed blend.
    expect(pixel(45, 4), 0x00000000); // Face starts 0.5 model units inward.
    expect(pixel(45, 5), 0x00FF00FF);
    expect(pixel(84, 84), 0x00FF00FF);
    expect(pixel(85, 84), 0x00000000);
    expect(pixel(89, 89), 0x00000000); // No stretching of the face to the hat.
    image.dispose();
    picture.dispose();
    texture.dispose();
  });
  test('legacy skins keep translucent outer pixels and square head geometry',
      () async {
    final source = ui.PictureRecorder();
    final canvas = Canvas(source);
    canvas.drawRect(const Rect.fromLTWH(8, 8, 8, 8),
        Paint()..color = const Color(0xFFFF0000));
    canvas.drawRect(const Rect.fromLTWH(40, 8, 8, 8),
        Paint()..color = const Color(0x800000FF));
    final sourcePicture = source.endRecording();
    final texture = await sourcePicture.toImage(64, 32);
    final recorder = ui.PictureRecorder();
    SkinHeadPainter(texture).paint(Canvas(recorder), const Size(108, 90));
    final picture = recorder.endRecording();
    final image = await picture.toImage(108, 90);
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.rawRgba))!;
    int pixel(int x, int y) => bytes.getUint32((y * 108 + x) * 4);
    expect(pixel(8, 45), 0x00000000); // Centered 90 x 90 head, not distorted.
    // rawRgba uses premultiplied channels for translucent pixels.
    expect(pixel(9, 0), 0x00008080); // Outer edge preserves transparency.
    expect(pixel(13, 45), 0x00008080);
    expect(pixel(14, 45), 0x7F0080FF); // Alpha composites over the inset face.
    expect(pixel(99, 45), 0x00000000);
    image.dispose();
    picture.dispose();
    texture.dispose();
    sourcePicture.dispose();
  });
  testWidgets('resolved skin replaces placeholder at the existing avatar size',
      (t) async {
    final bytes = await t.runAsync(skinPng);
    await t.pumpWidget(MaterialApp(
        home: SkinAvatar(skinUrl: skinUrl, loader: (_) async => bytes!)));
    await flushImage(t);
    expect(head(), findsOneWidget);
    expect(find.byIcon(Icons.person_outline_rounded), findsNothing);
    expect(find.byType(ClipRRect), findsNothing);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'late texture cannot reappear after logout and failed texture stays harmless',
      (t) async {
    final bytes = await t.runAsync(skinPng);
    final deferred = Completer<Uint8List>();
    Future<Uint8List> loader(Uri _) => deferred.future;
    await t.pumpWidget(
        MaterialApp(home: SkinAvatar(skinUrl: skinUrl, loader: loader)));
    await t.pumpWidget(
        MaterialApp(home: SkinAvatar(skinUrl: null, loader: loader)));
    deferred.complete(bytes!);
    await flushImage(t);
    expect(head(), findsNothing);
    expect(find.byIcon(Icons.person_outline_rounded), findsOneWidget);
    await t.pumpWidget(MaterialApp(
        home: SkinAvatar(
            skinUrl: skinUrl,
            loader: (_) async =>
                throw const FormatException('test CDN failure'))));
    await flushImage(t);
    expect(head(), findsNothing);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'failed skin is retried only on explicit click and then replaces fallback',
      (t) async {
    final bytes = await t.runAsync(skinPng);
    var attempts = 0;
    Future<Uint8List> loader(Uri _) async {
      attempts++;
      if (attempts == 1) throw const FormatException('temporary CDN failure');
      return bytes!;
    }

    await t.pumpWidget(
        MaterialApp(home: SkinAvatar(skinUrl: skinUrl, loader: loader)));
    await flushImage(t);
    expect(attempts, 1);
    expect(find.byIcon(Icons.refresh_rounded), findsOneWidget);
    await t.pump(const Duration(seconds: 20));
    expect(attempts, 1);
    await t.tap(find.byIcon(Icons.refresh_rounded));
    await flushImage(t);
    expect(attempts, 2);
    expect(head(), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'signed-in profile reaches avatar without changing ownership or launch gates',
      (t) async {
    final core = TestCore();
    core.state['account'] = {
      'name': 'VerifiedPlayer',
      'owned': true,
      'skin_url': ''
    };
    t.view.physicalSize = const Size(1080, 720);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    await t.pumpWidget(SeclApp(core: core));
    await t.pump(const Duration(milliseconds: 100));
    expect(find.text('VerifiedPlayer'), findsOneWidget);
    expect(find.text('Java 版资格已验证'), findsOneWidget);
    expect(find.byType(SkinAvatar), findsOneWidget);
    expect(
        t.widget<FilledButton>(find.byKey(const Key('launchButton'))).onPressed,
        isNull);
    expect(find.byKey(const Key('errorPanel')), findsNothing);
    await t.pumpWidget(const SizedBox());
  });
}
