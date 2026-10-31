// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/main.dart';
import 'package:secl/localization.dart';

class TestCore implements LauncherCore {
  final List<Map<String, dynamic>> requests = [];
  Map<String, dynamic> state = {
    'language': 'zh_Hans',
    'root': '/not/installed/minecraft',
    'versions': [],
    'javas': [],
    'account': null,
    'busy': false,
    'client_id': ''
  };
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    requests.add(request);
    if (request['action'] == 'login') {
      state = {
        ...state,
        'error': {
          'stage': 'configuration',
          'code': 'client_id_missing',
          'message': '请先配置 SECL 自己的 Client ID。',
          'http_status': 0
        }
      };
    }
    if (request['action'] == 'clear_error') state = {...state, 'error': null};
    return {...state};
  }
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  testWidgets(
      'no account cannot launch, and button remains in the bottom right',
      (tester) async {
    tester.view.physicalSize = const Size(1080, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final core = TestCore();
    await tester.pumpWidget(SeclApp(core: core));
    await tester.pump(const Duration(milliseconds: 100));
    final launch =
        tester.widget<FilledButton>(find.byKey(const Key('launchButton')));
    expect(launch.onPressed, isNull);
    final p = tester.getBottomRight(find.byKey(const Key('launchButton')));
    expect(p.dx, greaterThan(1000));
    expect(p.dy, greaterThan(650));
    expect(find.text('未登录'), findsOneWidget);
    expect(find.text('离线账号'), findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets('authentication failures appear in a dedicated panel',
      (tester) async {
    tester.view.physicalSize = const Size(1080, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final core = TestCore();
    await tester.pumpWidget(SeclApp(core: core));
    await tester.pump(const Duration(milliseconds: 100));
    await tester.tap(find.byKey(const Key('loginButton')));
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.byKey(const Key('errorPanel')), findsOneWidget);
    expect(find.textContaining('client_id_missing'), findsOneWidget);
    expect(core.requests.where((r) => r['action'] == 'launch'), isEmpty);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets('compact window retains controls without overflow',
      (tester) async {
    tester.view.physicalSize = const Size(880, 640);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(SeclApp(core: TestCore()));
    await tester.pump(const Duration(milliseconds: 100));
    expect(find.byKey(const Key('launchButton')), findsOneWidget);
    expect(find.byKey(const Key('javaSelector')), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
}
