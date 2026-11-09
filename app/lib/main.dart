import 'appearance.dart';
import 'cover_art.dart';
import 'home_scene.dart';
import 'launcher_sidebar.dart';
import 'home_instance_shelf.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'localization.dart';
import 'dart:async';
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart';
import 'package:file_selector/file_selector.dart';
import 'core_bridge.dart';
import 'app_widgets.dart';
import 'resource_engine.dart';
import 'resource_market.dart';
import 'download_center.dart';
import 'modpack_import.dart';
import 'skin_avatar.dart';
import 'instance_library.dart';
import 'instance_page.dart';
import 'java_manager.dart';
import 'account_manager.dart';
import 'download_button.dart';
import 'launcher_icons.dart';
import 'about_project.dart';
import 'settings_page.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  LicenseRegistry.addLicense(() async* {
    for (final entry in {
      'SECL / M²CL': 'LICENSE',
      'M²CL additional terms': 'ADDITIONAL_TERMS.md',
      'nlohmann/json': 'json-MIT.txt',
      'TinyXML2': 'tinyxml2-Zlib.txt',
      'toml++ 3.4.0': 'tomlplusplus-MIT.txt'
    }.entries) {
      yield LicenseEntryWithLineBreaks([entry.key],
          await rootBundle.loadString('assets/legal/${entry.value}'));
    }
  });
  WidgetsBinding.instance.addObserver(AppLanguage.instance);
  WidgetsBinding.instance.addObserver(AppAppearance.instance);
  runApp(SeclApp(core: NativeCore()));
}

class SeclApp extends StatelessWidget {
  const SeclApp({super.key, required this.core});
  final LauncherCore core;
  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation:
          Listenable.merge([AppLanguage.instance, AppAppearance.instance]),
      builder: (context, _) => MaterialApp(
          locale: AppLanguage.instance.locale,
          supportedLocales: AppLanguage.supported,
          localizationsDelegates: GlobalMaterialLocalizations.delegates,
          localeListResolutionCallback: (locales, _) =>
              AppLanguage.resolve(locales ?? []),
          title: 'SECL',
          debugShowCheckedModeBanner: false,
          theme: ThemeData(
            useMaterial3: true,
            brightness: AppAppearance.instance.dark
                ? Brightness.dark
                : Brightness.light,
            scaffoldBackgroundColor: seclColor(0xFFF5F7FA),
            fontFamily: AppLanguage.instance.code == 'en'
                ? (Platform.isWindows ? 'Segoe UI' : null)
                : Platform.isWindows
                    ? (AppLanguage.instance.code == 'zh_Hant'
                        ? 'Microsoft JhengHei UI'
                        : 'Microsoft YaHei UI')
                    : Platform.isMacOS
                        ? (AppLanguage.instance.code == 'zh_Hant'
                            ? 'PingFang TC'
                            : 'PingFang SC')
                        : (AppLanguage.instance.code == 'zh_Hant'
                            ? 'Noto Sans CJK TC'
                            : 'Noto Sans CJK SC'),
            colorScheme: ColorScheme.fromSeed(
                brightness: AppAppearance.instance.dark
                    ? Brightness.dark
                    : Brightness.light,
                seedColor: accent,
                primary: accent,
                surface: panelSurface),
            textTheme: TextTheme(
                bodyMedium: TextStyle(fontSize: 14, color: ink),
                bodySmall: TextStyle(fontSize: 12, color: muted)),
            dialogTheme: DialogTheme(
                backgroundColor: panelSurface,
                surfaceTintColor: Colors.transparent,
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(12)),
                titleTextStyle: TextStyle(
                    fontSize: 20, fontWeight: FontWeight.w600, color: ink)),
            dividerTheme: DividerThemeData(color: line, space: 1),
            inputDecorationTheme: InputDecorationTheme(
                filled: true,
                fillColor: panelSurface,
                isDense: true,
                contentPadding:
                    const EdgeInsets.symmetric(horizontal: 14, vertical: 14),
                border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(9),
                    borderSide: BorderSide(color: line)),
                focusedBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(9),
                    borderSide: BorderSide(color: accent, width: 1.5)),
                enabledBorder: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(9),
                    borderSide: BorderSide(color: line))),
            outlinedButtonTheme: OutlinedButtonThemeData(
                style: OutlinedButton.styleFrom(
                    foregroundColor: ink,
                    side: BorderSide(color: line),
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 14),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(9)))),
            filledButtonTheme: FilledButtonThemeData(
                style: FilledButton.styleFrom(
                    foregroundColor: AppAppearance.instance.dark
                        ? const Color(0xff132b3c)
                        : Colors.white,
                    padding: const EdgeInsets.symmetric(
                        horizontal: 16, vertical: 13),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(9)))),
            textButtonTheme: TextButtonThemeData(
                style: TextButton.styleFrom(
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(7)))),
            checkboxTheme: CheckboxThemeData(
                shape: RoundedRectangleBorder(
                    borderRadius: BorderRadius.circular(4))),
          ),
          home: LauncherHome(core: core)));
}

class LauncherHome extends StatefulWidget {
  const LauncherHome({super.key, required this.core});
  final LauncherCore core;
  @override
  State<LauncherHome> createState() => _LauncherHomeState();
}

class _LauncherHomeState extends State<LauncherHome> {
  Map<String, dynamic> state = {};
  String selected = '', java = '';
  String page = 'home';
  String settingsReturnPage = 'home';
  String downloadTab = 'game';
  Map<String, dynamic>? focusedInstance, marketTarget;
  String instanceTab = 'mod', marketQuery = '';
  late final ResourceEngine resources;
  static const accountActions = {
    'login',
    'logout',
    'accounts.restore',
    'accounts.select',
    'accounts.remove',
    'accounts.signout',
  };
  final pendingActions = <String>[];
  Future<void>? authenticationCancellation;
  bool polling = false, languageLoaded = false;
  Timer? timer;
  Map<String, dynamic>? localError;
  List<Map<String, dynamic>> get versions =>
      (state['versions'] as List? ?? []).cast<Map<String, dynamic>>();
  List<Map<String, dynamic>> get javas =>
      (state['javas'] as List? ?? []).cast<Map<String, dynamic>>();
  Map<String, dynamic>? get account =>
      state['account'] as Map<String, dynamic>?;
  bool get working => pendingActions.isNotEmpty;
  bool get accountPending => pendingActions.any(accountActions.contains);
  bool get authenticating =>
      accountPending && state['busy'] == true && state['stage'] != 'engine';
  bool get busy => working || resources.active || state['busy'] == true;
  bool get canLaunch =>
      !busy &&
      account?['owned'] == true &&
      selected.isNotEmpty &&
      java.isNotEmpty &&
      state['game_running'] != true;
  @override
  void initState() {
    super.initState();
    resources = ResourceEngine(widget.core)..addListener(resourceChanged);
    resources.onReplayed = (request, result) async {
      if (!mounted) return;
      final op = request['op'];
      if (result is Map &&
          (op == 'java.install' ||
              op == 'game.install' ||
              op == 'modpacks.apply')) {
        await installed(op == 'java.install' ? 'java' : 'game',
            Map<String, dynamic>.from(result));
      } else {
        await refreshInstance();
      }
    };
    unawaited(initialize());
    timer = Timer.periodic(const Duration(milliseconds: 700), (_) => poll());
  }

  @override
  void dispose() {
    timer?.cancel();
    resources.removeListener(resourceChanged);
    resources.dispose();
    super.dispose();
  }

  void resourceChanged() {
    if (mounted) setState(() {});
  }

  Future<void> initialize() async {
    await action({'action': 'poll'});
    if (state['root'] is String &&
        Directory(state['root'] as String).existsSync()) {
      await action({'action': 'scan', 'root': state['root']});
    } else {
      await action({'action': 'detect_java'});
    }
    if ((state['selected_account'] as String? ?? '').isNotEmpty &&
        (state['accounts'] as List? ?? []).any((a) =>
            a['id'] == state['selected_account'] && a['remembered'] == true)) {
      await action({'action': 'accounts.restore'});
    }
  }

  void accept(Map<String, dynamic> next) {
    if (!mounted) return;
    if (!languageLoaded) {
      languageLoaded = true;
      AppLanguage.instance.select(next['language'] as String? ?? 'system');
      AppAppearance.instance.select(next['appearance'] as String? ?? 'system',
          next['tone'] as String? ?? 'blue');
    }
    setState(() {
      state = next;
      if (selected.isEmpty || !versions.any((v) => v['id'] == selected)) {
        selected = versions
                .where((v) => v['available'] == true)
                .map((v) => v['id'] as String)
                .firstOrNull ??
            '';
      }
      final pinned =
          versions.where((v) => v['id'] == selected).firstOrNull?['java'];
      if (pinned is String &&
          pinned.isNotEmpty &&
          javas.any((j) => j['path'] == pinned)) java = pinned;
      if (java.isEmpty || !javas.any((j) => j['path'] == java)) {
        java = javas
                .map((j) => j['path'] as String)
                .where((p) => p == state['java'])
                .firstOrNull ??
            (javas.isNotEmpty ? javas.first['path'] as String : '');
      }
    });
  }

  Future<void> action(Map<String, dynamic> request) async {
    if (!mounted) return;
    final operation = request['action'] as String;
    if (operation == 'cancel') {
      if (authenticating) {
        await (authenticationCancellation ??= callCore(request));
      }
      return;
    }
    setState(() {
      pendingActions.add(operation);
      localError = null;
    });
    try {
      await resources.queueCoreCommand(() async {
        if (!mounted) return;
        await callCore(request);
        if (!accountActions.contains(operation)) return;
        while (mounted && authenticating) {
          await Future<void>.delayed(const Duration(milliseconds: 350));
          await authenticationCancellation;
          authenticationCancellation = null;
          if (mounted && authenticating) await callCore({'action': 'poll'});
        }
        await authenticationCancellation;
        authenticationCancellation = null;
      });
    } finally {
      if (mounted) setState(() => pendingActions.remove(operation));
    }
  }

  Future<void> callCore(Map<String, dynamic> request) async {
    try {
      accept(await widget.core.call(request));
    } catch (_) {
      if (mounted) {
        setState(() => localError = {
              'stage': 'core',
              'code': 'core_unavailable',
              'message': "无法加载 C++ 核心，请确认安装包完整，或重新构建本地核心。"
            });
      }
    }
  }

  Future<void> poll() async {
    if (polling || working || resources.active || !mounted) return;
    polling = true;
    try {
      final snapshot = await widget.core.call({'action': 'poll'});
      if (!working && !resources.active) accept(snapshot);
    } catch (_) {/* Explicit operations display bridge failures. */} finally {
      polling = false;
    }
  }

  void openInstance(Map<String, dynamic> instance, String tab) => setState(() {
        focusedInstance = instance;
        instanceTab = tab;
        page = 'instance';
      });
  void browseForInstance(String kind, {String? query}) => setState(() {
        marketTarget = focusedInstance;
        marketQuery = query ?? '';
        page = kind;
      });
  Future<void> refreshInstance() async {
    final instance = focusedInstance;
    if (instance == null) return;
    try {
      final result = await resources.run({
        'op': 'instances.scan',
        'roots': [state['root']]
      }) as Map;
      final matches = (result['instances'] as List)
          .cast<Map<String, dynamic>>()
          .where((i) => i['id'] == instance['id']);
      if (mounted &&
          focusedInstance?['path'] == instance['path'] &&
          matches.isNotEmpty) {
        setState(() => focusedInstance = matches.first);
      }
    } catch (_) {/* The current page reports actionable operation errors. */}
  }

  Future<void> manageSelected([String tab = 'mod']) async {
    final id = selected;
    final fromPage = page;
    if (id.isEmpty) return;
    try {
      final result = await resources.run({
        'op': 'instances.scan',
        'roots': [state['root']]
      }) as Map;
      final matches = (result['instances'] as List)
          .cast<Map<String, dynamic>>()
          .where((i) => i['id'] == id);
      if (mounted && page == fromPage && selected == id && matches.isNotEmpty) {
        openInstance(matches.first, tab);
      }
    } on EngineFailure catch (e) {
      if (mounted) setState(() => localError = e.details);
    }
  }

  Future<void> chooseRoot() async {
    final folder = await getDirectoryPath(confirmButtonText: tr("选择游戏目录"));
    if (folder != null) await action({'action': 'scan', 'root': folder});
  }

  Future<void> chooseJava() async {
    final file = await openFile(confirmButtonText: tr("选择 Java"));
    if (file == null) return;
    await action({'action': 'configure', 'java': file.path});
    await action({'action': 'detect_java', 'java': file.path});
    if (javas.any((j) => j['path'] == file.path)) {
      setState(() => java = file.path);
    }
  }

  String get selectedGameVersion =>
      versions.where((v) => v['id'] == selected).firstOrNull?['version']
          as String? ??
      selected;

  Future<void> chooseBackground() async {
    final choice = await showBackgroundPicker(context,
        current: state['home_background'] as String? ?? 'auto',
        version: selectedGameVersion);
    if (choice != null && mounted) {
      await action({'action': 'configure', 'home_background': choice});
    }
  }

  Future<void> settings() async {
    if (page == 'settings') return;
    setState(() {
      settingsReturnPage = page;
      page = 'settings';
    });
  }

  Future<void> setAppearance(String mode, String tone) async {
    AppAppearance.instance.select(mode, tone);
    await action({'action': 'configure', 'appearance': mode, 'tone': tone});
  }

  Future<void> setLanguage(String value) async {
    await action({'action': 'configure', 'language': value});
    if (mounted && state['language'] == value) {
      AppLanguage.instance.select(value);
    }
  }

  void downloads(String tab) => setState(() {
        downloadTab = tab;
        page = 'download';
      });

  Future<void> importModpack() => showModpackImport(context, resources,
          root: state['root'] as String? ?? '', onInstalled: (result) async {
        await installed('game', result);
        await manageSelected();
      });

  Future<void> installed(String kind, Map<String, dynamic> result) async {
    final runtime =
        kind == 'java' ? result : result['java'] as Map<String, dynamic>;
    await action({'action': 'configure', 'java': runtime['path']});
    await action(kind == 'game'
        ? {'action': 'scan', 'root': result['root']}
        : {'action': 'detect_java'});
    if (mounted) {
      setState(() {
        java = runtime['path'] as String;
        if (kind == 'game') selected = result['id'] as String;
      });
    }
  }

  String stageName(String? value) =>
      {
        'browser': tr("在浏览器中完成微软授权"),
        'microsoft': tr("验证 Microsoft 登录"),
        'xbox': tr("验证 Xbox 账号"),
        'xsts': tr("验证 Xbox 服务权限"),
        'minecraft': tr("验证 Minecraft Services"),
        'ownership': tr("检查 Java 版游戏资格"),
        'profile': tr("读取游戏角色"),
        'ready': tr("认证完成"),
        'callback': tr("浏览器登录回调"),
        'launch': tr("游戏启动"),
        'configuration': tr("应用配置"),
        'versions': tr("本地版本"),
        'network': tr("网络请求"),
        'settings': tr("本地设置"),
        'core': tr("C++ 核心")
      }[value] ??
      (value ?? tr("操作"));
  Widget badge(String text, {bool good = false}) => Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 5),
      decoration: BoxDecoration(
          color: good ? seclColor(0xFFE7F3EC) : seclColor(0xFFF0F3F7),
          borderRadius: BorderRadius.circular(6)),
      child: Text(text,
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(
              fontSize: 11, color: good ? seclColor(0xFF367856) : muted)));
  Widget accountPanel() => Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          mainAxisSize: MainAxisSize.min,
          children: [
            Material(
                color: Colors.transparent,
                child: InkWell(
                    borderRadius: BorderRadius.circular(10),
                    onTap: () => setState(() => page = 'accounts'),
                    child: Padding(
                        padding: const EdgeInsets.symmetric(vertical: 8),
                        child: Row(children: [
                          SkinAvatar(
                              skinUrl: account?['skin_url'] as String?,
                              size: 40),
                          const SizedBox(width: 10),
                          Expanded(
                              child: Column(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                Text(account?['name'] as String? ?? tr('未登录'),
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: const TextStyle(
                                        fontSize: 14,
                                        height: 1.5,
                                        fontWeight: FontWeight.w600)),
                                const SizedBox(height: 2),
                                Text(
                                    account == null
                                        ? tr('Microsoft 账号')
                                        : tr('Java 版资格已验证'),
                                    maxLines: 2,
                                    overflow: TextOverflow.ellipsis,
                                    style: TextStyle(
                                        fontSize: 10.5,
                                        height: 1.45,
                                        color: muted)),
                              ])),
                          const SizedBox(width: 3),
                          Icon(Icons.expand_more_rounded,
                              size: 17, color: muted)
                        ])))),
            if (account == null) ...[
              const SizedBox(height: 4),
              OutlinedButton.icon(
                  key: const Key('loginButton'),
                  onPressed: accountPending
                      ? null
                      : () => action({
                            'action': 'login',
                            'locale': AppLanguage.instance.code
                          }),
                  style: OutlinedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 10, vertical: 11),
                      side: BorderSide.none,
                      backgroundColor: accent.withOpacity(.10),
                      foregroundColor: accent),
                  icon: const Icon(Icons.login_rounded, size: 17),
                  label: Text(
                      tr(accountPending && !authenticating
                          ? '等待当前任务完成'
                          : '微软登录'),
                      style: const TextStyle(fontSize: 12.5))),
            ],
            if (authenticating)
              Padding(
                  padding: const EdgeInsets.only(top: 8),
                  child: Row(children: [
                    const SizedBox(
                        width: 12,
                        height: 12,
                        child: CircularProgressIndicator(strokeWidth: 2)),
                    const SizedBox(width: 7),
                    Expanded(
                        child: Text(stageName(state['stage'] as String?),
                            style: TextStyle(
                                fontSize: 10.5, height: 1.5, color: muted))),
                    IconButton(
                        tooltip: tr('取消登录'),
                        constraints: const BoxConstraints.tightFor(
                            width: 26, height: 26),
                        padding: EdgeInsets.zero,
                        onPressed: () => action({'action': 'cancel'}),
                        icon: const Icon(Icons.close_rounded, size: 15))
                  ]))
          ]);

  Widget navigation(String value, String label, LauncherIconKind icon,
          {Widget? trailing}) =>
      SidebarNavItem(
          key: Key('nav-$value'),
          label: label,
          selected: page == value || value == 'library' && page == 'instance',
          leading: LauncherIcon(icon,
              size: 21,
              color: page == value || value == 'library' && page == 'instance'
                  ? accent
                  : ink.withOpacity(.88)),
          trailing: trailing,
          onTap: () => setState(() {
                page = value;
                if (['mod', 'shader', 'resourcepack'].contains(value)) {
                  marketTarget = null;
                  marketQuery = '';
                }
              }));

  Widget sidebarTool(
          {required String tooltip,
          required LauncherIconKind icon,
          required VoidCallback? onPressed}) =>
      IconButton(
          tooltip: tooltip,
          onPressed: onPressed,
          constraints: const BoxConstraints.tightFor(width: 24, height: 24),
          padding: EdgeInsets.zero,
          style: IconButton.styleFrom(
              tapTargetSize: MaterialTapTargetSize.shrinkWrap),
          icon: LauncherIcon(icon, size: 16, color: muted));

  void showProjectAbout() => unawaited(showProjectAboutDialog(context));

  Widget sidebar() => LauncherSidebar(
          onAbout: showProjectAbout,
          profile: accountPanel(),
          settings: SidebarNavItem(
              label: tr('设置'),
              selected: page == 'settings',
              onTap: settings,
              leading: IconButton(
                  key: const Key('settingsButton'),
                  tooltip: tr('设置'),
                  onPressed: settings,
                  constraints:
                      const BoxConstraints.tightFor(width: 22, height: 22),
                  padding: EdgeInsets.zero,
                  icon: LauncherIcon(LauncherIconKind.settings,
                      size: 21,
                      color:
                          page == 'settings' ? accent : ink.withOpacity(.88)))),
          items: [
            navigation('home', tr('开始'), LauncherIconKind.home),
            navigation('library', tr('游戏库'), LauncherIconKind.library,
                trailing: sidebarTool(
                    tooltip: tr('刷新本地版本'),
                    icon: LauncherIconKind.refresh,
                    onPressed: pendingActions.contains('scan')
                        ? null
                        : () => action({'action': 'scan'}))),
            const SizedBox(height: 9),
            navigation('mod', tr('模组'), LauncherIconKind.mod),
            navigation('shader', tr('光影'), LauncherIconKind.shader),
            navigation(
                'resourcepack', tr('材质包'), LauncherIconKind.resourcepack),
            navigation('download', tr('下载'), LauncherIconKind.download),
            navigation('java', 'Java', LauncherIconKind.java),
            const SizedBox(height: 9),
          ]);
  Widget errorBox(Map<String, dynamic> error) => Container(
      key: const Key('errorPanel'),
      width: double.infinity,
      margin: const EdgeInsets.only(top: 14),
      padding: const EdgeInsets.all(14),
      decoration: BoxDecoration(
          color: seclColor(0xFFFFF3EE),
          borderRadius: BorderRadius.circular(9),
          border: Border.all(color: seclColor(0xFFF2D5C6))),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Icon(Icons.error_outline_rounded,
            color: seclColor(0xFFA75536), size: 20),
        const SizedBox(width: 10),
        Expanded(
            child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
              Text(
                  tr("{0}未完成{1}", [
                    stageName(error['stage'] as String?),
                    (error['http_status'] as int? ?? 0) > 0
                        ? ' · HTTP ${error['http_status']}'
                        : ''
                  ]),
                  style: const TextStyle(
                      fontWeight: FontWeight.w600, fontSize: 13)),
              const SizedBox(height: 5),
              SelectableText(errorMessage(error),
                  style: const TextStyle(fontSize: 12, height: 1.5)),
              const SizedBox(height: 4),
              SelectableText(
                  '${error['code'] ?? ''}${(error['url'] as String? ?? '').isNotEmpty ? ' · ${error['url']}' : ''}',
                  style: TextStyle(fontSize: 10, color: muted))
            ])),
        const SizedBox(width: 4),
        IconButton(
            tooltip: tr("关闭提示"),
            visualDensity: VisualDensity.compact,
            onPressed: () {
              setState(() => localError = null);
              unawaited(action({'action': 'clear_error'}));
            },
            icon: const Icon(Icons.close, size: 16))
      ]));
  Map<String, dynamic>? get selectedEntry =>
      versions.where((version) => version['id'] == selected).firstOrNull;

  void selectInstance(String id) => setState(() {
        selected = id;
        page = 'home';
        final pinned = selectedEntry?['java'];
        if (pinned is String &&
            javas.any((runtime) => runtime['path'] == pinned)) {
          java = pinned;
        }
      });

  String versionDescription(Map<String, dynamic>? version) {
    if (version == null) return tr('选择一个本地版本，开始你的旅程。');
    final base = version['version'] as String? ?? version['id'] as String;
    final loader = version['loader'] as String? ?? 'vanilla';
    final loaderName = {
          'fabric': 'Fabric',
          'forge': 'Forge',
          'neoforge': 'NeoForge',
          'quilt': 'Quilt',
          'vanilla': tr('原版')
        }[loader] ??
        loader;
    final loaderVersion = version['loader_version'] as String? ?? '';
    return 'Minecraft $base · $loaderName${loaderVersion.isEmpty ? '' : ' $loaderVersion'}';
  }

  Widget instanceSelector() => PopupMenuButton<String>(
      key: const Key('instanceSelector'),
      tooltip: tr('切换实例'),
      enabled: versions.isNotEmpty && !busy,
      color: panelSurface,
      surfaceTintColor: Colors.transparent,
      elevation: 8,
      shadowColor: Colors.black.withOpacity(.15),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
      constraints: const BoxConstraints(minWidth: 260, maxWidth: 380),
      onSelected: selectInstance,
      itemBuilder: (_) => versions
          .map((version) => PopupMenuItem<String>(
              value: version['id'] as String,
              enabled: version['available'] == true,
              height: 66,
              child: Row(children: [
                Icon(
                    version['id'] == selected
                        ? Icons.check_rounded
                        : Icons.sports_esports_outlined,
                    size: 19,
                    color: version['id'] == selected ? accent : muted),
                const SizedBox(width: 12),
                Expanded(
                    child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        mainAxisSize: MainAxisSize.min,
                        children: [
                      Text(
                          version['name'] as String? ?? version['id'] as String,
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(
                              fontSize: 14,
                              height: 1.5,
                              fontWeight: FontWeight.w600)),
                      const SizedBox(height: 3),
                      Text(versionDescription(version),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                              fontSize: 11, color: muted, height: 1.5))
                    ]))
              ])))
          .toList(),
      child: Row(mainAxisSize: MainAxisSize.min, children: [
        Flexible(
            child: Text(
                selectedEntry?['name'] as String? ??
                    (selected.isEmpty ? tr('选择游戏实例') : selected),
                maxLines: 2,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                    fontSize: MediaQuery.sizeOf(context).width < 960 ? 30 : 38,
                    height: 1.2,
                    fontWeight: FontWeight.w700,
                    letterSpacing: -.6,
                    color: ink))),
        const SizedBox(width: 12),
        Icon(Icons.keyboard_arrow_down_rounded, size: 27, color: ink),
      ]));

  Widget javaSelector() => DropdownButtonHideUnderline(
      child: DropdownButton<String>(
          key: const Key('javaSelector'),
          value: java.isEmpty ? null : java,
          isExpanded: true,
          isDense: true,
          borderRadius: BorderRadius.circular(10),
          menuMaxHeight: 300,
          dropdownColor: panelSurface,
          focusColor: Colors.transparent,
          icon: Icon(Icons.keyboard_arrow_down_rounded, size: 18, color: muted),
          hint:
              Text(tr('未检测到 Java'), style: TextStyle(fontSize: 13, color: ink)),
          style: TextStyle(color: ink, fontSize: 13),
          selectedItemBuilder: (_) => javas
              .map((runtime) => Align(
                  alignment: Alignment.centerLeft,
                  child: Text(
                      'Java ${runtime['version']} · ${runtime['vendor']}',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(fontSize: 13, color: ink))))
              .toList(),
          items: javas
              .map((runtime) => DropdownMenuItem(
                  value: runtime['path'] as String,
                  child: Text(
                      'Java ${runtime['version']} · ${runtime['vendor']} · ${runtime['arch']}',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis)))
              .toList(),
          onChanged: busy
              ? null
              : (value) {
                  if (value == null) return;
                  setState(() => java = value);
                  unawaited(action({
                    'action': 'configure',
                    'java': value,
                    'version': selected
                  }));
                }));

  String get sceneBackgroundAsset => homeBackgroundAsset(
      state['home_background'] as String? ?? 'auto', selectedGameVersion);

  Widget homeScene(Map<String, dynamic>? error) => HomeScene(
      backgroundAsset: sceneBackgroundAsset,
      title: selectedEntry?['name'] as String? ?? selected,
      versionLabel: versionDescription(selectedEntry),
      versionSelector: instanceSelector(),
      javaSelector: javaSelector(),
      instanceBrowser: HomeInstanceShelf(
          instances: versions,
          selected: selected,
          onSelected: selectInstance,
          enabled: !busy,
          onOpenLibrary: () => setState(() => page = 'library')),
      canLaunch: canLaunch,
      gameRunning: state['game_running'] == true,
      launchPending: pendingActions.contains('launch'),
      launchStatus: state['game_running'] == true
          ? tr('游戏正在运行')
          : account == null
              ? tr('登录后验证游戏资格')
              : selected.isEmpty
                  ? tr('请选择本地版本')
                  : '${account!['name']} · ${tr('准备就绪')}',
      error: error == null ? null : errorBox(error),
      onLaunch: canLaunch
          ? () =>
              action({'action': 'launch', 'version': selected, 'java': java})
          : null,
      onManage: busy || selected.isEmpty ? null : () => manageSelected(),
      onInstallGame: busy ? null : () => downloads('game'),
      onImport: busy ? null : importModpack,
      onChooseRoot: busy ? null : chooseRoot,
      onInstallJava: busy ? null : () => downloads('java'),
      onDetectJava: busy ? null : () => action({'action': 'detect_java'}),
      onChooseJava: busy ? null : chooseJava,
      onChangeBackground: chooseBackground);

  Widget pageBody(Map<String, dynamic>? error, {String? route}) {
    final currentPage = route ?? page;
    if (currentPage == 'accounts') {
      return AccountManager(
          snapshot: state,
          action: action,
          pending: accountPending,
          authenticating: authenticating);
    }
    if (currentPage == 'java') {
      return JavaManager(
          engine: resources,
          onDownload: () => downloads('java'),
          onChanged: () => action({'action': 'detect_java'}));
    }
    if (currentPage == 'library') {
      return InstanceLibrary(
          engine: resources,
          root: state['root'] as String? ?? '',
          onOpen: openInstance,
          onInstall: () => downloads('game'),
          onImport: importModpack,
          onLaunch: (instance) {
            selectInstance(instance['id'] as String);
            action({'action': 'launch', 'version': selected, 'java': java});
          });
    }
    if (currentPage == 'instance' && focusedInstance != null) {
      return InstancePage(
          key: ValueKey('instance-${focusedInstance!['path']}'),
          engine: resources,
          instance: focusedInstance!,
          javas: javas,
          initialTab: instanceTab,
          onBack: () => setState(() => page = 'library'),
          onChanged: refreshInstance,
          onBrowse: browseForInstance);
    }
    if (currentPage == 'download') {
      return DownloadCenter(
          key: ValueKey('download-$downloadTab'),
          engine: resources,
          root: state['root'] as String? ?? '',
          javaDirectory: state['java_directory'] as String? ?? '',
          javas: javas,
          source: state['download_source'] as String? ?? 'auto',
          onSource: (source) =>
              action({'action': 'configure', 'download_source': source}),
          onInstalled: installed,
          onImport: importModpack,
          initialTab: downloadTab);
    }
    if (currentPage != 'home') {
      return ResourceMarket(
          kind: currentPage,
          engine: resources,
          root: state['root'] as String? ?? '',
          selectedVersion: selected,
          initialTarget: marketTarget,
          initialQuery: marketQuery,
          onBack: marketTarget == null
              ? null
              : () => setState(() => page = 'instance'));
    }
    return homeScene(error);
  }

  @override
  Widget build(BuildContext context) {
    final error = localError ?? state['error'] as Map<String, dynamic>?;
    return PageArtworkScope(
        asset: selectedEntry == null ? null : sceneBackgroundAsset,
        child: Scaffold(
            body: Row(children: [
          sidebar(),
          Expanded(
              child: Stack(fit: StackFit.expand, children: [
            Offstage(
                offstage: page == 'settings',
                child: ExcludeFocus(
                    excluding: page == 'settings',
                    child: pageBody(error,
                        route:
                            page == 'settings' ? settingsReturnPage : page))),
            if (page == 'settings')
              SettingsPage(
                  background: state['home_background'] as String? ?? 'auto',
                  version: selectedGameVersion,
                  onAppearanceChanged: setAppearance,
                  onLanguageChanged: setLanguage,
                  onChooseBackground: chooseBackground,
                  onDone: () => setState(() => page = settingsReturnPage),
                  onAbout: showProjectAbout),
            Positioned(
                top: 42,
                right: 16,
                child: DownloadTaskButton(engine: resources)),
          ]))
        ])));
  }
}
