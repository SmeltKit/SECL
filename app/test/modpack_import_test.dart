// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/app_widgets.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/download_center.dart';
import 'package:secl/instance_library.dart';
import 'package:secl/localization.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/modpack_import.dart';
import 'package:secl/resource_engine.dart';

class PendingImportCore implements LauncherCore {
  final started = Completer<void>();
  final completed = Completer<Data>();
  @override
  Future<Data> call(Data request) async {
    if (request['action'] == 'engine') {
      started.complete();
      return completed.future;
    }
    return {'busy': false};
  }
}

class ModpackCore implements LauncherCore {
  final requests = <Data>[];
  bool failInspect = false, failPlan = false, failApply = false;
  bool waitApply = false, busy = false;
  dynamic result;
  Data? error;
  static const requiredFile = {
    'path': 'mods/required.jar',
    'client': 'required',
    'size': 2048,
    'sha512': 'required-hash',
    'urls': ['https://example.invalid/required.jar']
  };
  static const optionalFile = {
    'path': 'mods/optional.jar',
    'client': 'optional',
    'size': 4096,
    'sha512': 'optional-hash',
    'urls': ['https://example.invalid/optional.jar']
  };
  static const pack = {
    'name': '中文整合包',
    'version_id': 'v1.2',
    'summary': '整合包说明',
    'minecraft': '1.20.1',
    'loader': 'fabric',
    'loader_version': '0.16.10'
  };

  List<Data> operations(String op) => requests
      .where((r) => (r['request'] as Map?)?['op'] == op)
      .map((r) => Map<String, dynamic>.from(r['request'] as Map))
      .toList();

  @override
  Future<Data> call(Data request) async {
    requests.add(request);
    if (request['action'] == 'cancel') busy = false;
    if (request['action'] == 'clear_error') error = null;
    if (request['action'] == 'engine') {
      final q = request['request'] as Map;
      error = null;
      switch (q['op']) {
        case 'platform':
          result = {'os': 'osx', 'arch': 'aarch64'};
        case 'game.versions':
          result = {'versions': []};
        case 'instances.scan':
          result = {'instances': []};
        case 'modpacks.inspect':
          result = {
            ...pack,
            'path': q['path'],
            'archive_sha512': 'archive-hash',
            'files': [
              requiredFile,
              optionalFile,
              {'path': 'mods/server.jar', 'client': 'unsupported', 'size': 512}
            ],
            'overrides': [
              {'entry': 'overrides/config/example.json', 'size': 10}
            ],
            'server_files': 1,
            'optional_count': 1,
            'override_count': 1
          };
          if (failInspect) {
            error = {'code': 'bad_archive', 'message': '整合包校验失败'};
          }
        case 'modpacks.plan':
          final optional = List<String>.from(q['optional_files'] as List);
          result = {
            'path': q['path'],
            'archive_sha512': 'archive-hash',
            'id': q['id'],
            'name': q['name'],
            'root': q['root'],
            'directory': '${q['root']}/instances/${q['id']}',
            'pack': pack,
            'files': [
              requiredFile,
              if (optional.contains('mods/optional.jar')) optionalFile
            ],
            'optional_files': optional,
            'skipped_server_count': 1,
            'override_count': 1,
            'total_download_bytes': 2048 + (optional.isNotEmpty ? 4096 : 0),
            'resumable': true
          };
          if (failPlan) {
            error = {'code': 'target_exists', 'message': '目标实例已存在'};
          }
        case 'modpacks.apply':
          final plan = q['plan'] as Map;
          result = {
            'id': plan['id'],
            'root': plan['root'],
            'directory': plan['directory'],
            'metadata': {'name': plan['name']},
            'java': {'path': '/isolated/java/bin/java'},
            'warnings': []
          };
          busy = waitApply;
          if (failApply) {
            error = {
              'code': 'http_503',
              'message': '下载服务返回 HTTP 503',
              'http_status': 503,
              'url': 'https://example.invalid/required.jar'
            };
          }
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

class CurseForgeModpackCore extends ModpackCore {
  bool configured = true;
  bool supplementDirectoryExists = true;
  static const optional = {
    'path': '123/456',
    'client': 'optional',
    'project_id': '123',
    'file_id': '456',
  };
  static const website =
      'https://www.curseforge.com/minecraft/mc-mods/example/files/456';
  static const archive = '/isolated/CurseForge.zip';
  static const supplement = '/isolated/Downloads';
  static final resolvedFile = {
    ...optional,
    'path': 'mods/example.jar',
    'filename': 'example.jar',
    'title': 'Example Mod',
    'website_url': website,
  };
  static const metadata = {...ModpackCore.pack, 'format': 'curseforge'};

  @override
  Future<Data> call(Data request) async {
    final snapshot = await super.call(request);
    final operation = request['request'] as Map?;
    switch (operation?['op']) {
      case 'modpacks.inspect':
        return {
          ...snapshot,
          'result': {
            ...snapshot['result'] as Map,
            ...metadata,
            'files': [optional],
          }
        };
      case 'modpacks.plan':
        if (!configured) {
          return {
            ...snapshot,
            'error': {
              'code': 'curseforge_key_missing',
              'message': '此构建未配置 CurseForge 访问。',
            }
          };
        }
        final hasOptional =
            (operation!['optional_files'] as List).contains(optional['path']);
        final supplemented = operation['supplement_directory'] == supplement;
        if (supplemented && !supplementDirectoryExists) {
          return {
            ...snapshot,
            'error': {
              'code': 'invalid_supplement',
              'message': '请选择有效的整合包补充文件目录。',
            }
          };
        }
        return {
          ...snapshot,
          'result': {
            ...snapshot['result'] as Map,
            'pack': metadata,
            'files': [
              ModpackCore.requiredFile,
              if (hasOptional && supplemented)
                {...resolvedFile, 'supplemental': true}
            ],
            'total_download_bytes': 2048,
            'resumable': false,
            'blocked_files': [
              if (hasOptional && !supplemented)
                {...resolvedFile, 'reason': 'manual_download_required'},
            ],
          }
        };
      case 'resource.open':
        return {
          'busy': false,
          'result': {'opened': true}
        };
      default:
        return snapshot;
    }
  }
}

Future<void> showImport(WidgetTester t, ModpackCore core,
    {Future<void> Function(Data)? onInstalled, bool usePicker = false}) async {
  t.view.physicalSize = const Size(880, 640);
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
  final engine = ResourceEngine(core);
  addTearDown(engine.dispose);
  await t.pumpWidget(MaterialApp(
      theme: ThemeData(useMaterial3: true),
      home: Scaffold(
          body: Builder(
              builder: (context) => TextButton(
                  onPressed: () async {
                    if (usePicker) {
                      await showModpackImport(context, engine,
                          root: '/isolated/game',
                          onInstalled: onInstalled ?? (_) async {});
                      return;
                    }
                    final result = await showDialog<Data>(
                        context: context,
                        barrierDismissible: false,
                        builder: (_) => ModpackImportDialog(
                            engine: engine,
                            path: '/isolated/中文整合包.mrpack',
                            root: '/isolated/game'));
                    if (result != null) await onInstalled?.call(result);
                  },
                  child: const Text('open'))))));
  await t.tap(find.text('open'));
  await t.pumpAndSettle();
}

Future<void> reviewPlan(WidgetTester t) async {
  await t.tap(find.byKey(const Key('confirmModpackImport')));
  await t.pumpAndSettle();
  expect(find.byKey(const Key('modpackReview')), findsOneWidget);
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));

  test('late cancellation only preserves a committed modpack result', () async {
    for (final scenario in [
      {'op': 'modpacks.apply', 'committed': true, 'succeeds': true},
      {'op': 'modpacks.apply', 'committed': false, 'succeeds': false},
      {'op': 'game.install', 'committed': true, 'succeeds': false}
    ]) {
      final core = PendingImportCore();
      final engine = ResourceEngine(core);
      addTearDown(engine.dispose);
      final result = {
        'id': 'pack-committed',
        'root': '/isolated/game',
        'modpack': {'archive_sha512': 'verified-hash'},
        'committed': scenario['committed']
      };
      final pending = engine.run({'op': scenario['op']});
      await core.started.future;
      await engine.cancel();
      final assertion = scenario['succeeds'] == true
          ? expectLater(pending, completion(same(result)))
          : expectLater(
              pending,
              throwsA(isA<EngineFailure>().having(
                  (error) => error.details['code'], 'code', 'cancelled')));
      core.completed.complete({'busy': false, 'result': result});
      await assertion;
      expect(engine.history.single['status'],
          scenario['succeeds'] == true ? 'complete' : 'cancelled');
      expect(engine.active, isFalse);
    }
  });

  test('modpack UI translations preserve literal placeholder values', () {
    AppLanguage.instance.select('en');
    expect(tr('导入整合包'), 'Import modpack');
    expect(tr('{0} 个文件 · {1}', ['中文{1}', '6 KiB']), '中文{1} files · 6 KiB');
    AppLanguage.instance.select('zh_Hant');
    expect(tr('确认导入整合包'), '確認匯入整合包');
    expect(tr('覆盖文件：{0}；仅服务端文件将跳过。', [2]), '覆寫檔案：2；僅伺服器端檔案將略過。');
  });

  test('modpack diagnostics translate prefixes and retain literal fields', () {
    AppLanguage.instance.select('en');
    expect(nativeText('整合包缺少有效字段：下载文件'),
        'The modpack is missing a valid field: 下载文件');
    expect(nativeText('整合包字段无效：设置'), 'Invalid modpack field: 设置');
    expect(nativeText('整合包文件哈希无效：sha512'), 'Invalid modpack file hash: sha512');
    expect(nativeText('整合包要求尚不支持的依赖：取消'),
        'The modpack requires an unsupported dependency: 取消');
    expect(nativeText('整合包包含不支持的环境字段：开始'),
        'The modpack contains an unsupported environment field: 开始');
    expect(nativeText('整合包下载地址或重定向域名不在允许列表中。'),
        'The modpack download or redirect domain is not on the allowlist.');
    const details = '下载文件 缺少前置：设置\n取消 与 开始 不兼容。';
    expect(nativeText('整合包包含依赖冲突：$details'),
        'The modpack contains dependency conflicts: $details');
    expect(nativeText('下载并校验整合包文件'), 'Downloading and verifying modpack files');
    expect(nativeText('整合包根目录缺少 manifest.json。'),
        'The modpack root is missing manifest.json.');
    expect(nativeText('CurseForge 文件标识无效：fileID'),
        'Invalid CurseForge file identifier: fileID');
    AppLanguage.instance.select('zh_Hant');
    expect(nativeText('整合包字段无效：下载文件'), '整合包欄位無效：下载文件');
    expect(nativeText('整合包 ZIP 文件头与目录不一致。'), '整合包 ZIP 檔案標頭與目錄不一致。');
    expect(nativeText('准备独立游戏实例'), '準備獨立遊戲實例');
  });

  testWidgets('inspect failure stays in dialog and retries only on request',
      (t) async {
    final core = ModpackCore()..failInspect = true;
    await showImport(t, core);
    expect(find.byType(ProblemPanel), findsOneWidget);
    await t.pump(const Duration(seconds: 1));
    expect(core.operations('modpacks.inspect'), hasLength(1));
    expect(core.operations('modpacks.plan'), isEmpty);
    core.failInspect = false;
    await t.tap(find.text('重试'));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsNothing);
    expect(find.byKey(const Key('modpackName')), findsOneWidget);
    expect(core.operations('modpacks.inspect'), hasLength(2));
    expect(t.takeException(), isNull);
  });

  void mockArchivePicker(WidgetTester t) {
    const channel = MethodChannel('plugins.flutter.io/file_selector');
    t.binding.defaultBinaryMessenger.setMockMethodCallHandler(channel,
        (call) async {
      switch (call.method) {
        case 'openFile':
          final arguments = call.arguments as Map;
          final groups = arguments['acceptedTypeGroups'] as List;
          expect((groups.single as Map)['extensions'], ['mrpack', 'zip']);
          return [CurseForgeModpackCore.archive];
        case 'getDirectoryPath':
          return CurseForgeModpackCore.supplement;
      }
      throw UnimplementedError(call.method);
    });
    addTearDown(() => t.binding.defaultBinaryMessenger
        .setMockMethodCallHandler(channel, null));
  }

  testWidgets(
      'CurseForge ZIP review opens exact file pages and checks local files',
      (t) async {
    final core = CurseForgeModpackCore();
    mockArchivePicker(t);
    await showImport(t, core, usePicker: true);
    expect(core.operations('modpacks.inspect').single['path'],
        CurseForgeModpackCore.archive);
    final optional = find.byKey(const Key('modpack-file-123/456'));
    expect(t.widget<CheckboxListTile>(optional).value, isFalse);
    expect(find.text('项目 123 · 文件 456'), findsOneWidget);
    expect(find.text('可选 · 0 KiB'), findsNothing);
    await t.ensureVisible(optional);
    await t.tap(optional);
    await reviewPlan(t);

    final confirm = find.byKey(const Key('confirmModpackImport'));
    expect(t.widget<FilledButton>(confirm).onPressed, isNull);
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(find.text('1 个文件需要处理'), findsOneWidget);
    expect(find.text('Example Mod'), findsOneWidget);
    expect(find.text('example.jar'), findsWidgets);
    await t.tap(find.text('在官网打开'));
    await t.pumpAndSettle();
    expect(core.operations('resource.open').single['url'],
        CurseForgeModpackCore.website);

    final choose = find.byKey(const Key('chooseModpackSupplementDirectory'));
    core.supplementDirectoryExists = false;
    await t.ensureVisible(choose);
    await t.tap(choose);
    await t.pumpAndSettle();
    expect(find.text('请选择有效的整合包补充文件目录。'), findsOneWidget);
    await t.tap(find.text('重试'));
    await t.pumpAndSettle();
    expect(
        core.operations('modpacks.plan').last['supplement_directory'], isNull);
    expect(find.text('1 个文件需要处理'), findsOneWidget);

    core.supplementDirectoryExists = true;
    await t.ensureVisible(choose);
    await t.tap(choose);
    await t.pumpAndSettle();
    expect(core.operations('modpacks.plan').last['supplement_directory'],
        CurseForgeModpackCore.supplement);
    expect(find.text('1 个文件需要处理'), findsNothing);
    expect(find.text('已校验的本地文件'), findsOneWidget);
    expect(find.text('1 个文件 · 2 KiB'), findsOneWidget);
    expect(find.text('导入中断后可重试，已校验的文件会保留并继续使用。'), findsNothing);
    expect(t.widget<FilledButton>(confirm).onPressed, isNotNull);
    expect(t.takeException(), isNull);
  });

  testWidgets('a keyless build gives an import error without offering a retry',
      (t) async {
    final core = CurseForgeModpackCore()..configured = false;
    mockArchivePicker(t);
    AppLanguage.instance.select('en');
    await showImport(t, core, usePicker: true);
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pumpAndSettle();
    expect(find.text('This build has no CurseForge access configured.'),
        findsOneWidget);
    expect(find.text('Retry'), findsNothing);
    expect(find.text('Back to edit'), findsOneWidget);
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(t.takeException(), isNull);
  });

  testWidgets(
      'required files stay selected and optional files can be deselected',
      (t) async {
    final core = ModpackCore();
    await showImport(t, core);
    final required = find.byKey(const Key('modpack-file-mods/required.jar'));
    final optional = find.byKey(const Key('modpack-file-mods/optional.jar'));
    expect(t.widget<CheckboxListTile>(required).value, isTrue);
    expect(t.widget<CheckboxListTile>(required).onChanged, isNull);
    expect(t.widget<CheckboxListTile>(optional).value, isFalse);
    expect(find.text('mods/server.jar'), findsNothing);
    await t.ensureVisible(optional);
    await t.tap(optional);
    await t.pumpAndSettle();
    expect(t.widget<CheckboxListTile>(optional).value, isTrue);
    await t.tap(optional);
    await t.pumpAndSettle();
    await reviewPlan(t);
    expect(core.operations('modpacks.plan').single['optional_files'], isEmpty);
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(t.takeException(), isNull);
  });

  testWidgets('cancel before confirmation never applies or invokes completion',
      (t) async {
    final core = ModpackCore();
    bool completed = false;
    await showImport(t, core, onInstalled: (_) async => completed = true);
    await reviewPlan(t);
    await t.tap(find.byKey(const Key('cancelModpackImport')));
    await t.pumpAndSettle();
    expect(find.byType(ModpackImportDialog), findsNothing);
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(completed, isFalse);
    expect(t.takeException(), isNull);
  });

  testWidgets('a failed plan allows editing the target before trying again',
      (t) async {
    final core = ModpackCore()..failPlan = true;
    await showImport(t, core);
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsOneWidget);
    expect(core.operations('modpacks.plan'), hasLength(1));
    await t.tap(find.text('返回修改'));
    await t.pumpAndSettle();
    await t.enterText(find.byKey(const Key('modpackRoot')), '/isolated/other');
    core.failPlan = false;
    await reviewPlan(t);
    expect(core.operations('modpacks.plan'), hasLength(2));
    expect(core.operations('modpacks.plan').last['root'], '/isolated/other');
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(t.takeException(), isNull);
  });

  testWidgets(
      'plan reviews chosen target, versions, download size and overrides',
      (t) async {
    final core = ModpackCore();
    await showImport(t, core);
    await t.enterText(find.byKey(const Key('modpackName')), '我的整合包');
    await t.enterText(find.byKey(const Key('modpackRoot')), '/isolated/新目录');
    final optional = find.byKey(const Key('modpack-file-mods/optional.jar'));
    await t.ensureVisible(optional);
    await t.tap(optional);
    await t.pumpAndSettle();
    await reviewPlan(t);
    final request = core.operations('modpacks.plan').single;
    expect(request['name'], '我的整合包');
    expect(request['id'], matches(RegExp(r'^pack-[0-9]+$')));
    expect(request['root'], '/isolated/新目录');
    expect(request['optional_files'], ['mods/optional.jar']);
    expect(find.text('我的整合包'), findsOneWidget);
    expect(
        find.textContaining('/isolated/新目录/instances/pack-'), findsOneWidget);
    expect(find.textContaining('Minecraft 1.20.1 · fabric 0.16.10'),
        findsOneWidget);
    expect(find.text('2 个文件 · 6 KiB'), findsOneWidget);
    expect(find.text('覆盖文件'), findsOneWidget);
    expect(find.text('跳过的服务端文件'), findsOneWidget);
    expect(core.operations('modpacks.apply'), isEmpty);
    expect(t.takeException(), isNull);
  });

  testWidgets('successful apply delivers installed target once', (t) async {
    final core = ModpackCore();
    final completed = <Data>[];
    await showImport(t, core, onInstalled: (result) async {
      completed.add(result);
    });
    await reviewPlan(t);
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pumpAndSettle();
    expect(completed, hasLength(1));
    expect(completed.single['root'], '/isolated/game');
    expect(
        completed.single['id'], core.operations('modpacks.plan').single['id']);
    expect(find.byType(ModpackImportDialog), findsNothing);
    expect(core.operations('modpacks.apply'), hasLength(1));
    expect(t.takeException(), isNull);
  });

  testWidgets('failed apply retries the same reviewed plan without looping',
      (t) async {
    final core = ModpackCore()..failApply = true;
    int completed = 0;
    await showImport(t, core, onInstalled: (_) async => completed++);
    await reviewPlan(t);
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pumpAndSettle();
    expect(find.byType(ProblemPanel), findsOneWidget);
    expect(completed, 0);
    await t.pump(const Duration(seconds: 1));
    expect(core.operations('modpacks.apply'), hasLength(1));
    final first = core.operations('modpacks.apply').single['plan'];
    core.failApply = false;
    await t.tap(find.text('重试'));
    await t.pumpAndSettle();
    expect(core.operations('modpacks.apply'), hasLength(2));
    expect(core.operations('modpacks.apply').last['plan'], same(first));
    expect(core.operations('modpacks.plan'), hasLength(1));
    expect(completed, 1);
    expect(t.takeException(), isNull);
  });

  testWidgets('cancelling apply retains its plan for explicit resume',
      (t) async {
    final core = ModpackCore()..waitApply = true;
    bool completed = false;
    await showImport(t, core, onInstalled: (_) async => completed = true);
    await reviewPlan(t);
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pump(const Duration(milliseconds: 150));
    expect(find.byType(LinearProgressIndicator), findsOneWidget);
    final first = core.operations('modpacks.apply').single['plan'];
    await t.tap(find.byKey(const Key('cancelModpackImport')));
    await t.pump(const Duration(milliseconds: 300));
    await t.pumpAndSettle();
    expect(core.requests.any((r) => r['action'] == 'cancel'), isTrue);
    expect(completed, isFalse);
    expect(find.byKey(const Key('modpackReview')), findsOneWidget);
    expect(find.text(tr('操作已取消，可重新确认并继续导入')), findsOneWidget);
    core.waitApply = false;
    await t.tap(find.byKey(const Key('confirmModpackImport')));
    await t.pumpAndSettle();
    expect(core.operations('modpacks.apply'), hasLength(2));
    expect(core.operations('modpacks.apply').last['plan'], same(first));
    expect(completed, isTrue);
    expect(find.byType(ModpackImportDialog), findsNothing);
    expect(t.takeException(), isNull);
  });

  testWidgets('English download-size clarification fits the minimum window',
      (t) async {
    AppLanguage.instance.select('en');
    await showImport(t, ModpackCore());
    await reviewPlan(t);
    final clarification = find.text(
        'The download size covers modpack files only, excluding the game, Java and loader.');
    await t.ensureVisible(clarification);
    await t.pumpAndSettle();
    expect(clarification, findsOneWidget);
    expect(t.getRect(clarification).top, greaterThanOrEqualTo(0));
    expect(t.getRect(clarification).bottom, lessThanOrEqualTo(640));
    expect(t.takeException(), isNull);
  });

  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language import entries fit the minimum content width',
        (t) async {
      AppLanguage.instance.select(language);
      t.view.physicalSize = const Size(880, 640);
      t.view.devicePixelRatio = 1;
      addTearDown(t.view.resetPhysicalSize);
      addTearDown(t.view.resetDevicePixelRatio);
      final engine = ResourceEngine(ModpackCore());
      addTearDown(engine.dispose);
      int opened = 0;
      Future<void> show(Widget child) async {
        await t.pumpWidget(MaterialApp(
            theme: ThemeData(useMaterial3: true),
            home: Scaffold(
                body: SizedBox(width: 656, height: 640, child: child))));
        await t.pumpAndSettle();
      }

      await show(InstanceLibrary(
          engine: engine,
          root: '/isolated/game',
          onOpen: (_, __) {},
          onInstall: () {},
          onImport: () => opened++,
          onLaunch: (_) {}));
      await t.tap(find.byKey(const Key('importModpackLibrary')));
      expect(opened, 1);
      expect(t.takeException(), isNull);
      await show(DownloadCenter(
          engine: engine,
          root: '/isolated/game',
          javaDirectory: '/isolated/java',
          javas: const [],
          source: 'official',
          onSource: (_) async {},
          onInstalled: (_, __) async {},
          onImport: () => opened++));
      await t.tap(find.byKey(const Key('importModpackDownload')));
      expect(opened, 2);
      expect(t.takeException(), isNull);
    });
  }
}
