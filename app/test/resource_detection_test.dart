// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_resources.dart';
import 'package:secl/localization.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/resource_detection.dart';
import 'package:secl/resource_engine.dart';

Data resource(
  String name, {
  String local = 'recognized',
  String source = 'not_checked',
  String compatibility = 'no_known_conflict',
  bool enabled = true,
  List<Data> issues = const [],
}) =>
    {
      'filename': '$name.jar',
      'name': name,
      'id': name.toLowerCase(),
      'loader': 'fabric',
      'version': '1.0',
      'enabled': enabled,
      'detection': {
        'local': local,
        'source': source,
        'compatibility': compatibility,
        'issues': issues,
      },
    };

class InventoryCore implements LauncherCore {
  final requests = <Data>[];
  List<Data> items = [resource('Demo')];
  Data? sourceError;
  List<Data>? sourceErrors;
  bool throwEnrich = false;
  Completer<void>? pendingEnrich;
  Data get instance => {
        'id': 'Test',
        'loader': 'fabric',
        'version': '1.20.1',
        'game_directory': '/test',
      };

  @override
  Future<Data> call(Data request) async {
    if (request['action'] != 'engine') return {'busy': false};
    final operation = Map<String, dynamic>.from(request['request'] as Map);
    requests.add(operation);
    if (operation['op'] == 'mods.enrich') await pendingEnrich?.future;
    if (operation['op'] == 'mods.enrich' && throwEnrich) {
      return {
        'busy': false,
        'error': {
          'code': 'http_429',
          'http_status': 429,
          'message': 'Rate limited',
        },
      };
    }
    return {
      'busy': false,
      'result': {
        'items': items,
        'checks': {'errors': [], 'warnings': []},
        'support': {},
        if (sourceError != null) 'source_error': sourceError,
        if (sourceErrors != null && operation['op'] == 'mods.enrich')
          'source_errors': sourceErrors,
      },
    };
  }
}

Future<void> showInventory(
  WidgetTester t,
  InventoryCore core, {
  String kind = 'mod',
}) async {
  t.view.physicalSize = const Size(880, 700);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await t.pumpWidget(
    MaterialApp(
      home: Scaffold(
        body: Padding(
          padding: const EdgeInsets.all(20),
          child: InstanceResources(
            engine: engine,
            instance: core.instance,
            kind: kind,
            onBrowse: (_, {query}) {},
          ),
        ),
      ),
    ),
  );
  await t.pumpAndSettle();
}

Finder statusFor(String name) => find.byWidgetPredicate(
      (widget) =>
          widget is ResourceDetectionStatus && widget.row['name'] == name,
    );

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));

  testWidgets(
    'local recognition does not imply a website source or compatibility',
    (t) async {
      final core = InventoryCore()
        ..items = [resource('Local', source: 'unmatched')];
      await showInventory(t, core);
      expect(find.text(tr('未发现已知冲突')), findsOneWidget);
      expect(find.text(tr('本地已识别')), findsOneWidget);
      expect(find.text(tr('来源未匹配')), findsOneWidget);
      final tooltip = t.widget<Tooltip>(
        find.descendant(of: statusFor('Local'), matching: find.byType(Tooltip)),
      );
      expect(tooltip.message, contains(tr('静态检查未发现已知冲突，仍需在游戏内验证。')));
      expect(t.takeException(), isNull);
    },
  );

  testWidgets('matched website source cannot resolve unknown local metadata', (
    t,
  ) async {
    final core = InventoryCore()
      ..items = [resource('Unknown', local: 'unknown', source: 'matched')];
    await showInventory(t, core);
    expect(find.text(tr('需核实')), findsOneWidget);
    expect(find.text(tr('未发现已知冲突')), findsNothing);
    expect(find.text(tr('本地元数据未知')), findsOneWidget);
    expect(find.text(tr('来源已匹配')), findsOneWidget);
    expect(t.takeException(), isNull);
  });

  testWidgets('instance conflict retains issue code and all involved files', (
    t,
  ) async {
    final issue = {
      'severity': 'error',
      'code': 'duplicate_id',
      'message': 'Both files declare demo',
      'files': ['Demo.jar', 'Copy.jar'],
    };
    final core = InventoryCore()
      ..items = [
        resource('Demo', compatibility: 'conflict', issues: [issue]),
        resource('Copy', compatibility: 'conflict', issues: [issue]),
      ];
    await showInventory(t, core);
    expect(find.text(tr('存在冲突')), findsNWidgets(2));
    final tooltip = t.widget<Tooltip>(
      find.descendant(of: statusFor('Demo'), matching: find.byType(Tooltip)),
    );
    expect(tooltip.message, contains('duplicate_id'));
    expect(tooltip.message, contains('Demo.jar, Copy.jar'));
    expect(tooltip.message, contains('Both files declare demo'));
  });

  testWidgets('online lookup shows each provider without losing local rows', (
    t,
  ) async {
    final core = InventoryCore();
    await showInventory(t, core);
    expect(core.requests.map((r) => r['op']), ['instance.inventory']);
    core.items = [
      resource('Demo', source: 'matched')
        ..['source'] = 'curseforge'
        ..['sources'] = ['curseforge'],
    ];
    core.sourceErrors = [
      {
        'source': 'modrinth',
        'code': 'http_503',
        'http_status': 503,
        'message': 'Service unavailable',
      }
    ];
    await t.tap(find.text(tr('补全来源信息')));
    await t.pumpAndSettle();
    expect(find.text('Demo'), findsOneWidget);
    expect(find.text('CurseForge'), findsOneWidget);
    expect(find.text('Modrinth · Service unavailable'), findsOneWidget);
    expect(find.text('http_503 · HTTP 503'), findsOneWidget);
    expect(find.text(tr('未发现已知冲突')), findsOneWidget);
    expect(t.takeException(), isNull);
  });

  testWidgets('problem filter includes structured and legacy warnings', (
    t,
  ) async {
    final legacy = resource('Legacy')..remove('detection');
    legacy['known'] = true;
    legacy['warnings'] = ['Version range needs manual review'];
    final core = InventoryCore()
      ..items = [
        resource('Clean'),
        resource(
          'Warning',
          issues: [
            {
              'severity': 'warning',
              'code': 'range_unknown',
              'message': 'Cannot compare version range',
              'files': ['Warning.jar'],
            },
          ],
        ),
        legacy,
      ];
    await showInventory(t, core);
    await t.tap(find.byKey(const Key('resourceStatusFilter')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('需要处理')).last);
    await t.pumpAndSettle();
    expect(find.text('Clean'), findsNothing);
    expect(find.text('Warning'), findsOneWidget);
    expect(find.text('Legacy'), findsOneWidget);
    expect(find.text(tr('需核实')), findsNWidgets(2));
    expect(t.takeException(), isNull);
  });

  testWidgets('disabled files retain metadata and source recognition', (
    t,
  ) async {
    final core = InventoryCore()
      ..items = [resource('Disabled', source: 'matched', enabled: false)];
    await showInventory(t, core);
    expect(
      find.descendant(
        of: statusFor('Disabled'),
        matching: find.text(tr('已禁用')),
      ),
      findsOneWidget,
    );
    expect(find.text(tr('本地已识别')), findsOneWidget);
    expect(find.text(tr('来源已匹配')), findsOneWidget);
    expect(t.widget<Switch>(find.byType(Switch)).value, isFalse);
  });

  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
      '$language narrow status cells keep secondary labels together',
      (t) async {
        AppLanguage.instance.select(language);
        await t.pumpWidget(
          MaterialApp(
            home: Scaffold(
              body: Align(
                alignment: Alignment.topLeft,
                child: SizedBox(
                  width: 100,
                  child: ResourceDetectionStatus(resource('Narrow')),
                ),
              ),
            ),
          ),
        );
        final local = find.text(tr('本地已识别'));
        final source = find.text(tr('来源未查询'));
        // Each phrase occupies one line. The source phrase moves as a whole
        // instead of wrapping its final character onto an extra line.
        expect(t.getSize(local).height, lessThanOrEqualTo(15));
        expect(t.getSize(source).height, lessThanOrEqualTo(15));
        expect(
          t.getTopLeft(source).dy,
          greaterThanOrEqualTo(t.getBottomLeft(local).dy),
        );
        final tooltip = t.widget<Tooltip>(find.byType(Tooltip));
        expect(tooltip.message, contains(tr('本地已识别')));
        expect(tooltip.message, contains(tr('来源未查询')));
        expect(t.takeException(), isNull);
      },
    );
  }

  for (final kind in ['shader', 'resourcepack']) {
    testWidgets('$kind keeps installed and invalid pack statuses', (t) async {
      final core = InventoryCore()
        ..items = [
          {'filename': 'Good.zip', 'name': 'Good pack', 'version': '1'},
          {
            'filename': 'Broken.zip',
            'name': 'Broken pack',
            'problem': '压缩包内容损坏。',
          },
        ];
      await showInventory(t, core, kind: kind);
      expect(find.text('Good pack'), findsOneWidget);
      expect(find.text('Broken pack'), findsOneWidget);
      expect(find.text(tr('已安装')), findsOneWidget);
      expect(find.text(tr('未识别')), findsOneWidget);
      expect(find.byType(ResourceDetectionStatus), findsNothing);
      expect(find.text(tr('本地元数据未知')), findsNothing);
      final problem = t.widget<Tooltip>(
        find.ancestor(of: find.text(tr('未识别')), matching: find.byType(Tooltip)),
      );
      expect(problem.message, contains(nativeText('压缩包内容损坏。')));
      expect(t.takeException(), isNull);
    });
  }

  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
      '$language source failure preserves rows and retries enrichment',
      (t) async {
        AppLanguage.instance.select(language);
        final core = InventoryCore()
          ..sourceError = {
            'code': 'http_503',
            'http_status': 503,
            'message': '服务暂时不可用',
            'url': 'https://api.modrinth.com/v2/version_files',
          };
        await showInventory(t, core);
        expect(find.text('Demo'), findsOneWidget);
        expect(find.text('http_503 · HTTP 503'), findsOneWidget);
        expect(find.text(tr('来源查询失败，本地资源仍可查看。')), findsOneWidget);
        core.sourceError = null;
        core.items = [resource('Enriched', source: 'matched')];
        core.pendingEnrich = Completer<void>();
        await t.tap(find.text(tr('重试来源查询')));
        await t.pump();
        expect(find.text('Demo'), findsOneWidget);
        expect(find.byType(LinearProgressIndicator), findsOneWidget);
        core.pendingEnrich!.complete();
        await t.pumpAndSettle();
        expect(find.text('http_503 · HTTP 503'), findsNothing);
        expect(find.text('Enriched'), findsOneWidget);
        expect(core.requests.map((r) => r['op']), [
          'instance.inventory',
          'mods.enrich',
        ]);
        expect(t.takeException(), isNull);
      },
    );
  }

  testWidgets(
    'enrichment exception is inline and refresh bypasses scan cache',
    (t) async {
      final core = InventoryCore()..throwEnrich = true;
      await showInventory(t, core);
      expect(core.requests.first.containsKey('fresh'), isFalse);
      await t.tap(find.text(tr('补全来源信息')));
      await t.pumpAndSettle();
      expect(find.text('http_429 · HTTP 429'), findsOneWidget);
      expect(find.text('Demo'), findsOneWidget);
      await t.tap(find.byTooltip(tr('刷新')));
      await t.pumpAndSettle();
      expect(core.requests.last['op'], 'instance.inventory');
      expect(core.requests.last['fresh'], isTrue);
      expect(find.text('http_429 · HTTP 429'), findsNothing);
      expect(find.text('Demo'), findsOneWidget);
    },
  );

  test('legacy rows degrade conservatively and preserve file validity', () {
    expect(ResourceDetection({'known': true}).compatibility, 'needs_review');
    final unknown = ResourceDetection({'project_id': 'website-project'});
    expect(unknown.local, 'unknown');
    expect(unknown.source, 'matched');
    expect(unknown.compatibility, 'needs_review');
    final invalid = ResourceDetection({'valid_archive': false});
    expect(invalid.local, 'invalid');
    expect(invalid.needsAttention, isTrue);
  });

  for (final language in ['zh_Hant', 'en']) {
    test('$language dependency diagnostics preserve names, IDs and paths', () {
      AppLanguage.instance.select(language);
      const name = '未知文件 与 自定义{1}.jar';
      const id = '元数据未知{0}';
      const path = '/custom/未知文件 与 自定义{1}.jar';
      for (final prefix in [
        '模组元数据字段必须是字符串：',
        '依赖含未支持的字段：',
        'Forge 加载元数据含未支持的字段：',
        '语言加载器版本尚未确认：',
        '加载器内置组件及其版本尚未确认：',
        '声明的内嵌 JAR 不存在或为空：',
        'JarJar 内嵌 JAR 损坏：',
        '回滚未完成，旧文件保留在：',
      ]) {
        expect(nativeText('$prefix$path'), '${tr(prefix)}$path');
      }
      for (final separator in [
        ' 缺少前置：',
        ' 前置版本不符合：',
        ' 依赖组合无法同时满足：',
        ' 语言加载器版本不符合：',
      ]) {
        expect(nativeText('$name$separator$id'), '$name${tr(separator)}$id');
      }
      expect(nativeText('$name 与 $id 不兼容。'), tr('{0} 与 {1} 不兼容。', [name, id]));
      expect(
        nativeText('$name 与 $id 不能同时启用。'),
        tr('{0} 与 {1} 不能同时启用。', [name, id]),
      );
      expect(
        nativeText('$name：加载器不匹配（fabric / forge）。'),
        tr('{0}：加载器不匹配（{1}）。', [name, 'fabric / forge']),
      );
      expect(
        nativeText('内嵌组件的加载选择尚未确定：$name 与 $id 的网站不兼容声明需由实际加载器确认。'),
        tr('内嵌组件的加载选择尚未确定：{0} 与 {1} 的网站不兼容声明需由实际加载器确认。', [name, id]),
      );
      expect(nativeText('$name：模组元数据文件为空。'), '$name：${tr('模组元数据文件为空。')}');
      expect(
        nativeText('$name：[json.exception] unexpected "元数据未知"'),
        '$name：[json.exception] unexpected "元数据未知"',
      );
      expect(
        nativeText('Quilt 官方兼容性提醒：$name。未提供精确文件哈希，需核对具体构建。'),
        tr('Quilt 官方兼容性提醒：{0}。未提供精确文件哈希，需核对具体构建。', [name]),
      );
    });

    testWidgets('$language structured graph warnings remain reviewable', (
      t,
    ) async {
      AppLanguage.instance.select(language);
      const name = '未知文件 与 自定义{1}';
      const id = 'custom_dependency{0}';
      const message = '$name 依赖组合无法同时满足：$id';
      final core = InventoryCore()
        ..items = [
          resource(
            name,
            compatibility: 'conflict',
            issues: [
              {
                'severity': 'error',
                'code': 'dependency_selection_conflict',
                'message': message,
                'files': ['$name.jar', 'Other.jar'],
              },
            ],
          )..['id'] = 'literal_name_fixture',
        ];
      await showInventory(t, core);
      expect(find.text(name), findsOneWidget);
      expect(find.text(tr('存在冲突')), findsOneWidget);
      final tooltip = t.widget<Tooltip>(
        find.descendant(of: statusFor(name), matching: find.byType(Tooltip)),
      );
      expect(tooltip.message, contains('dependency_selection_conflict'));
      expect(tooltip.message, contains('$name.jar, Other.jar'));
      expect(tooltip.message, contains('$name${tr(' 依赖组合无法同时满足：')}$id'));
      expect(t.takeException(), isNull);
    });

    test(
      '$language metadata and graph limits have localized review messages',
      () {
        AppLanguage.instance.select(language);
        for (final source in [
          '模组元数据版本未受完整支持。',
          'Quilt 插件可更改模组发现及依赖规则，需手动确认。',
          'JarJar 的跨 JAR 版本协商尚未完整模拟，内嵌候选已静态检查。',
          '依赖检查达到复杂度上限，请使用加载器日志进一步确认。',
          '实例存在依赖冲突，尚未完成此组件的联合兼容性核对。',
          '依赖环境条件尚未完整核对，需手动确认。',
          '内嵌组件缺少完整依赖元数据，需由实际加载器确认。',
          '依赖元数据查询失败，请重新生成安装计划。',
          '所选文件与网站版本记录不一致，请重新确认。',
          '所选更新之间存在作者声明的不兼容关系。',
          '请先在版本组件中移除 OptiFine，再安装 Iris / Sodium。',
        ]) {
          final translated = nativeText(source);
          expect(translated, isNot(source), reason: source);
          if (language == 'en') {
            expect(translated, isNot(matches(RegExp(r'[\u4e00-\u9fff]'))));
          }
        }
      },
    );

    test('$language native range warnings retain literal dependency IDs', () {
      AppLanguage.instance.select(language);
      // Chinese names and placeholder-like text belong to the dependency ID.
      const id = '未知文件 与 自定义{1}.jar';
      for (final prefix in [
        '运行环境版本未知，无法完整检查：',
        '无法完整比较版本范围：',
        '无法完整检查冲突版本范围：',
      ]) {
        final message = nativeText('$prefix$id');
        expect(message, endsWith(id));
        expect(message, isNot(startsWith(prefix)));
        if (language == 'en') {
          expect(
            message.substring(0, message.length - id.length),
            isNot(matches(RegExp(r'[\u4e00-\u9fff]'))),
          );
        }
      }
      final error = errorMessage({
        'code': 'invalid_identity',
        'http_status': 503,
        'message': '模组来源返回格式异常。',
      });
      expect(
        error,
        language == 'en'
            ? 'The mod source service returned an invalid response.'
            : '模組來源傳回的格式異常。',
      );
    });
  }
}
