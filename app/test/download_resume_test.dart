// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/task_panel.dart';
import 'package:secl/localization.dart';

class ResumeCore implements LauncherCore {
  final requests = <Map<String, dynamic>>[];
  final first = Completer<Map<String, dynamic>>();
  int engineCalls = 0, cancelCalls = 0;
  bool lateCommit = false, failReplay = false, failCancelOnce = false;
  Map<String, dynamic>? committedResult;
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    switch (request['action']) {
      case 'engine':
        final input = Map<String, dynamic>.from(request['request'] as Map);
        requests.add(input);
        engineCalls++;
        if (engineCalls == 1) {
          return {
            'busy': true,
            'progress': {'message': '下载文件', 'done': 128, 'total': 1024}
          };
        }
        if (failReplay) {
          return {
            'busy': false,
            'error': {'code': 'plan_stale', 'message': 'Resource files changed'}
          };
        }
        return {
          'busy': false,
          'result': {'path': '/verified/java'}
        };
      case 'poll':
        return first.future;
      case 'cancel':
        cancelCalls++;
        if (failCancelOnce && cancelCalls == 1) {
          throw StateError('temporary cancel failure');
        }
        if (!first.isCompleted) {
          first.complete({
            'busy': false,
            'operation_committed': lateCommit,
            'result': lateCommit
                ? (committedResult ?? {'path': '/verified/java'})
                : null
          });
        }
        return {'busy': false};
      default:
        return {'busy': false};
    }
  }
}

Map<String, dynamic> java(String version) => {
      'op': 'java.install',
      'build': {'version': version, 'provider': 'temurin', 'arch': 'aarch64'}
    };
final paused =
    isA<EngineFailure>().having((e) => e.details['code'], 'code', 'paused');
final cancelled =
    isA<EngineFailure>().having((e) => e.details['code'], 'code', 'cancelled');
void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  test(
      'pausing one download leaves later work queued and replays frozen request',
      () async {
    final core = ResumeCore();
    final active = ResourceEngine(core);
    addTearDown(active.dispose);
    final input = java('17');
    final one = active.run(input);
    final waiting = expectLater(one, throwsA(paused));
    final two = active.run(java('21'));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    final record = active.history.last;
    input['build']['version'] = '99';
    await active.pauseDownload(record);
    await waiting;
    await two;
    expect(record['status'], 'paused');
    expect(core.cancelCalls, 1);
    expect(core.requests[1]['build']['version'], '21');
    expect(active.activeDownloadCount, 0);
    active.clearFinished();
    expect(active.history, [record]);
    var refreshed = 0;
    active.onReplayed = (_, __) async {
      refreshed++;
    };
    await active.resumeDownload(record);
    expect(core.requests.last['build']['version'], '17');
    expect(active.history.length, 1);
    expect(record['status'], 'complete');
    expect(refreshed, 1);
    expect(active.replayRevision, 1);
  });
  test('canceling a queued item does not cancel the active operation',
      () async {
    final core = ResumeCore();
    final active = ResourceEngine(core);
    addTearDown(active.dispose);
    final one = active.run(java('17'));
    final two = active.run(java('21'));
    final waiting = expectLater(two, throwsA(cancelled));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    await active.cancelDownload(active.history.first);
    expect(core.cancelCalls, 0);
    core.first.complete({
      'busy': false,
      'result': {'path': '/java'}
    });
    await one;
    await waiting;
    expect(core.requests.length, 1);
    expect(active.history.first['status'], 'cancelled');
    await active.resumeDownload(active.history.first);
    expect(core.requests.last['build']['version'], '21');
  });
  test('a paused task can be cancelled and cleared without native work',
      () async {
    final core = ResumeCore();
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run(java('17'));
    final waiting = expectLater(job, throwsA(paused));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    final task = engine.history.single;
    await engine.pauseDownload(task);
    await waiting;
    await engine.cancelDownload(task);
    expect(core.cancelCalls, 1);
    expect(task['status'], 'cancelled');
    engine.clearFinished();
    expect(engine.history, isEmpty);
  });
  test(
      'a concurrent game exit does not turn a successful download into failure',
      () async {
    final core = ResumeCore();
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run(java('17'));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    core.first.complete({
      'busy': false,
      'result': {'path': '/java'},
      'error': {
        'stage': 'launch',
        'code': 'game_exit_1',
        'message': 'Game exited'
      }
    });
    expect((await job)['path'], '/java');
    expect(engine.history.single['status'], 'complete');
    expect(engine.history.single['error'], isNull);
  });
  test('a transient cancel failure can be retried', () async {
    final core = ResumeCore()..failCancelOnce = true;
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run(java('17'));
    final waiting = expectLater(job, throwsA(paused));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    final task = engine.history.single;
    await expectLater(engine.pauseDownload(task), throwsStateError);
    expect(engine.canPause(task), true);
    await engine.pauseDownload(task);
    await waiting;
    expect(core.cancelCalls, 2);
    expect(task['status'], 'paused');
  });
  test('a restored backup committed before cancel remains successful',
      () async {
    final core = ResumeCore()
      ..lateCommit = true
      ..committedResult = {'restored': true, 'id': 'copy'};
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run({'op': 'instance.backups.restore'});
    await Future<void>.delayed(const Duration(milliseconds: 5));
    await engine.cancel();
    expect((await job)['restored'], true);
    expect(engine.history, isEmpty);
  });
  test('an install committed before pause remains complete', () async {
    final core = ResumeCore()..lateCommit = true;
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run(java('17'));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    await engine.pauseDownload(engine.history.single);
    await job;
    expect(engine.history.single['status'], 'complete');
    expect(engine.canResume(engine.history.single), false);
  });
  test(
      'stale replay is rejected and stays retryable without fabricating success',
      () async {
    final core = ResumeCore();
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    final job = engine.run(java('17'));
    final waiting = expectLater(job, throwsA(paused));
    await Future<void>.delayed(const Duration(milliseconds: 5));
    await engine.pauseDownload(engine.history.single);
    await waiting;
    core.failReplay = true;
    await expectLater(engine.resumeDownload(engine.history.single),
        throwsA(isA<EngineFailure>()));
    expect(engine.history.single['status'], 'failed');
    expect(engine.history.single['error']['code'], 'plan_stale');
    expect(engine.replayRevision, 0);
    expect(engine.canResume(engine.history.single), true);
  });
  testWidgets('task panel offers pause and resume without overflowing',
      (t) async {
    final core = ResumeCore();
    final active = ResourceEngine(core);
    addTearDown(active.dispose);
    t.view.physicalSize = const Size(560, 600);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    final job = active.run(java('17'));
    final waiting = expectLater(job, throwsA(paused));
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Center(
                child:
                    SizedBox(width: 400, child: TaskPanel(engine: active))))));
    await t.pump();
    await t.tap(find.byTooltip(tr('暂停下载')));
    await t.pump(const Duration(milliseconds: 150));
    await waiting;
    await t.pump();
    expect(find.text(tr('已暂停')), findsOneWidget);
    expect(find.byTooltip(tr('继续下载')), findsOneWidget);
    await t.tap(find.byTooltip(tr('继续下载')));
    await t.pumpAndSettle();
    expect(find.text(tr('已完成')), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
}
