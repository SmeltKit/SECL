// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_worlds.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';

const instance = {
  'id': 'test',
  'root': '/test/root',
  'game_directory': '/test/game',
  'version': '1.20.1',
  'loader': 'fabric'
};

class WorldsCore implements LauncherCore {
  final requests = <Map<String, dynamic>>[];
  bool fail = false;
  List<Map<String, dynamic>> worlds = [
    {
      'folder': 'My world',
      'name': 'A long world name for layout testing',
      'version': '1.20.1',
      'mode': 0,
      'last_played': 1793700000000,
      'size': 1048576,
      'problem': ''
    }
  ];
  Completer<Map<String, dynamic>>? pending;
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> r) async {
    if (r['action'] != 'engine') return {'busy': false};
    final request = Map<String, dynamic>.from(r['request'] as Map);
    requests.add(request);
    if (pending != null) return pending!.future;
    if (fail) {
      return {
        'busy': false,
        'error': {'code': 'world_read', 'message': '存档文件读取失败。'}
      };
    }
    dynamic result;
    switch (request['op']) {
      case 'worlds.list':
        result = worlds;
        break;
      case 'worlds.backups':
        result = [
          {
            'archive': 'snapshot.zip',
            'world': 'My world',
            'name': 'World backup',
            'created': 1793700000,
            'size': 4096
          }
        ];
        break;
      case 'instances.scan':
        result = {
          'instances': [
            {
              'id': 'other',
              'name': 'Other instance',
              'root': '/test/root',
              'game_directory': '/test/other',
              'version': '1.21',
              'loader': 'neoforge',
              'loader_version': '21.0'
            }
          ]
        };
        break;
      default:
        result = {'saved': true};
    }
    return {'busy': false, 'result': result};
  }
}

Future<void> show(WidgetTester t, WorldsCore core) async {
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await t.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Padding(
              padding: const EdgeInsets.all(20),
              child: InstanceWorlds(engine: engine, instance: instance)))));
  await t.pump();
}

void viewport(WidgetTester t) {
  t.view.physicalSize = const Size(680, 640);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
}

void main() {
  test('world sizes retain nonzero small values and readable large units', () {
    expect(worldSize(120), '120 B');
    expect(worldSize(2147483648), '2.0 GiB');
  });
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
        '$language world list, actions and name dialog fit compact window',
        (t) async {
      viewport(t);
      AppLanguage.instance.select(language);
      final core = WorldsCore();
      await show(t, core);
      await t.pumpAndSettle();
      expect(find.text('A long world name for layout testing'), findsOneWidget);
      await t.tap(find.byType(Checkbox).first);
      await t.pumpAndSettle();
      await t.tap(find.text(tr('复制存档')));
      await t.pumpAndSettle();
      expect(find.byType(WorldNameDialog), findsOneWidget);
      await t.tap(find.text(tr('取消')));
      await t.pumpAndSettle();
      expect(core.requests.where((r) => r['op'] == 'worlds.copy'), isEmpty);
      expect(t.takeException(), isNull);
    });
  }
  testWidgets('loading occupies the list; explicit retry replaces stale error',
      (t) async {
    viewport(t);
    final core = WorldsCore()..pending = Completer<Map<String, dynamic>>();
    await show(t, core);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    core.pending!.complete({
      'busy': false,
      'error': {'code': 'world_read', 'message': '存档文件读取失败。'}
    });
    core.pending = null;
    await t.pumpAndSettle();
    expect(find.text('world_read'), findsOneWidget);
    await t.tap(find.text(tr('重试')));
    await t.pumpAndSettle();
    expect(find.text('world_read'), findsNothing);
    expect(find.text('My world'), findsOneWidget);
  });
  testWidgets('restore creates a named copy; never sends an overwrite flag',
      (t) async {
    viewport(t);
    final core = WorldsCore();
    await show(t, core);
    await t.pumpAndSettle();
    await t.tap(find.text(tr('备份记录')));
    await t.pumpAndSettle();
    await t.tap(find.text('World backup'));
    await t.pumpAndSettle();
    expect(find.byType(WorldNameDialog), findsOneWidget);
    expect(find.widgetWithText(TextField, 'My world-restored'), findsOneWidget);
    await t.tap(find.text(tr('确认')));
    await t.pumpAndSettle();
    final request =
        core.requests.firstWhere((r) => r['op'] == 'worlds.restore');
    expect(request['archive'], 'snapshot.zip');
    expect(request['name'], 'My world-restored');
    expect(request.containsKey('replace'), isFalse);
    expect(request['instance'], instance);
    expect(t.takeException(), isNull);
  });
  testWidgets(
      'cross-instance copy shows compatibility warning and passes chosen target',
      (t) async {
    viewport(t);
    final core = WorldsCore();
    await show(t, core);
    await t.pumpAndSettle();
    await t.tap(find.byType(Checkbox).first);
    await t.pumpAndSettle();
    await t.tap(find.text(tr('复制到其他实例')));
    await t.pumpAndSettle();
    await t.tap(find.text('Other instance'));
    await t.pumpAndSettle();
    expect(find.textContaining('降级可能损坏世界'), findsOneWidget);
    await t.tap(find.text(tr('确认')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('确认')));
    await t.pumpAndSettle();
    final request = core.requests.firstWhere((r) => r['op'] == 'worlds.copy');
    expect(
        (request['target_instance'] as Map)['game_directory'], '/test/other');
    expect(request['folder'], 'My world');
    expect(request['name'], 'My world-copy');
    expect(t.takeException(), isNull);
  });
}
