// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/version_components.dart';

class ComponentsCore implements LauncherCore {
  Completer<Map<String, dynamic>>? pending;
  bool malformed = false;
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> r) async {
    if (r['action'] != 'engine') return {'busy': false};
    if (pending != null) return pending!.future;
    return {
      'busy': false,
      'result': malformed
          ? []
          : {
              'source': 'bmclapi',
              'warnings': [
                {'http_status': 503}
              ],
              'builds': [
                {
                  'game': '1.20.1',
                  'version': 'HD_U_I6',
                  'forge': '47.2.18',
                  'preview': false
                },
                {
                  'game': '1.20.1',
                  'version': 'HD_U_I7_pre1',
                  'forge': '47.2.18',
                  'preview': true
                }
              ]
            }
    };
  }
}

Future<void> show(WidgetTester t, ComponentsCore core) async {
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  t.view.physicalSize = const Size(800, 850);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
  await t.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Padding(
              padding: const EdgeInsets.all(16),
              child: VersionComponents(
                  engine: engine,
                  instance: const {
                    'id': 'test',
                    'version': '1.20.1',
                    'loader': 'fabric',
                    'loader_version': '0.16.10'
                  },
                  javas: const [],
                  onChanged: () {})))));
  await t.pumpAndSettle();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
        '$language parses OptiFine envelope and keeps channel selection consistent',
        (t) async {
      AppLanguage.instance.select(language);
      await show(t, ComponentsCore());
      await t.tap(find.text(tr('查询构建')).last);
      await t.pumpAndSettle();
      expect(find.text('HD_U_I6'), findsOneWidget);
      expect(find.text(tr('构建来源：{0}', [tr('BMCLAPI 镜像')])), findsOneWidget);
      expect(find.text('invalid_result'), findsNothing);
      await t.tap(find.text(tr('预览版')));
      await t.pumpAndSettle();
      expect(find.text('HD_U_I6'), findsNothing);
      expect(t.widget<Radio<String>>(find.byType(Radio<String>)).groupValue,
          'HD_U_I7_pre1');
      expect(t.takeException(), isNull);
    });
  }
  testWidgets(
      'OptiFine loading and errors remain inside panel; retry clears error',
      (t) async {
    final core = ComponentsCore();
    await show(t, core);
    core.pending = Completer<Map<String, dynamic>>();
    await t.tap(find.text(tr('查询构建')).last);
    await t.pump();
    expect(find.byType(CircularProgressIndicator), findsOneWidget);
    expect(find.text(tr('已安装组件')), findsOneWidget);
    expect(find.text(tr('主加载器')), findsOneWidget);
    core.pending!.complete({
      'busy': false,
      'error': {
        'code': 'list_failed',
        'http_status': 503,
        'message': 'OptiFine 构建查询失败，请检查网络后重试。'
      }
    });
    core.pending = null;
    await t.pumpAndSettle();
    expect(find.textContaining('HTTP 503'), findsOneWidget);
    expect(find.text(tr('主加载器')), findsOneWidget);
    await t.tap(find.text(tr('重试')));
    await t.pumpAndSettle();
    expect(find.textContaining('HTTP 503'), findsNothing);
    expect(find.text('HD_U_I6'), findsOneWidget);
    expect(t.takeException(), isNull);
  });
  testWidgets('malformed response has specific error, not invalid_result',
      (t) async {
    await show(t, ComponentsCore()..malformed = true);
    await t.tap(find.text(tr('查询构建')).last);
    await t.pumpAndSettle();
    expect(find.text('invalid_optifine_response'), findsOneWidget);
    expect(find.text('invalid_result'), findsNothing);
    expect(find.text(tr('已安装组件')), findsOneWidget);
  });
}
