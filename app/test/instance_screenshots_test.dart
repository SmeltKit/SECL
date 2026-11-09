// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:convert';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_screenshots.dart';
import 'package:secl/localization.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/resource_engine.dart';

class ScreenshotsCore implements LauncherCore {
  ScreenshotsCore(this.rows);
  final List<Data> rows;
  final requests = <Data>[];
  bool changed = false;
  @override
  Future<Data> call(Data request) async {
    if (request['action'] != 'engine') return {'busy': false};
    final command = Map<String, dynamic>.from(request['request'] as Map);
    requests.add(command);
    dynamic result;
    switch (command['op']) {
      case 'screenshots.list':
        result = {
          'items': rows,
          'total': rows.length,
          'has_more': false,
          'skipped': 1,
          'scan_limited': false
        };
        break;
      case 'screenshots.inspect':
        if (changed) {
          return {
            'busy': false,
            'error': {'code': 'file_changed', 'message': '截图已被外部修改，请刷新列表后重试。'}
          };
        }
        result = rows.firstWhere(
            (row) => row['filename'] == command['file']['filename']);
        break;
      case 'screenshots.remove':
        rows.removeWhere(
            (row) => row['filename'] == command['file']['filename']);
        result = {'removed': true};
        break;
      default:
        result = {'opened': true};
    }
    return {'busy': false, 'result': result};
  }
}

void main() {
  late Directory temporary;
  late List<Data> rows;
  setUp(() async {
    AppLanguage.instance.select('zh_Hans');
    temporary =
        await Directory.systemTemp.createTemp('secl-screenshot-widget-');
    final bytes = base64Decode(
        'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=');
    rows = [];
    for (final name in ['first.png', 'second.png']) {
      final file = await File('${temporary.path}/$name').writeAsBytes(bytes);
      rows.add({
        'filename': name,
        'path': file.path,
        'width': 1,
        'height': 1,
        'size': bytes.length,
        'format': 'PNG',
        'modified': 1793700000,
        'snapshot': 'snapshot-$name'
      });
    }
  });
  tearDown(() async {
    AppLanguage.instance.select('zh_Hans');
    await temporary.delete(recursive: true);
  });
  Future<void> show(WidgetTester t, ScreenshotsCore core) async {
    t.view.physicalSize = const Size(880, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Padding(
                padding: const EdgeInsets.all(20),
                child: SizedBox(
                    width: 656,
                    child: InstanceScreenshots(engine: engine, instance: {
                      'id': 'test',
                      'game_directory': temporary.path
                    }))))));
    await t.pumpAndSettle();
  }

  testWidgets('gallery previews real files and switches with keyboard',
      (t) async {
    final core = ScreenshotsCore(rows);
    await show(t, core);
    expect(find.byKey(const Key('screenshotGallery')), findsOneWidget);
    await t.tap(find.byKey(const ValueKey('screenshot-first.png')));
    await t.pumpAndSettle();
    expect(find.byType(ScreenshotPreview), findsOneWidget);
    expect(core.requests.last['file'], screenshotChoice(rows.first));
    await t.sendKeyEvent(LogicalKeyboardKey.arrowRight);
    await t.pumpAndSettle();
    expect(core.requests.last['file'], screenshotChoice(rows.last));
    expect(
        find.descendant(
            of: find.byType(ScreenshotPreview),
            matching: find.text('second.png')),
        findsOneWidget);
    await t.sendKeyEvent(LogicalKeyboardKey.arrowLeft);
    await t.pumpAndSettle();
    expect(core.requests.last['file'], screenshotChoice(rows.first));
    await t.sendKeyEvent(LogicalKeyboardKey.escape);
    await t.pumpAndSettle();
    expect(find.byType(ScreenshotPreview), findsNothing);
    expect(t.takeException(), isNull);
  });
  testWidgets('trash requires confirmation and sends the selected snapshot',
      (t) async {
    final core = ScreenshotsCore(rows);
    await show(t, core);
    Future<void> trash() async {
      await t.tap(find.byKey(const ValueKey('screenshot-menu-first.png')));
      await t.pumpAndSettle();
      await t.tap(find.text(tr('移到废纸篓')));
      await t.pumpAndSettle();
    }

    await trash();
    expect(find.text(tr('移除截图')), findsOneWidget);
    await t.tap(find.text(tr('取消')));
    await t.pumpAndSettle();
    expect(
        core.requests.where((r) => r['op'] == 'screenshots.remove'), isEmpty);
    final expected = screenshotChoice(rows.first);
    await trash();
    await t.tap(find.text(tr('确认')));
    await t.pumpAndSettle();
    final operation = core.requests
        .singleWhere((request) => request['op'] == 'screenshots.remove');
    expect(operation['file'], expected);
    expect(operation['confirmed'], isTrue);
    expect(find.byKey(const ValueKey('screenshot-first.png')), findsNothing);
    expect(find.byKey(const ValueKey('screenshot-second.png')), findsOneWidget);
    expect(t.takeException(), isNull);
  });
  testWidgets('changed file cannot be opened from a stale preview', (t) async {
    final core = ScreenshotsCore(rows)..changed = true;
    await show(t, core);
    await t.tap(find.byKey(const ValueKey('screenshot-first.png')));
    await t.pumpAndSettle();
    expect(find.text('file_changed'), findsOneWidget);
    expect(core.requests.where((r) => r['op'] == 'screenshots.open'), isEmpty);
    expect(t.takeException(), isNull);
  });
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language gallery and preview fit compact content', (t) async {
      AppLanguage.instance.select(language);
      const longName =
          '2026-11-07_a_very_long_screenshot_filename_in_the_game.png';
      rows.first['filename'] = longName;
      await show(t, ScreenshotsCore(rows));
      expect(t.takeException(), isNull);
      await t.tap(find.byKey(const ValueKey('screenshot-$longName')));
      await t.pumpAndSettle();
      expect(find.byType(ScreenshotPreview), findsOneWidget);
      expect(t.takeException(), isNull);
      await t.sendKeyEvent(LogicalKeyboardKey.escape);
      await t.pumpAndSettle();
    });
  }
  testWidgets('empty gallery keeps its local-only explanation', (t) async {
    await show(t, ScreenshotsCore([]));
    expect(find.text(tr('尚无可显示的截图')), findsOneWidget);
    expect(t.takeException(), isNull);
  });
  test('size labels preserve small nonzero images', () {
    expect(screenshotSize(90), '90 B');
    expect(screenshotSize(2048), '2.0 KiB');
    expect(screenshotSize(2097152), '2.0 MiB');
  });
}
