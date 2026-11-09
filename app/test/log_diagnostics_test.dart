// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_logs.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';

class LogsCore implements LauncherCore {
  int calls = 0;
  List<Map<String, dynamic>> logs = [
    {
      'name': 'logs/latest.log',
      'text': '[INFO] Starting\njava.lang.OutOfMemoryError: Java heap space\n',
      'truncated': true,
      'diagnosis': {
        'status': 'matched',
        'truncated': true,
        'findings': [
          {
            'code': 'java_heap_exhausted',
            'severity': 'error',
            'title': '日志包含 Java 堆内存不足线索',
            'suggestion': '检查实例内存设置和可用系统内存；适当调整最大内存，并排查大型资源包或模组占用。不要直接分配全部内存。',
            'evidence': [
              {'line': 2, 'text': 'java.lang.OutOfMemoryError: Java heap space'}
            ]
          }
        ]
      }
    },
    {
      'name': '.solar-launch.log',
      'text': '[INFO] Process exited normally\n',
      'diagnosis': {'status': 'unknown', 'findings': []}
    }
  ];
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] != 'engine') return {'busy': false};
    ++calls;
    return {'busy': false, 'result': logs};
  }
}

Future<void> show(WidgetTester tester, LogsCore core) async {
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await tester.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Padding(
              padding: const EdgeInsets.all(16),
              child: InstanceLogs(
                  engine: engine,
                  instance: const {'game_directory': '/test/game'})))));
  await tester.pumpAndSettle();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  testWidgets('analysis is explicit and local; original log remains available',
      (t) async {
    final core = LogsCore();
    await show(t, core);
    expect(find.byType(LogDiagnosisPanel), findsNothing);
    expect(find.byKey(const Key('originalInstanceLog')), findsOneWidget);
    final calls = core.calls;
    await t.tap(find.byKey(const Key('analyzeInstanceLog')));
    await t.pumpAndSettle();
    expect(core.calls, calls);
    expect(find.text('错误线索 · java_heap_exhausted'), findsOneWidget);
    expect(find.textContaining('第 2 行\njava.lang.OutOfMemoryError'),
        findsOneWidget);
    expect(find.textContaining('旧日志不一定属于本次启动'), findsOneWidget);
    expect(find.textContaining('仅分析末尾片段'), findsOneWidget);
    expect(find.byKey(const Key('originalInstanceLog')), findsOneWidget);
    await t.tap(find.text('日志分析'));
    await t.pumpAndSettle();
    expect(find.text('错误线索 · java_heap_exhausted'), findsNothing);
    expect(find.byKey(const Key('originalInstanceLog')), findsOneWidget);
  });
  testWidgets('switching log clears previous findings and unknown is explicit',
      (t) async {
    final core = LogsCore();
    await show(t, core);
    await t.tap(find.byKey(const Key('analyzeInstanceLog')));
    await t.pumpAndSettle();
    await t.tap(find.text('.solar-launch.log'));
    await t.pumpAndSettle();
    expect(find.byType(LogDiagnosisPanel), findsNothing);
    expect(find.text('错误线索 · java_heap_exhausted'), findsNothing);
    await t.tap(find.byKey(const Key('analyzeInstanceLog')));
    await t.pumpAndSettle();
    expect(find.text('未匹配到已知线索'), findsOneWidget);
    expect(find.textContaining('暂时无法判断'), findsOneWidget);
  });
  testWidgets('refreshing changed content invalidates old analysis', (t) async {
    final core = LogsCore();
    await show(t, core);
    await t.tap(find.byKey(const Key('analyzeInstanceLog')));
    await t.pumpAndSettle();
    core.logs = [
      {
        ...core.logs.first,
        'text': 'New log content',
        'diagnosis': {'status': 'unknown', 'findings': []}
      }
    ];
    await t.tap(find.text('刷新'));
    await t.pumpAndSettle();
    expect(find.byType(LogDiagnosisPanel), findsNothing);
    expect(find.text('New log content'), findsOneWidget);
  });
  testWidgets('compact window retains readable analysis and log space',
      (t) async {
    t.view.physicalSize = const Size(680, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    await show(t, LogsCore());
    await t.tap(find.byKey(const Key('analyzeInstanceLog')));
    await t.pumpAndSettle();
    expect(t.getSize(find.byKey(const Key('originalInstanceLog'))).height,
        greaterThan(0));
    expect(t.takeException(), isNull);
  });
  testWidgets('empty logs do not allow analysis', (t) async {
    await show(t, LogsCore()..logs = []);
    expect(
        t
            .widget<OutlinedButton>(find.byKey(const Key('analyzeInstanceLog')))
            .onPressed,
        isNull);
    expect(find.byType(LogDiagnosisPanel), findsNothing);
  });
}
