// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/app_widgets.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/download_center.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';

class DownloadCore implements LauncherCore {
  final requests = <Map<String, dynamic>>[];
  bool waitForInstall = false,
      failQuery = false,
      failReleases = false,
      busy = false;
  List<int> releases = [8, 11, 16, 17, 21, 25, 26, 27];
  dynamic result;
  Map<String, dynamic>? error;
  static final runtime = {
    'version': '17.0.17',
    'major': 17,
    'vendor': 'Azul Systems, Inc.',
    'arch': 'aarch64',
    'path': '/isolated/java/bin/java'
  };
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    requests.add(request);
    if (request['action'] == 'cancel') busy = false;
    if (request['action'] == 'clear_error') error = null;
    if (request['action'] == 'engine') {
      error = null;
      final q = request['request'] as Map;
      switch (q['op']) {
        case 'platform':
          result = {'os': 'osx', 'arch': 'aarch64'};
        case 'game.versions':
          result = {
            'versions': [
              {
                'id': '1.20.1',
                'channel': 'release',
                'releaseTime': '2023-06-12T00:00:00Z',
                'url': 'https://example.invalid/manifest',
                'sha1': '0' * 40
              },
              {
                'id': '24w14potato',
                'channel': 'special',
                'releaseTime': '2024-04-01'
              },
              {
                'id': '24w33a',
                'channel': 'snapshot',
                'releaseTime': '2024-08-15'
              },
              {
                'id': 'b1.7.3',
                'channel': 'historical',
                'releaseTime': '2011-07-08'
              }
            ]
          };
        case 'loader.versions':
          result = [
            {
              'loader': {'version': '0.16.10', 'stable': true}
            }
          ];
        case 'java.releases':
          result = releases;
          if (failReleases) {
            error = {
              'code': 'http_503',
              'http_status': 503,
              'message': '下载服务返回 HTTP 503',
              'url': 'https://example.invalid/java-releases'
            };
          }
        case 'java.builds':
          result = [
            {
              'provider': q['vendor'],
              'version': '17.0.17+10',
              'major': q['major'],
              'os': q['os'],
              'arch': q['arch'],
              'type': 'JRE',
              'size': 48000000,
              'id': 'unique-build'
            }
          ];
          if (failQuery) {
            error = {
              'code': 'http_503',
              'http_status': 503,
              'message': '下载服务返回 HTTP 503',
              'url': 'https://example.invalid/java'
            };
          }
        case 'java.install':
          result = runtime;
          busy = waitForInstall;
        case 'game.install':
          result = {
            'id': (q['request'] as Map)['id'],
            'root': (q['request'] as Map)['root'],
            'java': runtime
          };
          busy = waitForInstall;
      }
    }
    return {
      'busy': busy,
      'result': result,
      'error': error,
      'progress': {'message': '下载文件', 'done': 50, 'total': 100}
    };
  }
}

void viewport(WidgetTester t) {
  t.view.physicalSize = const Size(880, 640);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
}

Future<void> show(WidgetTester t, DownloadCore core,
    {String tab = 'game', DownloadComplete? done}) async {
  viewport(t);
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await t.pumpWidget(MaterialApp(
      theme: ThemeData(useMaterial3: true),
      home: Scaffold(
          body: SizedBox(
              width: 656,
              child: DownloadCenter(
                  engine: engine,
                  root: '/isolated/game',
                  javaDirectory: '/isolated/java',
                  javas: const [],
                  source: 'official',
                  onSource: (_) async {},
                  onInstalled: done ?? (_, __) async {},
                  initialTab: tab)))));
  await t.pumpAndSettle();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
        '$language downloads fit minimum window and preserve native fields',
        (t) async {
      AppLanguage.instance.select(language);
      final core = DownloadCore();
      await show(t, core, tab: 'java');
      await t.tap(find.byKey(const Key('queryJavaBuilds')));
      await t.pumpAndSettle();
      await t.pump(const Duration(milliseconds: 100));
      await t.pumpAndSettle();
      expect(find.text('17.0.17+10'), findsOneWidget);
      final query = core.requests.lastWhere(
              (r) => (r['request'] as Map?)?['op'] == 'java.builds')['request']
          as Map;
      expect(query['os'], 'osx');
      expect(query['arch'], 'aarch64');
      expect(find.byKey(const Key('installDownload')), findsOneWidget);
      expect(t.takeException(), isNull);
      await t.pumpWidget(const SizedBox());
    });
  }
  testWidgets(
      'game channels are separated and selected loader build reaches core',
      (t) async {
    final core = DownloadCore();
    Map<String, dynamic>? complete;
    await show(t, core, done: (kind, result) async {
      complete = result;
    });
    expect(find.text('1.20.1'), findsOneWidget);
    expect(find.text('24w14potato'), findsNothing);
    await t.tap(find.text('特殊版本'));
    await t.pumpAndSettle();
    expect(find.text('24w14potato'), findsOneWidget);
    expect(find.text('1.20.1'), findsNothing);
    await t.tap(find.text('正式版'));
    await t.pumpAndSettle();
    await t.tap(find.text('1.20.1'));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('gameLoader')));
    await t.pumpAndSettle();
    await t.tap(find.text('Fabric').last);
    await t.pumpAndSettle();
    expect(find.textContaining('0.16.10'), findsOneWidget);
    await t.tap(find.byKey(const Key('installDownload')));
    await t.pumpAndSettle();
    final payload = core.requests.lastWhere(
            (r) => (r['request'] as Map?)?['op'] == 'game.install')['request']
        as Map;
    expect(payload['source'], 'official');
    expect(payload['request']['loader_version'], '0.16.10');
    expect(payload['request']['id'], '1.20.1-fabric');
    expect(complete?['id'], '1.20.1-fabric');
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'Java choices use the native release catalogue, never a numeric range',
      (t) async {
    final core = DownloadCore()..releases = [26, 21, 17, 8];
    await show(t, core, tab: 'java');
    await t.tap(find.byKey(const Key('javaMajor')));
    await t.pumpAndSettle();
    expect(find.text('Java 26'), findsOneWidget);
    for (final absent in [9, 25, 28, 29, 30]) {
      expect(find.text('Java $absent'), findsNothing);
    }
    await t.tap(find.text('Java 26'));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('queryJavaBuilds')));
    await t.pumpAndSettle();
    final request = core.requests
        .lastWhere((r) => (r['request'] as Map?)?['op'] == 'java.builds');
    expect(request['request']['major'], 26);
    expect(
        core.requests
            .where((r) => (r['request'] as Map?)?['op'] == 'java.releases')
            .length,
        1);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'failed Java catalogue requires explicit retry and invents no choices',
      (t) async {
    final core = DownloadCore()..failReleases = true;
    await show(t, core, tab: 'java');
    expect(find.byType(ProblemPanel), findsOneWidget);
    expect(find.byKey(const Key('queryJavaBuilds')), findsNothing);
    expect(find.byKey(const Key('javaMajor')), findsNothing);
    await t.pump(const Duration(seconds: 1));
    expect(
        core.requests
            .where((r) => (r['request'] as Map?)?['op'] == 'java.releases')
            .length,
        1);
    core.failReleases = false;
    await t.tap(find.text('重试'));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsNothing);
    expect(
        t
            .widget<FilledButton>(find.byKey(const Key('queryJavaBuilds')))
            .onPressed,
        isNotNull);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'HTTP failures stay in results area and clear after successful retry',
      (t) async {
    final core = DownloadCore()..failQuery = true;
    await show(t, core, tab: 'java');
    await t.tap(find.byKey(const Key('queryJavaBuilds')));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsOneWidget);
    expect(find.textContaining('HTTP 503'), findsWidgets);
    core.failQuery = false;
    await t.tap(find.text('重试'));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsNothing);
    expect(find.text('17.0.17+10'), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets('install progress can cancel without a completion callback',
      (t) async {
    final core = DownloadCore()..waitForInstall = true;
    bool completed = false;
    await show(t, core, tab: 'java', done: (_, __) async {
      completed = true;
    });
    await t.tap(find.byKey(const Key('queryJavaBuilds')));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('installDownload')));
    await t.pump(const Duration(milliseconds: 150));
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    await t.tap(find.text('取消'));
    await t.pump(const Duration(milliseconds: 300));
    expect(core.requests.any((r) => r['action'] == 'cancel'), isTrue);
    expect(completed, isFalse);
    expect(find.text('操作已取消'), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
}
