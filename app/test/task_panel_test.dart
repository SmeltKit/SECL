// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/task_panel.dart';
import 'package:secl/localization.dart';

class TaskCore implements LauncherCore {
  final List<Map<String, dynamic>> requests = [];
  Completer<Map<String, dynamic>>? finish;
  Map<String, dynamic>? failure;
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    requests.add(request);
    if (request['action'] == 'poll') return finish!.future;
    if (request['action'] == 'cancel') return {};
    if (request['action'] == 'clear_error') return {};
    if (finish != null) {
      return {
        'busy': true,
        'progress': {
          'message': '下载文件',
          'done': 512,
          'total': 1024,
        }
      };
    }
    return failure == null ? {'result': {}} : {'error': failure};
  }
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));

  test('catalogue reads never become download history', () async {
    final engine = ResourceEngine(TaskCore());
    for (final op in [
      'resource.search',
      'resource.tags',
      'instances.scan',
      'java.builds',
      'java.scan',
      'mods.plan',
      'worlds.export'
    ]) {
      await engine.run({'op': op}, title: 'Lookup');
    }
    expect(engine.downloadHistory, isEmpty);
    expect(engine.activeDownloadCount, 0);
    engine.dispose();
  });

  test('install tasks retain real identity, progress and completion', () async {
    final core = TaskCore()..finish = Completer<Map<String, dynamic>>();
    final engine = ResourceEngine(core);
    final job = engine.run({
      'op': 'java.install',
      'build': {
        'version': '21.0.8+9',
        'provider': 'temurin',
        'arch': 'aarch64',
      }
    }, title: '下载');
    await Future<void>.delayed(const Duration(milliseconds: 5));
    expect(engine.activeDownloadCount, 1);
    expect(engine.activeDownloadTitle, 'Java 21.0.8+9');
    expect(engine.currentDownload?['progress'], {
      'message': '下载文件',
      'done': 512,
      'total': 1024,
    });
    core.finish!.complete({
      'result': {'path': '/runtime/bin/java'}
    });
    await job;
    expect(engine.activeDownloadCount, 0);
    expect(engine.activeDownloadTitle, isEmpty);
    expect(engine.history.single['status'], 'complete');
    expect(engine.history.single['progress']['done'], 512);
    engine.clearFinished();
    expect(engine.history, isEmpty);
    engine.dispose();
  });

  test('cancel retires the current download and queued downloads', () async {
    final core = TaskCore()..finish = Completer<Map<String, dynamic>>();
    final engine = ResourceEngine(core);
    final one = engine.run({
      'op': 'java.install',
      'build': {'version': '17.0.9'}
    });
    final two = engine.run({
      'op': 'game.install',
      'request': {'id': '1.20.1'}
    });
    final cancelled = isA<EngineFailure>()
        .having((error) => error.details['code'], 'code', 'cancelled');
    final waitOne = expectLater(one, throwsA(cancelled));
    final waitTwo = expectLater(two, throwsA(cancelled));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    expect(engine.activeDownloadCount, 2);
    expect(engine.activeDownloadTitle, 'Java 17.0.9');
    await engine.cancel();
    core.finish!.complete({'result': {}});
    await Future.wait([waitOne, waitTwo]);
    expect(engine.activeDownloadCount, 0);
    expect(
        engine.history.every((task) => task['status'] == 'cancelled'), isTrue);
    engine.dispose();
  });

  test('queued dependencies, shader identity and HTTP failures stay attached',
      () async {
    final core = TaskCore();
    final engine = ResourceEngine(core);
    await engine.run({
      'op': 'mods.apply',
      'plan': {
        'target': {'id': 'survival'},
        'root_project': 'comforts',
        'files': [
          {'project_id': 'api', 'title': 'Fabric API', 'version': '0.92.2'},
          {
            'project_id': 'comforts',
            'root': true,
            'title': 'Comforts',
            'version': '6.4.0'
          },
        ]
      }
    });
    expect(engine.history.single['title'], 'Comforts · 6.4.0');
    expect(engine.history.single['items'],
        ['Fabric API · 0.92.2', 'Comforts · 6.4.0']);
    expect(engine.history.single['target'], 'survival');
    core.failure = {
      'code': 'http_error',
      'http_status': 503,
      'message': 'Service unavailable'
    };
    await expectLater(
        engine.run({
          'op': 'packs.apply',
          'plan': {
            'kind': 'shader',
            'target': {'id': 'survival'},
            'file': {
              'title': 'Complementary Reimagined',
              'version': 'r5.9.3',
            }
          }
        }),
        throwsA(isA<EngineFailure>()));
    expect(engine.history.first['title'], 'Complementary Reimagined · r5.9.3');
    expect(engine.history.first['kind'], 'shader');
    expect(engine.history.first['status'], 'failed');
    expect(engine.history.first['error']['http_status'], 503);
    expect(core.requests.last['action'], 'clear_error');
    engine.dispose();
  });

  testWidgets('compact download popover shows item names and actual progress',
      (t) async {
    t.view.physicalSize = const Size(560, 520);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    final engine = ResourceEngine(TaskCore());
    engine.history.addAll([
      {
        'kind': 'java',
        'title': 'Java 21.0.8+9',
        'subtitle': 'Temurin · ARM64',
        'status': 'running',
        'progress': {'message': '下载文件', 'done': 512, 'total': 1024}
      },
      {
        'kind': 'shader',
        'title': 'Complementary Reimagined',
        'target': 'Survival',
        'status': 'queued',
        'items': ['shader.zip', 'iris.jar', 'sodium.jar']
      },
    ]);
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Center(
                child:
                    SizedBox(width: 420, child: TaskPanel(engine: engine))))));
    await t.pump();
    expect(find.text('Java 21.0.8+9'), findsOneWidget);
    expect(find.text('Complementary Reimagined'), findsOneWidget);
    expect(find.text('50%'), findsOneWidget);
    expect(find.textContaining('HTTP'), findsNothing);
    expect(find.text('等待中'), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
    engine.dispose();
  });
}
