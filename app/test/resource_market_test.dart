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
  String? curseForgeKeyError;
  int total = 45;
  static const curseForgeProject =
      'https://www.curseforge.com/minecraft/mc-mods/sample';
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
  Map<String, dynamic> get curseHit => {
        ...hit,
        'source': 'curseforge',
        'project_id': '200',
        'id': '200',
        'title': 'CurseForge Sample',
        'website_url': curseForgeProject,
      };
  Map<String, dynamic> curseVersion(int offset) => {
        ...version,
        'source': 'curseforge',
        'project_id': '200',
        'version_id': offset == 0 ? '80' : '81',
        'version': offset == 0 ? 'Website only' : 'Downloadable',
        'website_url': '$curseForgeProject/files/80',
        'downloadable': offset != 0,
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
      final curseForge = r['source'] == 'curseforge';
      dynamic result;
      switch (r['op']) {
        case 'resource.search':
          if (curseForge && curseForgeKeyError != null) {
            return {
              ...state,
              'error': {
                'code': curseForgeKeyError,
                'message': curseForgeKeyError == 'curseforge_key_missing'
                    ? '此构建未配置 CurseForge 访问。'
                    : 'CurseForge 访问密钥格式无效。'
              }
            };
          }
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
            'hits': [curseForge ? curseHit : hit],
            'total_hits': total
          };
          break;
        case 'resource.categories':
          result = [
            {'name': 'utility', 'label': '实用工具'}
          ];
          break;
        case 'resource.project':
          result = curseForge ? curseHit : hit;
          break;
        case 'resource.versions':
          if (curseForge) {
            final offset = r['offset'] as int;
            result = {
              'files': [curseVersion(offset)],
              'next_offset': offset == 0 ? 50 : null,
            };
          } else {
            result = [version];
          }
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
      'CurseForge keeps a separate catalog and offers official file links',
      (t) async {
    viewport(t);
    AppLanguage.instance.select('en');
    final core = CatalogCore()..curseForgeKeyError = 'curseforge_key_missing';
    final engine = ResourceEngine(core);
    addTearDown(engine.dispose);
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: ResourceMarket(
                kind: 'mod', engine: engine, root: '', selectedVersion: ''))));
    await t.pumpAndSettle();
    final search = find.byKey(const Key('resourceSearch'));
    await t.enterText(search, 'Modrinth query');
    await t.tap(find.text('Search'));
    await t.pumpAndSettle();

    Future<void> switchTo(String name) async {
      await t.tap(find.byKey(const Key('resourceSource')));
      await t.pumpAndSettle();
      await t.tap(find.text(name).last);
      await t.pumpAndSettle();
    }

    await switchTo('CurseForge');
    expect(t.widget<TextField>(search).controller!.text, isEmpty);
    expect(find.text('This build has no CurseForge access configured.'),
        findsOneWidget);
    core.curseForgeKeyError = 'curseforge_key_invalid';
    await t.tap(find.text('Retry'));
    await t.pumpAndSettle();
    expect(find.text('The CurseForge access key has an invalid format.'),
        findsOneWidget);
    await switchTo('Modrinth');
    expect(t.widget<TextField>(search).controller!.text, 'Modrinth query');
    expect(find.text('Test Resource'), findsOneWidget);

    core.curseForgeKeyError = null;
    await switchTo('CurseForge');
    await t.tap(find.text('CurseForge Sample'));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('下载')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('install-0')), findsNothing);
    await t.tap(find.byKey(const Key('website-0')));
    await t.pumpAndSettle();
    final requests = core.requests
        .where((r) => r['action'] == 'engine')
        .map((r) => r['request'] as Map);
    expect(requests.last,
        containsPair('url', '${CatalogCore.curseForgeProject}/files/80'));
    await t.tap(find.byKey(const Key('moreResourceFiles')));
    await t.pumpAndSettle();
    expect(requests.last, containsPair('offset', 50));
    expect(requests.last, containsPair('source', 'curseforge'));
    final install = find.byKey(const Key('install-1'));
    await t.scrollUntilVisible(install, 75,
        scrollable: find.descendant(
            of: find.byKey(const Key('resourceFileList')),
            matching: find.byType(Scrollable)));
    expect(install.hitTestable(), findsOneWidget);
    expect(t.takeException(), isNull);
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
