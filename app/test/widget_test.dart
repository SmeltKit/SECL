// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
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

class PendingLibraryCore extends TestCore {
  final library = Completer<void>();

  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] == 'engine' &&
        (request['request'] as Map)['op'] == 'instances.scan') {
      requests.add(request);
      await library.future;
      return {
        ...state,
        'result': {'instances': []}
      };
    }
    if (request['action'] == 'login') {
      requests.add(request);
      state = {...state, 'busy': true, 'stage': 'browser'};
      return {...state};
    }
    return super.call(request);
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
  testWidgets('download activity lives in the global top right on every page',
      (tester) async {
    tester.view.physicalSize = const Size(1080, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(SeclApp(core: TestCore()));
    await tester.pumpAndSettle();
    final button = find.byKey(const Key('downloadTasksButton'));
    expect(tester.getTopRight(button).dx, greaterThan(1020));
    expect(tester.getTopRight(button).dy, inInclusiveRange(40, 48));
    await tester.tap(button);
    await tester.pumpAndSettle();
    expect(find.text('还没有下载任务'), findsOneWidget);
    await tester.tap(find.byTooltip('关闭').last);
    await tester.pumpAndSettle();
    for (final route in ['java', 'library', 'download']) {
      await tester.tap(find.byKey(Key('nav-$route')));
      await tester.pumpAndSettle();
      expect(button, findsOneWidget);
      expect(tester.getTopRight(button).dy, inInclusiveRange(40, 48));
    }
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
  testWidgets('sidebar actions stay stable and wait for their own turn',
      (tester) async {
    tester.view.physicalSize = const Size(1080, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final core = PendingLibraryCore();
    await tester.pumpWidget(SeclApp(core: core));
    await tester.pump(const Duration(milliseconds: 100));

    final login = find.byKey(const Key('loginButton'));
    final settings = find.byKey(const Key('settingsButton'));
    final refresh = find.byWidgetPredicate(
        (widget) => widget is IconButton && widget.tooltip == '刷新本地版本');
    Iterable<Map<String, dynamic>> actions(String action) =>
        core.requests.where((request) => request['action'] == action);

    await tester.tap(find.byKey(const Key('nav-library')));
    await tester.pump();
    expect(actions('engine'), hasLength(1));
    await tester.pump();
    expect(tester.widget<OutlinedButton>(login).onPressed, isNotNull);
    expect(tester.widget<IconButton>(refresh).onPressed, isNotNull);
    expect(tester.widget<IconButton>(settings).onPressed, isNotNull);
    expect(actions('login'), isEmpty);
    expect(actions('scan'), isEmpty);

    await tester.tap(settings);
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.byKey(const Key('languageSelector')), findsOneWidget);
    await tester.tap(find.widgetWithText(TextButton, '完成'));
    await tester.pump(const Duration(milliseconds: 300));

    await tester.tap(find.byKey(const Key('nav-home')));
    await tester.pump();
    expect(find.byKey(const Key('launchButton')), findsOneWidget);
    await tester.tap(login);
    await tester.pump();
    expect(actions('login'), isEmpty);
    expect(tester.widget<OutlinedButton>(login).onPressed, isNull);
    expect(tester.widget<IconButton>(refresh).onPressed, isNotNull);
    expect(tester.widget<IconButton>(settings).onPressed, isNotNull);

    core.library.complete();
    await tester.pump(const Duration(milliseconds: 200));
    expect(actions('login'), hasLength(1));
    await tester.ensureVisible(refresh);
    await tester.pump();
    await tester.tap(refresh);
    await tester.pump();
    expect(actions('scan'), isEmpty);

    core.state = {...core.state, 'busy': false, 'stage': 'ready'};
    await tester.pump(const Duration(milliseconds: 700));
    await tester.pump();
    expect(actions('scan'), hasLength(1));
    expect(actions('cancel'), isEmpty);
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
  testWidgets(
      'settings use the content area and retain navigation at minimum size',
      (tester) async {
    tester.view.physicalSize = const Size(880, 640);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(SeclApp(core: TestCore()));
    await tester.pump(const Duration(milliseconds: 100));
    await tester.tap(find.byKey(const Key('settingsButton')));
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.byKey(const Key('settingsPage')), findsOneWidget);
    expect(find.byType(AlertDialog), findsNothing);
    expect(find.byKey(const Key('nav-home')), findsOneWidget);
    expect(find.byKey(const Key('appearance-system')), findsOneWidget);
    expect(find.byKey(const Key('tone-blue')), findsOneWidget);
    expect(find.byKey(const Key('languageSelector')), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.tap(find.widgetWithText(TextButton, '完成'));
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.byKey(const Key('launchButton')), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
  testWidgets(
      'ordinary settings no longer expose Microsoft application configuration',
      (tester) async {
    tester.view.physicalSize = const Size(1080, 720);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    await tester.pumpWidget(SeclApp(core: TestCore()));
    await tester.pump(const Duration(milliseconds: 100));
    await tester.tap(find.byKey(const Key('settingsButton')));
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.byKey(const Key('languageSelector')), findsOneWidget);
    expect(find.byKey(const Key('appConfiguration')), findsNothing);
    expect(find.byKey(const Key('clientIdField')), findsNothing);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
}
