// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_backups.dart';
import 'package:secl/instance_page.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';

const instance = {
  'id': 'original',
  'name': 'Original',
  'root': '/test/root',
  'version': '1.20.1',
  'loader': 'fabric',
  'loader_version': '0.16.10',
  'game_directory': '/test/root/instances/original'
};

class BackupCore implements LauncherCore {
  final requests = <Map<String, dynamic>>[];
  bool cancelled = false;
  Completer<Map<String, dynamic>>? pending;
  final rows = [
    {
      'id': 'original',
      'name': 'Original',
      'path': '/backup/original.secl-backup',
      'created': 1794000000,
      'size': 1048576,
      'file_count': 12,
      'problem': ''
    }
  ];
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] == 'cancel') {
      cancelled = true;
      pending?.complete({
        'busy': false,
        'error': {'code': 'cancelled', 'message': '操作已取消'}
      });
      return {'busy': false};
    }
    if (request['action'] != 'engine') return {'busy': false};
    final r = Map<String, dynamic>.from(request['request'] as Map);
    requests.add(r);
    switch (r['op']) {
      case 'instance.backups.list':
        return {
          'busy': false,
          'result': {'directory': r['directory'] ?? '/backup', 'items': rows}
        };
      case 'instance.backups.create':
        if (pending != null) return pending!.future;
        return {
          'busy': false,
          'result': {'path': '/backup/new.secl-backup'}
        };
      case 'instance.backups.restore':
        return {
          'busy': false,
          'result': {
            'restored': true,
            'id': r['id'],
            'requires_shared_dependencies': true
          }
        };
      default:
        return {'busy': false, 'result': {}};
    }
  }
}

Future<void> show(WidgetTester t, BackupCore core,
    {VoidCallback? restored}) async {
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await t.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Padding(
              padding: const EdgeInsets.all(16),
              child: InstanceBackups(
                  engine: engine,
                  instance: instance,
                  onRestored: restored ?? () {},
                  pickDirectory: () async => '/external/backups')))));
  await t.pumpAndSettle();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  testWidgets('backup scope and shared dependency limitation are explicit',
      (t) async {
    final core = BackupCore();
    await show(t, core);
    await t.tap(find.text(tr('备份内容与限制')));
    await t.pumpAndSettle();
    expect(find.textContaining('不包含启动器账号'), findsOneWidget);
    expect(find.textContaining('不是 ZIP'), findsOneWidget);
    expect(find.text('Original'), findsOneWidget);
    await t.tap(find.text(tr('备份内容与限制')));
    await t.pumpAndSettle();
    expect(core.requests.where((r) => r['op'] == 'instance.backups.create'),
        isEmpty);
    await t.tap(find.byKey(const Key('chooseInstanceBackupDirectory')));
    await t.pumpAndSettle();
    expect(core.requests.last['directory'], '/external/backups');
    await t.tap(find.byKey(const Key('createInstanceBackup')));
    await t.pumpAndSettle();
    final create =
        core.requests.firstWhere((r) => r['op'] == 'instance.backups.create');
    expect(create['directory'], '/external/backups');
    expect(create['instance'], instance);
    expect(find.text(tr('实例备份已完成')), findsOneWidget);
  });
  testWidgets('restore requires a new id and sends no overwrite option',
      (t) async {
    final core = BackupCore();
    var restored = 0;
    await show(t, core, restored: () => restored++);
    await t.tap(find.text(tr('恢复副本')));
    await t.pumpAndSettle();
    expect(find.byType(RestoreInstanceBackupDialog), findsOneWidget);
    expect(find.textContaining('固定 Java'), findsOneWidget);
    await t.enterText(find.byKey(const Key('restoreInstanceId')), 'original');
    await t.pumpAndSettle();
    expect(
        t
            .widget<FilledButton>(
                find.byKey(const Key('confirmInstanceRestore')))
            .onPressed,
        isNull);
    await t.enterText(find.byKey(const Key('restoreInstanceId')), '../outside');
    await t.pumpAndSettle();
    expect(
        t
            .widget<FilledButton>(
                find.byKey(const Key('confirmInstanceRestore')))
            .onPressed,
        isNull);
    await t.enterText(find.byKey(const Key('restoreInstanceId')), 'new-copy');
    await t.enterText(
        find.byKey(const Key('restoreInstanceName')), 'Recovered');
    await t.tap(find.byKey(const Key('confirmInstanceRestore')));
    await t.pumpAndSettle();
    final r =
        core.requests.firstWhere((r) => r['op'] == 'instance.backups.restore');
    expect(r['id'], 'new-copy');
    expect(r['name'], 'Recovered');
    expect(r['backup'], '/backup/original.secl-backup');
    expect(r['root'], '/test/root');
    expect(r.containsKey('replace'), isFalse);
    expect(restored, 1);
  });
  testWidgets('cancelling restore dialog does not alter any instance',
      (t) async {
    final core = BackupCore();
    await show(t, core);
    await t.tap(find.text(tr('恢复副本')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('取消')));
    await t.pumpAndSettle();
    expect(core.requests.where((r) => r['op'] == 'instance.backups.restore'),
        isEmpty);
  });
  testWidgets('active backup exposes cancel and cancellation finishes visibly',
      (t) async {
    final core = BackupCore();
    await show(t, core);
    core.pending = Completer<Map<String, dynamic>>();
    await t.tap(find.byKey(const Key('createInstanceBackup')));
    await t.pump();
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    await t.tap(find.byKey(const Key('cancelInstanceBackup')));
    await t.pumpAndSettle();
    expect(core.cancelled, isTrue);
    expect(find.text('cancelled'), findsOneWidget);
    expect(find.byType(LinearProgressIndicator), findsNothing);
  });
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language real InstancePage backups fit 656 by 640',
        (t) async {
      t.view.physicalSize = const Size(656, 640);
      t.view.devicePixelRatio = 1;
      addTearDown(t.view.resetPhysicalSize);
      addTearDown(t.view.resetDevicePixelRatio);
      AppLanguage.instance.select(language);
      final engine = ResourceEngine(BackupCore());
      addTearDown(engine.dispose);
      await t.pumpWidget(MaterialApp(
          home: Scaffold(
              body: InstancePage(
                  engine: engine,
                  instance: instance,
                  javas: const [],
                  initialTab: 'backups',
                  onBack: () {},
                  onChanged: () {},
                  onBrowse: (_, {query}) {}))));
      await t.pumpAndSettle();
      expect(t.takeException(), isNull);
      expect(find.text(tr('恢复副本')).hitTestable(), findsOneWidget);
      expect(find.text(tr('不含账号、共享资源库或 Java；恢复仍需已有游戏依赖。')), findsOneWidget);
      await t.tap(find.text(tr('备份内容与限制')));
      await t.pumpAndSettle();
      expect(t.takeException(), isNull);
      final scroll = find.byKey(const Key('instanceBackupScroll'));
      await t.drag(scroll, const Offset(0, -380));
      await t.pumpAndSettle();
      await t.ensureVisible(find.text(tr('恢复副本')));
      await t.tap(find.text(tr('恢复副本')));
      await t.pumpAndSettle();
      expect(find.byType(RestoreInstanceBackupDialog), findsOneWidget);
      expect(t.takeException(), isNull);
      await t.tap(find.text(tr('取消')));
      await t.pumpAndSettle();
    });
  }
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language backup list and restore dialog fit compact window',
        (t) async {
      t.view.physicalSize = const Size(680, 640);
      t.view.devicePixelRatio = 1;
      addTearDown(t.view.resetPhysicalSize);
      addTearDown(t.view.resetDevicePixelRatio);
      AppLanguage.instance.select(language);
      await show(t, BackupCore());
      await t.tap(find.text(tr('恢复副本')));
      await t.pumpAndSettle();
      expect(t.takeException(), isNull);
      await t.tap(find.text(tr('取消')));
      await t.pumpAndSettle();
      expect(t.takeException(), isNull);
    });
  }
}
