// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/appearance.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_library.dart';
import 'package:secl/localization.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/resource_engine.dart';

List<Data> historyInstances() => [
      {
        'id': 'old',
        'name': 'Zulu',
        'version': '1.9.4',
        'loader': 'vanilla',
        'last_launch': 100,
        'launch_count': 2,
        'runtime_seconds': 10,
      },
      {
        'id': 'recent',
        'name': 'Alpha',
        'version': '1.20.1',
        'loader': 'fabric',
        'loader_version': '0.16.10',
        'last_launch': 200,
        'launch_count': 3,
        'runtime_seconds': 7260,
      },
      {
        'id': 'newversion',
        'name': 'Beta',
        'version': '26.1',
        'loader': 'vanilla',
        'last_launch': 0,
        'launch_count': 0,
        'runtime_seconds': 0,
      },
    ];

class LibraryHistoryCore implements LauncherCore {
  @override
  Future<Data> call(Data request) async => {
        'busy': false,
        'result': {'instances': historyInstances()}
      };
}

void main() {
  setUp(() {
    AppLanguage.instance.select('en');
    AppAppearance.instance.select('light', 'blue');
  });
  tearDown(() {
    AppLanguage.instance.select('zh_Hans');
    AppAppearance.instance.select('system', 'blue');
  });
  test('library ordering is natural, deterministic and does not mutate scans',
      () {
    final rows = historyInstances();
    List<String> ids(String sort) => orderedLibraryInstances(rows, sort)
        .map((row) => row['id'] as String)
        .toList();
    expect(ids('recent'), ['recent', 'old', 'newversion']);
    expect(ids('name'), ['recent', 'newversion', 'old']);
    expect(ids('version'), ['newversion', 'recent', 'old']);
    expect(rows.map((row) => row['id']), ['old', 'recent', 'newversion']);
    rows.first['favorite'] = true;
    expect(ids('name').first, 'old');
    expect(ids('version').first, 'old');
    final previews = [
      {'id': 'preview', 'version': '1.20.1-pre1'},
      {'id': 'release', 'version': '1.20.1'},
    ];
    expect(orderedLibraryInstances(previews, 'version').first['id'], 'release');
  });
  test('history labels distinguish missing, known and unreadable statistics',
      () {
    expect(libraryHistorySummary(historyInstances().last), tr('尚无启动记录'));
    expect(libraryRuntimeLabel(10), '<1m');
    expect(libraryRuntimeLabel(120), '2m');
    expect(libraryRuntimeLabel(7260), '2h 1m');
    expect(libraryHistorySummary(historyInstances()[1]), contains('2h 1m'));
    expect(libraryHistoryDetails(historyInstances()[1]),
        contains(tr('运行时间包括挂起和闲置，仅记录启动器观察到的进程运行时长。')));
    expect(
        libraryHistorySummary({
          'history_warning': {'message': 'Preserved damaged file'},
          'launch_count': 0
        }),
        tr('统计暂不可用'));
    expect(
        instanceHistoryNumber(
            {'last_launch': 9223372036854775807}, 'last_launch'),
        0);
    expect(instanceHistoryNumber({'launch_count': -5}, 'launch_count'), 0);
  });
  testWidgets('compact English library exposes sorting and readable history',
      (t) async {
    t.view.physicalSize = const Size(880, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    final engine = ResourceEngine(LibraryHistoryCore());
    addTearDown(engine.dispose);
    for (final mode in ['light', 'dark']) {
      AppAppearance.instance.select(mode, 'blue');
      await t.pumpWidget(MaterialApp(
          theme: ThemeData(
              useMaterial3: true,
              brightness: mode == 'dark' ? Brightness.dark : Brightness.light),
          home: Scaffold(
              body: SizedBox(
                  width: 656,
                  child: InstanceLibrary(
                      key: ValueKey(mode),
                      engine: engine,
                      root: '/example',
                      onOpen: (_, __) {},
                      onInstall: () {},
                      onImport: () {},
                      onLaunch: (_) {})))));
      await t.pumpAndSettle();
      expect(find.byKey(const Key('librarySort')), findsOneWidget);
      expect(
          find.byKey(const ValueKey('library-history-recent')), findsOneWidget);
      expect(t.getTopLeft(find.text('Alpha')).dx,
          lessThan(t.getTopLeft(find.text('Zulu')).dx));
      await t.tap(find.byKey(const Key('librarySort')));
      await t.pumpAndSettle();
      await t.tap(find.text(tr('游戏版本')));
      await t.pumpAndSettle();
      expect(t.getTopLeft(find.text('Beta')).dy,
          lessThan(t.getTopLeft(find.text('Zulu')).dy));
      expect(t.takeException(), isNull);
      await t.pumpWidget(const SizedBox());
    }
  });
}
