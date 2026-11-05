// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/resource_updates.dart';

class UpdateCore implements LauncherCore {
  final requests = <Map<String, dynamic>>[];
  Completer<Map<String, dynamic>>? pending;
  bool fail = false;
  String kind = 'mod';
  Map<String, dynamic> get instance => {
        'id': 'Test',
        'version': '1.20.1',
        'loader': 'fabric',
        'game_directory': '/test'
      };
  Map<String, dynamic> get file => {
        'project_id': 'p1',
        'filename': 'new.jar',
        'name': 'Demo',
        'version': '2.0',
        'previous_version': '1.0',
        'root': true,
        'size': 1234,
        'install_disabled': true
      };
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] != 'engine') return {'busy': false};
    final r = Map<String, dynamic>.from(request['request'] as Map);
    requests.add(r);
    if (pending != null) return pending!.future;
    dynamic result;
    switch (r['op']) {
      case 'updates.check':
        if (fail) {
          return {
            'busy': false,
            'error': {
              'code': 'http_503',
              'http_status': 503,
              'message': 'Service unavailable'
            }
          };
        }
        result = {
          'target': instance,
          'kind': kind,
          'channel': r['channel'],
          'items': [
            {
              'filename': 'old.jar.disabled',
              'name': 'Demo with a long resource name',
              'version': '1.0',
              'current_version': '1.0',
              'enabled': false,
              'status': 'update',
              'candidate': file
            },
            {
              'filename': 'custom.jar',
              'name': 'Custom',
              'status': 'unknown',
              'message': '网站未识别此文件，无法确认更新。'
            },
            {
              'filename': 'other.jar',
              'name': 'Other',
              'status': 'failed',
              'message': 'Service unavailable',
              'error': {'code': 'http_429', 'http_status': 429}
            }
          ]
        };
        break;
      case 'updates.plan':
        result = {
          'target': instance,
          'kind': kind,
          'update_kind': kind,
          'files': [file],
          'reuse': [],
          'conflicts': [],
          'edges': []
        };
        break;
      case 'resource.projects':
        result = [];
        break;
      case 'mods.select':
        result = r['plan'];
        break;
      case 'updates.apply':
        result = {
          'installed': ['new.jar.disabled'],
          'warnings': []
        };
        break;
      default:
        result = {};
    }
    return {'busy': false, 'result': result};
  }
}

Future<void> show(WidgetTester t, UpdateCore core) async {
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  t.view.physicalSize = const Size(800, 700);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
  await t.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Padding(
              padding: const EdgeInsets.all(20),
              child: ResourceUpdates(
                  engine: engine,
                  instance: core.instance,
                  kind: core.kind,
                  onBack: () {})))));
  await t.pump();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
        '$language update review fits compact viewport and preserves choice',
        (t) async {
      AppLanguage.instance.select(language);
      final core = UpdateCore();
      await show(t, core);
      await t.pumpAndSettle();
      expect(core.requests.first['channel'], 'release');
      expect(find.text(tr('更新后保持禁用')), findsOneWidget);
      expect(find.textContaining('HTTP 429'), findsOneWidget);
      // Unknown/error items have disabled checkboxes and cannot be selected.
      expect(
          t
              .widgetList<Checkbox>(find.byType(Checkbox))
              .where((w) => w.onChanged == null)
              .length,
          2);
      await t.tap(find.byKey(const Key('reviewUpdates')));
      await t.pumpAndSettle();
      expect(
          core.requests
              .firstWhere((r) => r['op'] == 'updates.plan')['selected'],
          ['old.jar.disabled']);
      expect(find.text(tr('确认更新方案')), findsOneWidget);
      expect(find.descendant(of: find.byType(AlertDialog), matching: find.textContaining('1.0 → 2.0')), findsOneWidget);
      await t.tap(find.byKey(const Key('applyInstall')));
      await t.pumpAndSettle();
      expect(core.requests.where((r) => r['op'] == 'updates.apply').length, 1);
      expect(find.text(tr('更新已完成')), findsOneWidget);
      expect(find.text('✓ new.jar.disabled'), findsOneWidget);
      expect(t.takeException(), isNull);
    });
  }
  testWidgets(
      'loading stays in list and successful retry clears previous error',
      (t) async {
    final core = UpdateCore()..pending = Completer<Map<String, dynamic>>();
    await show(t, core);
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    core.pending!.complete({
      'busy': false,
      'error': {
        'code': 'http_503',
        'message': 'Service unavailable',
        'http_status': 503
      }
    });
    core.pending = null;
    await t.pumpAndSettle();
    expect(find.textContaining('HTTP 503'), findsOneWidget);
    await t.tap(find.text(tr('重新检查')));
    await t.pumpAndSettle();
    expect(find.textContaining('HTTP 503'), findsNothing);
    expect(find.text('Custom'), findsOneWidget);
    expect(core.requests.where((r) => r['op'] == 'updates.check').length, 2);
  });
  testWidgets(
      'cancelling review does not apply; changing channel invalidates old selection',
      (t) async {
    final core = UpdateCore();
    await show(t, core);
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('reviewUpdates')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('取消')));
    await t.pumpAndSettle();
    expect(core.requests.where((r) => r['op'] == 'updates.apply'), isEmpty);
    await t.tap(find.byKey(const Key('updateChannel')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('包含测试版')).last);
    await t.pumpAndSettle();
    expect(core.requests.last['channel'], 'beta');
    expect(core.requests.where((r) => r['op'] == 'updates.check').length, 2);
    expect(t.takeException(), isNull);
  });
  testWidgets('pack updates reuse the confirmation dialog for multiple files',
      (t) async {
    final core = UpdateCore()..kind = 'resourcepack';
    await show(t, core);
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('reviewUpdates')));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('applyInstall')));
    await t.pumpAndSettle();
    expect(core.requests.where((r) => r['op'] == 'mods.select'), isEmpty);
    expect(core.requests.where((r) => r['op'] == 'updates.apply').length, 1);
    expect(find.text(tr('更新已完成')), findsOneWidget);
    expect(t.takeException(), isNull);
  });
}
