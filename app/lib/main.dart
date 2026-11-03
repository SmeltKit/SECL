import 'appearance.dart';
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
import 'skin_avatar.dart';
import 'instance_library.dart';
import 'instance_page.dart';
import 'java_manager.dart';
import 'account_manager.dart';
import 'task_panel.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  LicenseRegistry.addLicense(() async* {
    for (final entry in {
      'SECL / M²CL': 'LICENSE',
      'M²CL additional terms': 'ADDITIONAL_TERMS.md',
      'nlohmann/json': 'json-MIT.txt',
      'TinyXML2': 'tinyxml2-Zlib.txt'
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
  String downloadTab = 'game';
  Map<String, dynamic>? focusedInstance, marketTarget;
  String instanceTab = 'mod', marketQuery = '';
  late final ResourceEngine resources;
  bool working = false, polling = false, languageLoaded = false;
  Timer? timer;
  Map<String, dynamic>? localError;
  List<Map<String, dynamic>> get versions =>
      (state['versions'] as List? ?? []).cast<Map<String, dynamic>>();
  List<Map<String, dynamic>> get javas =>
      (state['javas'] as List? ?? []).cast<Map<String, dynamic>>();
  Map<String, dynamic>? get account =>
      state['account'] as Map<String, dynamic>?;
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
    setState(() {
      working = true;
      localError = null;
    });
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
    } finally {
      if (mounted) setState(() => working = false);
    }
  }

  Future<void> poll() async {
    if (polling || working || resources.active || !mounted) return;
    polling = true;
    try {
      accept(await widget.core.call({'action': 'poll'}));
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
    if (focusedInstance == null) return;
    try {
      final result = await resources.run({
        'op': 'instances.scan',
        'roots': [state['root']]
      }) as Map;
      final matches = (result['instances'] as List)
          .cast<Map<String, dynamic>>()
          .where((i) => i['id'] == focusedInstance!['id']);
      if (mounted && matches.isNotEmpty) {
        setState(() => focusedInstance = matches.first);
      }
    } catch (_) {/* The current page reports actionable operation errors. */}
  }

  Future<void> manageSelected([String tab = 'mod']) async {
    if (selected.isEmpty) return;
    try {
      final result = await resources.run({
        'op': 'instances.scan',
        'roots': [state['root']]
      }) as Map;
      final matches = (result['instances'] as List)
          .cast<Map<String, dynamic>>()
          .where((i) => i['id'] == selected);
      if (mounted && matches.isNotEmpty) openInstance(matches.first, tab);
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

  Future<void> settings() async {
    await showDialog<void>(
        context: context,
        builder: (dialogContext) => AnimatedBuilder(
            animation: Listenable.merge(
                [AppLanguage.instance, AppAppearance.instance]),
            builder: (context, _) => AlertDialog(
                    title: Text(tr('设置')),
                    content: SizedBox(
                        width: 420,
                        child: Column(
                            mainAxisSize: MainAxisSize.min,
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              DropdownButtonFormField<String>(
                                  value: AppAppearance.instance.mode,
                                  items: [
                                    for (final value in [
                                      'system',
                                      'light',
                                      'dark'
                                    ])
                                      DropdownMenuItem(
                                          value: value,
                                          child: Text(tr({
                                            'system': '跟随系统',
                                            'light': '浅色',
                                            'dark': '深色'
                                          }[value]!)))
                                  ],
                                  onChanged: (value) async {
                                    if (value == null) return;
                                    AppAppearance.instance.select(
                                        value, AppAppearance.instance.tone);
                                    await action({
                                      'action': 'configure',
                                      'appearance': value
                                    });
                                  }),
                              const SizedBox(height: 12),
                              DropdownButtonFormField<String>(
                                  value: AppAppearance.instance.tone,
                                  items: [
                                    for (final value in [
                                      'blue',
                                      'forest',
                                      'violet'
                                    ])
                                      DropdownMenuItem(
                                          value: value,
                                          child: Text(tr({
                                            'blue': '蓝色',
                                            'forest': '森林绿',
                                            'violet': '紫色'
                                          }[value]!)))
                                  ],
                                  onChanged: (value) async {
                                    if (value == null) return;
                                    AppAppearance.instance.select(
                                        AppAppearance.instance.mode, value);
                                    await action(
                                        {'action': 'configure', 'tone': value});
                                  }),
                              const SizedBox(height: 16),
                              Text(tr('界面语言'),
                                  style: const TextStyle(
                                      fontWeight: FontWeight.w600)),
                              const SizedBox(height: 10),
                              DropdownButtonFormField<String>(
                                  key: const Key('languageSelector'),
                                  borderRadius: BorderRadius.circular(9),
                                  focusColor: Colors.transparent,
                                  value: AppLanguage.instance.preference,
                                  isExpanded: true,
                                  items: [
                                    DropdownMenuItem(
                                        value: 'system',
                                        child: Text(tr('跟随系统'))),
                                    const DropdownMenuItem(
                                        value: 'zh_Hans', child: Text('简体中文')),
                                    const DropdownMenuItem(
                                        value: 'zh_Hant', child: Text('繁體中文')),
                                    const DropdownMenuItem(
                                        value: 'en', child: Text('English')),
                                  ],
                                  onChanged: (value) async {
                                    if (value == null || busy) return;
                                    await action({
                                      'action': 'configure',
                                      'language': value
                                    });
                                    if (mounted && state['language'] == value) {
                                      AppLanguage.instance.select(value);
                                    }
                                  }),
                              const SizedBox(height: 12),
                              Text(tr('切换后立即生效。项目介绍和文件名保留原文。'),
                                  style: TextStyle(
                                      fontSize: 12, color: muted, height: 1.5)),
                              const SizedBox(height: 22),
                              OutlinedButton.icon(
                                  key: const Key('appConfiguration'),
                                  onPressed: () async {
                                    Navigator.pop(dialogContext);
                                    await configure();
                                  },
                                  icon:
                                      const Icon(Icons.key_outlined, size: 18),
                                  label: Text(tr('微软应用配置'))),
                            ])),
                    actions: [
                      TextButton(
                          onPressed: () => Navigator.pop(dialogContext),
                          child: Text(tr('完成')))
                    ])));
  }

  Future<void> configure() async {
    final controller =
        TextEditingController(text: state['client_id'] as String? ?? '');
    final save = await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
                title: Text(tr("微软应用配置"),
                    style: const TextStyle(
                        fontSize: 20, fontWeight: FontWeight.w600)),
                content: SizedBox(
                    width: 480,
                    child: Column(
                        mainAxisSize: MainAxisSize.min,
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          const Text('Application（client）ID'),
                          const SizedBox(height: 10),
                          TextField(
                              key: const Key('clientIdField'),
                              controller: controller,
                              decoration: InputDecoration(
                                  hintText: tr("SECL 自己注册的公共客户端应用 ID"))),
                          const SizedBox(height: 16),
                          Text(tr("桌面应用回调地址"),
                              style:
                                  const TextStyle(fontWeight: FontWeight.w600)),
                          const SizedBox(height: 6),
                          const SelectableText(
                              'http://localhost/oauth/callback',
                              style: TextStyle(
                                  fontFamily: 'monospace', fontSize: 13)),
                          const SizedBox(height: 14),
                          Text(tr("个人 Microsoft 账号 · 授权码 + PKCE\n无需 Client Secret。API 访问仍须由应用自身获准。"),
                              style: TextStyle(
                                  fontSize: 13, color: muted, height: 1.6)),
                        ])),
                actions: [
                  TextButton(
                      onPressed: () => Navigator.pop(context, false),
                      child: Text(tr("取消"))),
                  FilledButton(
                      onPressed: () => Navigator.pop(context, true),
                      child: Text(tr("保存")))
                ]));
    if (save == true) {
      await action({
        'action': 'configure',
        'client_id': controller.text.trim(),
        'java': java
      });
    }
    controller.dispose();
  }

  void downloads(String tab) => setState(() {
        downloadTab = tab;
        page = 'download';
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
  Widget accountPanel() =>
      Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          SkinAvatar(skinUrl: account?['skin_url'] as String?),
          const SizedBox(width: 12),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(account?['name'] as String? ?? tr("未登录"),
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        fontSize: 16, fontWeight: FontWeight.w600)),
                const SizedBox(height: 4),
                Text(account == null ? tr("Microsoft 账号") : tr("Java 版资格已验证"),
                    style: TextStyle(
                        fontSize: 11,
                        color:
                            account == null ? muted : seclColor(0xFF367856))),
              ]))
        ]),
        const SizedBox(height: 16),
        SizedBox(
            width: double.infinity,
            child: account == null
                ? OutlinedButton.icon(
                    key: const Key('loginButton'),
                    onPressed: busy
                        ? null
                        : () => action({
                              'action': 'login',
                              'locale': AppLanguage.instance.code
                            }),
                    icon: const Icon(Icons.login_rounded, size: 17),
                    label: Text(tr("微软登录")))
                : OutlinedButton.icon(
                    onPressed: busy ? null : () => action({'action': 'logout'}),
                    icon: const Icon(Icons.logout_rounded, size: 17),
                    label: Text(tr("退出账号")))),
        if (state['busy'] == true && state['stage'] != 'engine')
          Padding(
              padding: const EdgeInsets.only(top: 12),
              child: Row(children: [
                const SizedBox(
                    width: 14,
                    height: 14,
                    child: CircularProgressIndicator(strokeWidth: 2)),
                const SizedBox(width: 8),
                Expanded(
                    child: Text(stageName(state['stage'] as String?),
                        style: const TextStyle(fontSize: 11))),
                IconButton(
                    onPressed: () => action({'action': 'cancel'}),
                    tooltip: tr("取消登录"),
                    icon: const Icon(Icons.close, size: 16))
              ])),
      ]);
  Widget navigation(String value, String label, IconData icon) => Padding(
      padding: const EdgeInsets.symmetric(vertical: 1),
      child: Material(
          color: page == value ? seclColor(0xFFEBF1F7) : Colors.transparent,
          borderRadius: BorderRadius.circular(8),
          child: InkWell(
              borderRadius: BorderRadius.circular(8),
              key: Key('nav-$value'),
              onTap: busy
                  ? null
                  : () => setState(() {
                        page = value;
                        if (['mod', 'shader', 'resourcepack'].contains(value)) {
                          marketTarget = null;
                          marketQuery = '';
                        }
                      }),
              child: Padding(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 12, vertical: 9),
                  child: Row(children: [
                    Icon(icon, size: 18, color: page == value ? accent : muted),
                    const SizedBox(width: 12),
                    Expanded(
                        child: Text(label,
                            softWrap: true,
                            style: TextStyle(
                                fontSize: 14,
                                fontWeight: page == value
                                    ? FontWeight.w600
                                    : FontWeight.w400,
                                color: page == value ? accent : ink)))
                  ])))));
  Widget sidebar() => Container(
      width: 224,
      decoration: BoxDecoration(
          color: panelSurface, border: Border(right: BorderSide(color: line))),
      child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Padding(
            padding:
                EdgeInsets.fromLTRB(23, Platform.isMacOS ? 48 : 26, 20, 16),
            child: Row(children: [
              Icon(Icons.wb_sunny_outlined, color: accent, size: 27),
              const SizedBox(width: 10),
              const Text('SECL',
                  style: TextStyle(
                      fontSize: 20,
                      fontWeight: FontWeight.w600,
                      letterSpacing: 2))
            ])),
        Expanded(
            child: SingleChildScrollView(
                child: Column(children: [
          Padding(
              padding: const EdgeInsets.symmetric(horizontal: 20),
              child: accountPanel()),
          const SizedBox(height: 12),
          const Divider(),
          Padding(
              padding: const EdgeInsets.all(8),
              child: Column(children: [
                navigation('home', tr("开始"), Icons.home_outlined),
                navigation('library', tr('游戏库'), Icons.grid_view_outlined),
                navigation(
                    'accounts', tr('账号管理'), Icons.person_outline_rounded),
                navigation('java', tr('Java 管理'), Icons.coffee_outlined),
                navigation('download', tr('下载'), Icons.download_outlined),
                navigation('mod', tr("模组"), Icons.extension_outlined),
                navigation('shader', tr("光影"), Icons.wb_twilight_outlined),
                navigation('resourcepack', tr("材质包"), Icons.layers_outlined),
              ])),
          const Divider(),
          Padding(
              padding: const EdgeInsets.fromLTRB(22, 4, 12, 0),
              child: Row(children: [
                Expanded(
                    child: Text(tr("本地版本"),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                            fontWeight: FontWeight.w600, fontSize: 13))),
                Text('${versions.length}',
                    style: TextStyle(color: muted, fontSize: 12)),
                IconButton(
                    tooltip: tr("刷新本地版本"),
                    onPressed: busy
                        ? null
                        : () =>
                            action({'action': 'scan', 'root': state['root']}),
                    icon: const Icon(Icons.refresh_rounded, size: 17))
              ])),
          SizedBox(
              height: 220,
              child: versions.isEmpty
                  ? Center(
                      child: Padding(
                          padding: const EdgeInsets.all(20),
                          child: Text(tr("尚未识别本地版本\n选择已安装的游戏目录"),
                              textAlign: TextAlign.center,
                              style: TextStyle(
                                  color: muted, fontSize: 12, height: 1.8))))
                  : ListView.separated(
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      itemCount: versions.length,
                      separatorBuilder: (_, __) => const SizedBox(height: 4),
                      itemBuilder: (_, i) {
                        final v = versions[i];
                        return Material(
                            color: v['id'] == selected
                                ? seclColor(0xFFEBF1F7)
                                : Colors.transparent,
                            borderRadius: BorderRadius.circular(8),
                            child: ListTile(
                                dense: true,
                                shape: RoundedRectangleBorder(
                                    borderRadius: BorderRadius.circular(8)),
                                contentPadding: const EdgeInsets.symmetric(
                                    horizontal: 12, vertical: 3),
                                title: Text(v['id'] as String,
                                    style: TextStyle(
                                        fontSize: 13,
                                        fontWeight: v['id'] == selected
                                            ? FontWeight.w600
                                            : FontWeight.w400,
                                        color: v['available'] == true
                                            ? ink
                                            : muted)),
                                subtitle: Text(
                                    v['available'] == true
                                        ? tr("已安装")
                                        : tr("配置需要处理"),
                                    style:
                                        TextStyle(fontSize: 11, color: muted)),
                                trailing: v['id'] == selected
                                    ? Icon(Icons.check_rounded,
                                        size: 17, color: accent)
                                    : null,
                                onTap: busy || v['available'] != true
                                    ? null
                                    : () => setState(() {
                                          selected = v['id'] as String;
                                          page = 'home';
                                          final pinned = v['java'];
                                          if (pinned is String &&
                                              javas.any(
                                                  (j) => j['path'] == pinned)) {
                                            java = pinned;
                                          }
                                        })));
                      })),
        ]))),
        FooterBar(
            horizontalPadding: 20,
            child: Row(children: [
              Expanded(
                  child: InkWell(
                      onTap: () => showAboutDialog(
                          context: context,
                          applicationName: 'SECL',
                          children: [
                            const Padding(
                                padding: EdgeInsets.only(top: 12),
                                child: Text('SUPREME ETERNITY CRAFT LAUNCHER',
                                    style: TextStyle(
                                        fontSize: 14,
                                        fontWeight: FontWeight.w600,
                                        height: 1.5)))
                          ],
                          applicationVersion: tr("0.1.0 · 开发预览"),
                          applicationLegalese: tr(
                              "Copyright © 2026 ppm203\n基于 M²CL 迁移与重写 · GPL-3.0-only + 第 7 节附加条款\n非官方项目，与 Mojang 或 Microsoft 无隶属或背书关系。")),
                      child: badge(tr("开发预览 0.1.0")))),
              IconButton(
                  tooltip: tr('下载任务'),
                  onPressed: () => showDialog<void>(
                      context: context,
                      builder: (_) => TaskPanel(engine: resources)),
                  icon: const Icon(Icons.downloading_outlined, size: 19)),
              const SizedBox(width: 6),
              IconButton(
                  key: const Key('settingsButton'),
                  tooltip: tr('设置'),
                  onPressed: busy ? null : settings,
                  icon: const Icon(Icons.settings_outlined, size: 19))
            ])),
      ]));
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
  @override
  Widget build(BuildContext context) {
    final error = localError ?? state['error'] as Map<String, dynamic>?;
    return Scaffold(
        body: Row(children: [
      sidebar(),
      Expanded(
          child: page == 'accounts'
              ? AccountManager(
                  snapshot: state, action: action, onConfigure: configure)
              : page == 'java'
                  ? JavaManager(
                      engine: resources,
                      onDownload: () => downloads('java'),
                      onChanged: () => action({'action': 'detect_java'}))
                  : page == 'library'
                      ? InstanceLibrary(
                          engine: resources,
                          root: state['root'] as String? ?? '',
                          onOpen: openInstance,
                          onInstall: () => downloads('game'),
                          onLaunch: (i) {
                            setState(() {
                              selected = i['id'] as String;
                              page = 'home';
                            });
                            action({
                              'action': 'launch',
                              'version': selected,
                              'java': java
                            });
                          })
                      : page == 'instance' && focusedInstance != null
                          ? InstancePage(
                              key: ValueKey(
                                  'instance-${focusedInstance!['path']}'),
                              engine: resources,
                              instance: focusedInstance!,
                              javas: javas,
                              initialTab: instanceTab,
                              onBack: () => setState(() => page = 'library'),
                              onChanged: refreshInstance,
                              onBrowse: browseForInstance)
                          : page == 'download'
                              ? DownloadCenter(
                                  key: ValueKey('download-$downloadTab'),
                                  engine: resources,
                                  root: state['root'] as String? ?? '',
                                  javaDirectory:
                                      state['java_directory'] as String? ?? '',
                                  javas: javas,
                                  source: state['download_source'] as String? ??
                                      'auto',
                                  onSource: (source) => action({
                                        'action': 'configure',
                                        'download_source': source
                                      }),
                                  onInstalled: installed,
                                  initialTab: downloadTab)
                              : page != 'home'
                                  ? ResourceMarket(
                                      kind: page,
                                      engine: resources,
                                      root: state['root'] as String? ?? '',
                                      selectedVersion: selected,
                                      initialTarget: marketTarget,
                                      initialQuery: marketQuery,
                                      onBack: marketTarget == null
                                          ? null
                                          : () =>
                                              setState(() => page = 'instance'))
                                  : Column(children: [
                                      Padding(
                                          padding: EdgeInsets.fromLTRB(
                                              28,
                                              Platform.isMacOS ? 44 : 22,
                                              28,
                                              18),
                                          child: Row(children: [
                                            Text(tr("开始"),
                                                style: const TextStyle(
                                                    fontSize: 25,
                                                    fontWeight:
                                                        FontWeight.w600)),
                                            const Spacer(),
                                            if (selected.isNotEmpty)
                                              TextButton.icon(
                                                  onPressed: busy
                                                      ? null
                                                      : () => manageSelected(),
                                                  icon: const Icon(
                                                      Icons.tune_rounded,
                                                      size: 18),
                                                  label: Text(tr('管理实例'))),
                                            TextButton.icon(
                                                key: const Key(
                                                    'installGameShortcut'),
                                                onPressed: busy
                                                    ? null
                                                    : () => downloads('game'),
                                                icon: const Icon(
                                                    Icons.add_rounded,
                                                    size: 18),
                                                label: Text(tr('安装游戏'))),
                                            const SizedBox(width: 8),
                                            IconButton(
                                                tooltip: tr("选择游戏目录"),
                                                onPressed:
                                                    busy ? null : chooseRoot,
                                                icon: const Icon(
                                                    Icons.folder_open_rounded,
                                                    size: 20))
                                          ])),
                                      Expanded(
                                          child: Padding(
                                              padding:
                                                  const EdgeInsets.symmetric(
                                                      horizontal: 28),
                                              child: Column(children: [
                                                Expanded(
                                                    child: Container(
                                                        key: const Key('cover'),
                                                        width: double.infinity,
                                                        clipBehavior:
                                                            Clip.antiAlias,
                                                        decoration: BoxDecoration(
                                                            borderRadius:
                                                                BorderRadius
                                                                    .circular(
                                                                        12)),
                                                        child: Stack(
                                                            fit:
                                                                StackFit.expand,
                                                            children: [
                                                              Image.asset(
                                                                  'assets/solar-dawn.jpg',
                                                                  fit: BoxFit
                                                                      .cover,
                                                                  alignment:
                                                                      Alignment
                                                                          .center),
                                                              const DecoratedBox(
                                                                  decoration: BoxDecoration(
                                                                      gradient: LinearGradient(
                                                                          begin:
                                                                              Alignment.bottomLeft,
                                                                          end: Alignment.topRight,
                                                                          colors: [
                                                                    Color(
                                                                        0xA5182B40),
                                                                    Color(
                                                                        0x00182B40)
                                                                  ]))),
                                                              Padding(
                                                                  padding:
                                                                      const EdgeInsets
                                                                          .all(
                                                                          28),
                                                                  child: Column(
                                                                      crossAxisAlignment:
                                                                          CrossAxisAlignment
                                                                              .start,
                                                                      mainAxisAlignment:
                                                                          MainAxisAlignment
                                                                              .end,
                                                                      children: [
                                                                        Text(
                                                                            selected.isEmpty
                                                                                ? 'SECL'
                                                                                : selected,
                                                                            style: const TextStyle(
                                                                                fontSize: 32,
                                                                                fontWeight: FontWeight.w600,
                                                                                color: Colors.white)),
                                                                        const SizedBox(
                                                                            height:
                                                                                9),
                                                                        Text(
                                                                            selected.isEmpty
                                                                                ? tr("选择一个本地版本，开始你的旅程。")
                                                                                : tr("Java Edition · 本地已安装"),
                                                                            style: const TextStyle(fontSize: 13, color: Color(0xFFDFE7EF)))
                                                                      ]))
                                                            ]))),
                                                const SizedBox(height: 18),
                                                Row(children: [
                                                  Icon(Icons.folder_outlined,
                                                      color: muted, size: 19),
                                                  const SizedBox(width: 9),
                                                  Expanded(
                                                      child: Tooltip(
                                                          message: state['root']
                                                                  as String? ??
                                                              '',
                                                          child: Text(
                                                              (state['root']
                                                                          as String? ??
                                                                      tr(
                                                                          "尚未选择游戏目录"))
                                                                  .replaceFirst(
                                                                      Platform.environment['HOME'] ??
                                                                          Platform.environment[
                                                                              'USERPROFILE'] ??
                                                                          '<none>',
                                                                      '~'),
                                                              maxLines: 1,
                                                              overflow:
                                                                  TextOverflow
                                                                      .ellipsis,
                                                              style: TextStyle(
                                                                  fontSize: 12,
                                                                  color:
                                                                      muted))))
                                                ]),
                                                const SizedBox(height: 14),
                                                Row(children: [
                                                  const Text('Java',
                                                      style: TextStyle(
                                                          fontSize: 13,
                                                          fontWeight:
                                                              FontWeight.w600)),
                                                  const SizedBox(width: 12),
                                                  Expanded(
                                                      child: DropdownButtonFormField<
                                                              String>(
                                                          key: const Key(
                                                              'javaSelector'),
                                                          value: java.isEmpty
                                                              ? null
                                                              : java,
                                                          isExpanded: true,
                                                          icon: const Icon(Icons.keyboard_arrow_down_rounded,
                                                              size: 20),
                                                          hint: Text(tr("未检测到 Java"),
                                                              style: const TextStyle(
                                                                  fontSize:
                                                                      13)),
                                                          style: TextStyle(
                                                              color: ink,
                                                              fontSize: 13),
                                                          items: javas
                                                              .map((j) => DropdownMenuItem(
                                                                  value: j['path']
                                                                      as String,
                                                                  child: Text('Java ${j['version']} · ${j['vendor']} · ${j['arch']}',
                                                                      overflow: TextOverflow.ellipsis)))
                                                              .toList(),
                                                          onChanged: busy
                                                              ? null
                                                              : (v) {
                                                                  if (v !=
                                                                      null) {
                                                                    setState(() =>
                                                                        java =
                                                                            v);
                                                                    unawaited(
                                                                        action({
                                                                      'action':
                                                                          'configure',
                                                                      'java': v,
                                                                      'version':
                                                                          selected
                                                                    }));
                                                                  }
                                                                })),
                                                  const SizedBox(width: 8),
                                                  IconButton(
                                                      key: const Key(
                                                          'installJavaShortcut'),
                                                      tooltip: tr('安装 Java'),
                                                      onPressed: busy
                                                          ? null
                                                          : () =>
                                                              downloads('java'),
                                                      icon: const Icon(
                                                          Icons
                                                              .download_outlined,
                                                          size: 20)),
                                                  IconButton(
                                                      tooltip: tr("检测本机 Java"),
                                                      onPressed: busy
                                                          ? null
                                                          : () => action({
                                                                'action':
                                                                    'detect_java'
                                                              }),
                                                      icon: const Icon(
                                                          Icons.refresh_rounded,
                                                          size: 20)),
                                                  IconButton(
                                                      tooltip: tr("选择本地 Java"),
                                                      onPressed: busy
                                                          ? null
                                                          : chooseJava,
                                                      icon: const Icon(
                                                          Icons
                                                              .more_horiz_rounded,
                                                          size: 20))
                                                ]),
                                                if (error != null)
                                                  ConstrainedBox(
                                                      constraints:
                                                          const BoxConstraints(
                                                              maxHeight: 160),
                                                      child:
                                                          SingleChildScrollView(
                                                              child: errorBox(
                                                                  error))),
                                                const SizedBox(height: 20),
                                              ]))),
                                      FooterBar(
                                          child: Row(children: [
                                        Expanded(
                                            child: Text(
                                                state['game_running'] == true
                                                    ? tr("游戏正在运行")
                                                    : account == null
                                                        ? tr("登录后验证游戏资格")
                                                        : selected.isEmpty
                                                            ? tr("请选择本地版本")
                                                            : '${account!['name']} · $selected',
                                                style: const TextStyle(
                                                    fontSize: 13,
                                                    fontWeight:
                                                        FontWeight.w500))),
                                        const SizedBox(width: 14),
                                        Tooltip(
                                            message: canLaunch
                                                ? tr("启动所选本地游戏")
                                                : tr(
                                                    "需要登录、有效游戏资格、本地版本与可用 Java"),
                                            child: SizedBox(
                                                width: 176,
                                                height: 50,
                                                child: FilledButton.icon(
                                                    key: const Key(
                                                        'launchButton'),
                                                    onPressed: canLaunch
                                                        ? () => action({
                                                              'action':
                                                                  'launch',
                                                              'version':
                                                                  selected,
                                                              'java': java
                                                            })
                                                        : null,
                                                    style: FilledButton.styleFrom(
                                                        shape: RoundedRectangleBorder(
                                                            borderRadius:
                                                                BorderRadius.circular(
                                                                    9))),
                                                    icon: working
                                                        ? const SizedBox(
                                                            width: 18,
                                                            height: 18,
                                                            child: CircularProgressIndicator(strokeWidth: 2))
                                                        : const Icon(Icons.play_arrow_rounded, size: 23),
                                                    label: Text(tr("启动游戏"), style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w600)))))
                                      ])),
                                    ]))
    ]));
  }
}
