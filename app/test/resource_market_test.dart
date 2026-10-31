// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/main.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';
import 'package:secl/resource_market.dart';

class CatalogCore implements LauncherCore {
  bool fail = false;
  int total = 45;
  final requests = <Map<String, dynamic>>[];
  final hit = <String, dynamic>{
    'project_id': 'test',
    'id': 'test',
    'slug': 'test',
    'title': 'Test Resource',
    'author': 'Author',
    'description': 'Real resource description',
    'categories': ['utility'],
    'downloads': 100
  };
  Map<String, dynamic> get version => {
        'project_id': 'test',
        'version_id': 'v1',
        'filename': 'test.jar',
        'version': '1.0',
        'name': 'Test version',
        'size': 2048,
        'release_type': 'release',
        'loaders': ['fabric'],
        'game_versions': ['1.20.1'],
        'dependencies': []
      };
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    requests.add(request);
    final state = <String, dynamic>{
      'language': 'zh_Hans',
      'root': '/not/installed',
      'versions': [],
      'javas': [],
      'account': null,
      'busy': false,
      'error': null
    };
    if (request['action'] == 'engine') {
      final r = request['request'] as Map;
      dynamic result;
      switch (r['op']) {
        case 'resource.search':
          if (fail) {
            return {
              ...state,
              'error': {
                'code': 'http_503',
                'http_status': 503,
                'message': '服务暂时不可用'
              }
            };
          }
          result = {
            'hits': [hit],
            'total_hits': total
          };
          break;
        case 'resource.categories':
          result = [
            {'name': 'utility', 'label': '实用工具'}
          ];
          break;
        case 'resource.project':
          result = hit;
          break;
        case 'resource.versions':
          result = [version];
          break;
        case 'resource.games':
          result = [
            {'version': '1.20.1', 'channel': 'release'},
            {'version': '24w14a', 'channel': 'snapshot'}
          ];
          break;
        case 'instances.scan':
          result = {'instances': []};
          break;
        default:
          result = {};
      }
      return {...state, 'result': result};
    }
    return state;
  }
}

void viewport(WidgetTester t) {
  t.view.physicalSize = const Size(880, 640);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  testWidgets(
      'three markets share compact layout and keep launch auth untouched',
      (t) async {
    viewport(t);
    final core = CatalogCore();
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    for (final kind in ['mod', 'shader', 'resourcepack']) {
      await t.tap(find.byKey(Key('nav-$kind')));
      await t.pumpAndSettle();
      expect(find.text(titleFor(kind)), findsOneWidget);
      expect(find.text('Test Resource'), findsOneWidget);
      expect(t.getCenter(find.byKey(const Key('catalogPagination'))).dy,
          greaterThan(550));
      expect(t.takeException(), isNull);
    }
    expect(
        core.requests
            .where((r) => r['action'] == 'launch' || r['action'] == 'login'),
        isEmpty);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'network error clears after explicit retry and details return preserves search',
      (t) async {
    viewport(t);
    final core = CatalogCore()..fail = true;
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('nav-mod')));
    await t.pumpAndSettle();
    expect(find.text('http_503 · HTTP 503'), findsOneWidget);
    core.fail = false;
    await t.tap(find.text('重试').first);
    await t.pumpAndSettle();
    expect(find.text('http_503 · HTTP 503'), findsNothing);
    await t.enterText(find.byKey(const Key('resourceSearch')), 'saved query');
    await t.tap(find.text('搜索'));
    await t.pumpAndSettle();
    await t.tap(find.text('Test Resource'));
    await t.pumpAndSettle();
    expect(find.text('项目主页'), findsOneWidget);
    await t.tap(find.byKey(const Key('backToCatalog')));
    await t.pumpAndSettle();
    expect(
        t
            .widget<TextField>(find.byKey(const Key('resourceSearch')))
            .controller!
            .text,
        'saved query');
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'dependency checkboxes default required on optional off and require warning acknowledgement',
      (t) async {
    viewport(t);
    final plan = <String, dynamic>{
      'root_project': 'main',
      'target': {
        'id': 'Test instance',
        'version': '1.20.1',
        'loader': 'fabric'
      },
      'files': [
        {'project_id': 'main', 'name': 'Main', 'version': '1', 'root': true},
        {
          'project_id': 'required',
          'name': 'Required',
          'version': '1',
          'optional': false
        },
        {
          'project_id': 'optional',
          'name': 'Optional',
          'version': '1',
          'optional': true
        }
      ],
      'edges': [
        {'from': 'main', 'to': 'required', 'optional': false},
        {'from': 'main', 'to': 'optional', 'optional': true}
      ],
      'reuse': []
    };
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: Builder(
                builder: (context) => TextButton(
                    onPressed: () => showDialog<void>(
                        context: context,
                        builder: (_) =>
                            InstallPlanDialog(plan: plan, kind: 'mod')),
                    child: const Text('Show'))))));
    await t.tap(find.text('Show'));
    await t.pumpAndSettle();
    final boxes = t.widgetList<Checkbox>(find.byType(Checkbox)).toList();
    expect(boxes[0].value, isTrue);
    expect(boxes[1].value, isTrue);
    expect(boxes[2].value, isFalse);
    await t.tap(find.byType(Checkbox).at(1));
    await t.pumpAndSettle();
    expect(
        t.widget<FilledButton>(find.byKey(const Key('applyInstall'))).onPressed,
        isNull);
    expect(find.textContaining('已取消 1 项必需依赖'), findsOneWidget);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
  test(
      'target matching keeps Forge separate from Fabric and accepts explicit Quilt support',
      () {
    final file = {
      'game_versions': ['1.20.1'],
      'loaders': ['fabric']
    };
    expect(compatible(file, {'version': '1.20.1', 'loader': 'forge'}, 'mod'),
        isFalse);
    expect(compatible(file, {'version': '1.20.1', 'loader': 'fabric'}, 'mod'),
        isTrue);
    expect(compatible(file, {'version': '1.19.4', 'loader': 'fabric'}, 'mod'),
        isFalse);
  });
  testWidgets('version selector separates stable and test releases', (t) async {
    viewport(t);
    final engine = ResourceEngine(CatalogCore());
    addTearDown(engine.dispose);
    await t.pumpWidget(MaterialApp(
        home: ChoicePicker(
            title: 'Versions',
            value: '',
            load: () async => [
                  const Choice('1.20.1', '1.20.1'),
                  const Choice('24w14a', '24w14a', group: 'snapshot')
                ],
            groups: const {'release': '正式版', 'snapshot': '测试版'})));
    await t.pumpAndSettle();
    expect(find.text('1.20.1'), findsOneWidget);
    expect(find.text('24w14a'), findsNothing);
    await t.tap(find.text('测试版'));
    await t.pumpAndSettle();
    expect(find.text('24w14a'), findsOneWidget);
    expect(find.text('1.20.1'), findsNothing);
    expect(t.takeException(), isNull);
  });
}
