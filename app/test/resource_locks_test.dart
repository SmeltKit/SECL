// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_resources.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/resource_updates.dart';

class LockCore implements LauncherCore {
  bool pinned = false;
  final requests = <Map<String, dynamic>>[];
  final instance = <String, dynamic>{
    'id': 'demo',
    'version': '1.20.1',
    'loader': 'fabric',
    'game_directory': '/fixture'
  };
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] != 'engine') return {'busy': false};
    final r = Map<String, dynamic>.from(request['request'] as Map);
    requests.add(r);
    dynamic result = {};
    final row = {
      'filename': 'demo.jar',
      'sha512': 'a' * 128,
      'name': 'Demo',
      'version': '1.0',
      'id': 'demo',
      'loader': 'Fabric',
      'known': true,
      'enabled': true,
      'update_locked': pinned
    };
    if (r['op'] == 'instance.inventory') {
      result = {
        'items': [row],
        'checks': {},
        'support': {}
      };
    }
    if (r['op'] == 'updates.lock') {
      pinned = r['locked'] as bool;
      result = {'saved': true};
    }
    if (r['op'] == 'updates.check') {
      result = {
        'items': [
          {...row, 'status': 'locked', 'message': '此文件已锁定版本，检查更新时已跳过。'}
        ]
      };
    }
    return {'busy': false, 'result': result};
  }
}

void main() {
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language pin and unpin use the real file snapshot',
        (t) async {
      AppLanguage.instance.select(language);
      t.view.physicalSize = const Size(900, 760);
      t.view.devicePixelRatio = 1;
      addTearDown(t.view.resetPhysicalSize);
      addTearDown(t.view.resetDevicePixelRatio);
      final core = LockCore();
      final active = ResourceEngine(core);
      addTearDown(active.dispose);
      await t.pumpWidget(MaterialApp(
          home: Scaffold(
              body: InstanceResources(
                  engine: active,
                  instance: core.instance,
                  kind: 'mod',
                  onBrowse: (_, {query}) {}))));
      await t.pumpAndSettle();
      Future<void> menu() async {
        await t.tap(find.byKey(const ValueKey('resource-menu-demo.jar')));
        await t.pumpAndSettle();
      }

      await menu();
      await t.tap(find.text(tr('锁定当前版本')));
      await t.pumpAndSettle();
      expect(find.text(tr('版本已锁定')), findsOneWidget);
      final request =
          core.requests.firstWhere((r) => r['op'] == 'updates.lock');
      expect(request['instance'], core.instance);
      expect(request['kind'], 'mod');
      expect((request['files'] as List).single['sha512'], 'a' * 128);
      expect(request['locked'], true);
      await menu();
      await t.tap(find.text(tr('解锁版本')));
      await t.pumpAndSettle();
      expect(core.pinned, false);
      expect(find.text(tr('版本已锁定')), findsNothing);
      expect(t.takeException(), isNull);
    });
  }
  testWidgets('locked update cannot be selected or installed', (t) async {
    AppLanguage.instance.select('zh_Hans');
    final core = LockCore()..pinned = true;
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: ResourceUpdates(
                engine: engine,
                instance: core.instance,
                kind: 'mod',
                onBack: () {}))));
    await t.pumpAndSettle();
    expect(find.text(tr('版本已锁定')), findsOneWidget);
    expect(
        t
            .widget<FilledButton>(find.byKey(const Key('reviewUpdates')))
            .onPressed,
        isNull);
    expect(core.requests.where((r) => r['op'] == 'updates.plan'), isEmpty);
    expect(t.takeException(), isNull);
  });
}
