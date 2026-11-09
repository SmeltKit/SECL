import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'resource_engine.dart';
import 'management_widgets.dart';

typedef DownloadComplete = Future<void> Function(
    String kind, Map<String, dynamic> result);

class DownloadCenter extends StatefulWidget {
  const DownloadCenter(
      {super.key,
      required this.engine,
      required this.root,
      required this.javaDirectory,
      required this.javas,
      required this.source,
      required this.onSource,
      required this.onInstalled,
      this.onImport,
      this.initialTab = 'game'});
  final ResourceEngine engine;
  final String root, javaDirectory, source, initialTab;
  final List<Map<String, dynamic>> javas;
  final Future<void> Function(String) onSource;
  final DownloadComplete onInstalled;
  final VoidCallback? onImport;
  @override
  State<DownloadCenter> createState() => _DownloadCenterState();
}

class _DownloadCenterState extends State<DownloadCenter> {
  late String tab, root;
  String channel = 'release',
      query = '',
      loader = 'vanilla',
      loaderVersion = '';
  final name = TextEditingController();
  final search = TextEditingController();
  List<Map<String, dynamic>> games = [], builds = [], loaders = [];
  List<int> javaReleases = [];
  Map<String, dynamic>? game, selectedBuild, problem;
  Map<String, dynamic> host = {};
  String os = '', arch = '', vendor = 'zulu';
  int major = 21;
  String javaChoice = '';
  bool loading = false, installing = false, forceJava = false;
  bool buildsQueried = false;
  String? notice;
  VoidCallback? retry;
  bool get locked => loading || installing || widget.engine.active;
  static final vendors = {
    'zulu': 'Zulu',
    'temurin': 'Temurin',
    'microsoft': 'Microsoft OpenJDK',
    'oracle-openjdk': 'Oracle OpenJDK'
  };
  static final systems = {
    'windows': 'Windows',
    'osx': 'macOS',
    'linux': 'Linux'
  };
  static final arches = {
    'x86_64': 'x64 (AMD64)',
    'aarch64': 'ARM64 (AArch64)',
    'x86': 'x86 (32-bit)',
    'arm': 'ARM32 (ARMv7)',
    'riscv64': 'RISC-V 64',
    'ppc64le': 'PPC64LE',
    's390x': 'S390x'
  };
  @override
  void initState() {
    super.initState();
    tab = widget.initialTab;
    root = widget.root;
    widget.engine.addListener(changed);
    unawaited(initialize());
  }

  void changed() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    widget.engine.removeListener(changed);
    name.dispose();
    search.dispose();
    super.dispose();
  }

  Future<dynamic> run(Map<String, dynamic> request) =>
      widget.engine.run({...request, 'source': widget.source}, title: '下载');
  Future<void> guard(Future<void> Function() work,
      {bool install = false}) async {
    if (locked) return;
    setState(() {
      problem = null;
      notice = null;
      retry = () => unawaited(guard(work, install: install));
      if (install) {
        installing = true;
      } else {
        loading = true;
      }
    });
    try {
      await work();
    } on EngineFailure catch (e) {
      if (mounted) {
        setState(() {
          if (e.details['code'] == 'cancelled' ||
              e.details['code'] == 'paused') {
            notice = e.details['code'] == 'paused'
                ? tr('下载已暂停，可从下载任务中继续。')
                : tr('操作已取消');
          } else {
            problem = e.details;
          }
        });
      }
    } catch (_) {
      if (mounted) {
        setState(() => problem = {
              'code': 'invalid_download_result',
              'message': '无法读取下载结果，请重试。'
            });
      }
    } finally {
      if (mounted) {
        setState(() {
          loading = false;
          installing = false;
        });
      }
    }
  }

  Future<void> initialize() => guard(() async {
        final p =
            Map<String, dynamic>.from(await run({'op': 'platform'}) as Map);
        if (!mounted) return;
        setState(() {
          host = p;
          os = p['os'] as String;
          arch = p['arch'] as String;
        });
        if (tab == 'game') {
          await loadGames();
        } else {
          await loadJavaReleases();
        }
      });
  Future<void> loadJavaReleases() async {
    final result = (await run({'op': 'java.releases'}) as List).cast<int>();
    if (!mounted) return;
    setState(() {
      javaReleases = result;
      if (!javaReleases.contains(major)) major = javaReleases.first;
    });
  }

  Future<void> loadGames() async {
    final result = await run({'op': 'game.versions'}) as Map;
    if (!mounted) return;
    setState(() =>
        games = (result['versions'] as List).cast<Map<String, dynamic>>());
  }

  Future<void> detect() => guard(() async {
        final p =
            Map<String, dynamic>.from(await run({'op': 'platform'}) as Map);
        if (mounted) {
          setState(() {
            host = p;
            os = p['os'] as String;
            arch = p['arch'] as String;
            builds.clear();
            selectedBuild = null;
            buildsQueried = false;
          });
        }
      });
  Future<void> queryJava() => guard(() async {
        setState(() {
          selectedBuild = null;
          builds = [];
          buildsQueried = true;
        });
        final result = await run({
          'op': 'java.builds',
          'major': major,
          'vendor': vendor,
          'os': os,
          'arch': arch
        }) as List;
        if (mounted) {
          setState(() {
            builds = result.cast<Map<String, dynamic>>();
            selectedBuild = builds.isEmpty ? null : builds.first;
          });
        }
      });
  Future<void> selectLoader(String value) => guard(() async {
        setState(() {
          loader = value;
          loaders = [];
          loaderVersion = '';
          name.text = defaultName;
        });
        if (value == 'vanilla') return;
        final result = await run(
                {'op': 'loader.versions', 'loader': value, 'game': game!['id']})
            as List;
        if (mounted) {
          setState(() {
            loaders = result
                .map((v) =>
                    Map<String, dynamic>.from((v as Map)['loader'] as Map))
                .toList();
            loaderVersion =
                loaders.isEmpty ? '' : loaders.first['version'] as String;
          });
        }
      });
  String get defaultName =>
      '${game?['id'] ?? ''}${loader == 'vanilla' ? '' : '-$loader'}';
  String? get nameError {
    final value = name.text.trim();
    if (!RegExp(r'^[A-Za-z0-9_.+ -]{1,180}$').hasMatch(value) ||
        value == '.' ||
        value == '..') {
      return tr('使用英文字母、数字、空格或 - _ . +');
    }
    if (File('$root/versions/$value/$value.json').existsSync()) {
      return tr('该名称已存在，请换一个名称');
    }
    return null;
  }

  Future<void> installGame() => guard(() async {
        final request = <String, dynamic>{
          'root': root,
          'id': name.text.trim(),
          'version_entry': game,
          'loader': loader
        };
        if (loader != 'vanilla') request['loader_version'] = loaderVersion;
        if (javaChoice.isNotEmpty) {
          request['java_choice'] = {
            'mode': 'fixed',
            'path': javaChoice,
            'allow_below_recommended': forceJava
          };
        }
        final result = Map<String, dynamic>.from(
            await run({'op': 'game.install', 'request': request}) as Map);
        await widget.onInstalled('game', result);
        if (mounted) {
          setState(() {
            notice = tr('游戏已安装，可返回开始页面启动');
            game = null;
          });
        }
      }, install: true);
  Future<void> installJava() => guard(() async {
        final selected = selectedBuild!;
        final result = Map<String, dynamic>.from(await run({
          'op': 'java.install',
          'build': selected,
          'directory': widget.javaDirectory
        }) as Map);
        await widget.onInstalled('java', result);
        if (mounted) setState(() => notice = tr('Java 已安装并加入本地列表'));
      }, install: true);
  Future<void> legal() => guard(() async {
        final result = Map<String, dynamic>.from(
            await run({'op': 'java.build_legal', 'build': selectedBuild})
                as Map);
        if (!mounted) return;
        await showDialog<void>(
            context: context,
            builder: (context) => AlertDialog(
                    title: Text(
                        '${vendors[vendor]} · ${selectedBuild?['version']}'),
                    content: SizedBox(
                        width: 440,
                        child: Column(
                            mainAxisSize: MainAxisSize.min,
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(result['name'] as String),
                              const SizedBox(height: 12),
                              Text(nativeText(result['scope'] as String),
                                  style: TextStyle(color: muted, height: 1.6)),
                            ])),
                    actions: [
                      TextButton(
                          onPressed: () {
                            Navigator.pop(context);
                            unawaited(open(result['license_url'] as String));
                          },
                          child: Text(tr('查看许可证'))),
                      TextButton(
                          onPressed: () => Navigator.pop(context),
                          child: Text(tr('完成'))),
                    ]));
      });
  Future<void> open(String url) async {
    // Browser opening is a short core command; it is not a download job.
    try {
      await run({'op': 'resource.open', 'url': url});
    } on EngineFailure catch (e) {
      if (mounted) setState(() => problem = e.details);
    }
  }

  Future<void> chooseRoot() async {
    final value = await getDirectoryPath(
        initialDirectory: Directory(root).existsSync() ? root : null,
        confirmButtonText: tr('选择游戏目录'));
    if (value != null && mounted) setState(() => root = value);
  }

  Widget select<T>(
          String label, T value, Map<T, String> choices, ValueChanged<T> change,
          {Key? key}) =>
      Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Text(label, style: TextStyle(fontSize: 12, color: muted)),
        const SizedBox(height: 8),
        DropdownButtonFormField<T>(
            key: key,
            value: choices.containsKey(value) ? value : null,
            isExpanded: true,
            borderRadius: BorderRadius.circular(10),
            style: TextStyle(fontSize: 13, color: ink),
            items: choices.entries
                .map((e) => DropdownMenuItem<T>(
                    value: e.key,
                    child: Text(e.value,
                        maxLines: 1, overflow: TextOverflow.ellipsis)))
                .toList(),
            onChanged: locked
                ? null
                : (v) {
                    if (v != null) change(v);
                  }),
      ]);
  Widget inlineSelect<T>(
          String label, T value, Map<T, String> choices, ValueChanged<T> change,
          {Key? key}) =>
      LayoutBuilder(
          builder: (context, bounds) => Row(children: [
                ConstrainedBox(
                    constraints:
                        BoxConstraints(maxWidth: bounds.maxWidth * .45),
                    child: Text(label,
                        style:
                            TextStyle(fontSize: 13, height: 1.35, color: ink))),
                const SizedBox(width: 10),
                Expanded(
                    child: DropdownButtonFormField<T>(
                        key: key,
                        value: choices.containsKey(value) ? value : null,
                        isExpanded: true,
                        borderRadius: BorderRadius.circular(9),
                        style: TextStyle(fontSize: 13, color: ink),
                        decoration: const InputDecoration(
                            contentPadding: EdgeInsets.symmetric(
                                horizontal: 12, vertical: 12)),
                        items: choices.entries
                            .map((e) => DropdownMenuItem<T>(
                                value: e.key,
                                child: Text(e.value,
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis)))
                            .toList(),
                        onChanged: locked
                            ? null
                            : (v) {
                                if (v != null) change(v);
                              }))
              ]));
  Widget switches(String current, Map<String, String> choices,
          ValueChanged<String> change) =>
      Wrap(
          spacing: 8,
          runSpacing: 8,
          children: choices.entries
              .map((e) => ChoiceChip(
                  label: Text(e.value),
                  selected: current == e.key,
                  showCheckmark: false,
                  selectedColor: accent.withOpacity(.10),
                  backgroundColor: panelSurface.withOpacity(.35),
                  labelStyle: TextStyle(
                      fontSize: 13,
                      color: current == e.key ? accent : muted,
                      fontWeight:
                          current == e.key ? FontWeight.w600 : FontWeight.w400),
                  side: BorderSide(
                      color: current == e.key
                          ? accent.withOpacity(.3)
                          : line.withOpacity(.65)),
                  onSelected: locked ? null : (_) => change(e.key)))
              .toList());
  Widget empty(String title, {IconData icon = Icons.download_outlined}) =>
      Center(
          child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(mainAxisSize: MainAxisSize.min, children: [
                Icon(icon, color: muted.withOpacity(.5), size: 36),
                const SizedBox(height: 14),
                Text(title,
                    textAlign: TextAlign.center,
                    style: TextStyle(color: muted, height: 1.6))
              ])));
  Widget get gameList {
    final visible = games
        .where((v) =>
            v['channel'] == channel &&
            v['id'].toString().toLowerCase().contains(query.toLowerCase()))
        .toList();
    return Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
      Row(children: [
        Expanded(
            child: TextField(
                key: const Key('gameVersionSearch'),
                controller: search,
                decoration: InputDecoration(
                    hintText: tr('搜索游戏版本'),
                    prefixIcon: const Icon(Icons.search, size: 19)),
                onChanged: (s) => setState(() => query = s))),
        const SizedBox(width: 10),
        IconButton(
            tooltip: tr('刷新'),
            onPressed: locked ? null : () => guard(loadGames),
            icon: const Icon(Icons.refresh_rounded, size: 20))
      ]),
      const SizedBox(height: 12),
      switches(
          channel,
          {
            'release': tr('正式版'),
            'snapshot': tr('快照 / 测试版'),
            'special': tr('特殊版本'),
            'historical': tr('历史版本')
          },
          (v) => setState(() => channel = v)),
      const SizedBox(height: 16),
      Expanded(
          child: visible.isEmpty
              ? empty(tr('没有匹配的版本'))
              : ListView.separated(
                  key: const Key('gameVersions'),
                  padding: EdgeInsets.zero,
                  itemCount: visible.length,
                  separatorBuilder: (_, __) =>
                      Divider(color: line.withOpacity(.65)),
                  itemBuilder: (context, i) {
                    final v = visible[i];
                    return ListTile(
                        key: ValueKey('game-${v['id']}'),
                        contentPadding: const EdgeInsets.symmetric(
                            horizontal: 8, vertical: 8),
                        leading: Container(
                            width: 38,
                            height: 38,
                            decoration: BoxDecoration(
                                color: accent.withOpacity(.08),
                                borderRadius: BorderRadius.circular(9)),
                            child: Icon(Icons.sports_esports_outlined,
                                color: accent, size: 20)),
                        title: Text(v['id'] as String,
                            style: const TextStyle(
                                fontWeight: FontWeight.w600, fontSize: 14)),
                        subtitle: Padding(
                            padding: const EdgeInsets.only(top: 4),
                            child: Text(
                                v['releaseTime'].toString().split('T').first,
                                style: TextStyle(color: muted, fontSize: 12))),
                        trailing: Icon(Icons.chevron_right_rounded,
                            color: muted, size: 21),
                        onTap: locked
                            ? null
                            : () => setState(() {
                                  game = v;
                                  loader = 'vanilla';
                                  loaders = [];
                                  loaderVersion = '';
                                  javaChoice = '';
                                  forceJava = false;
                                  name.text = defaultName;
                                  notice = null;
                                }));
                  }))
    ]);
  }

  Widget get gameSetup => SingleChildScrollView(
          child:
              Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        TextButton.icon(
            onPressed: locked ? null : () => setState(() => game = null),
            icon: const Icon(Icons.arrow_back_rounded, size: 18),
            label: Text(tr('返回版本列表'))),
        const SizedBox(height: 12),
        Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4, vertical: 4),
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text('Minecraft ${game!['id']}',
                  style: const TextStyle(
                      fontSize: 20, fontWeight: FontWeight.w600)),
              const SizedBox(height: 20),
              TextField(
                  key: const Key('gameInstanceName'),
                  controller: name,
                  enabled: !locked,
                  onChanged: (_) => setState(() {}),
                  decoration: InputDecoration(
                      labelText: tr('实例名称'), errorText: nameError)),
              const SizedBox(height: 20),
              Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Expanded(
                    child: select(
                        tr('加载器'),
                        loader,
                        {
                          'vanilla': tr('原版'),
                          'fabric': 'Fabric',
                          'forge': 'Forge',
                          'neoforge': 'NeoForge',
                          'quilt': 'Quilt'
                        },
                        (v) => unawaited(selectLoader(v)),
                        key: const Key('gameLoader'))),
                if (loader != 'vanilla') ...[
                  const SizedBox(width: 14),
                  Expanded(
                      child: select(
                          tr('加载器版本'),
                          loaderVersion,
                          {
                            for (final l in loaders)
                              l['version'] as String:
                                  '${l['version']}${l['stable'] == true ? ' · ${tr('正式版')}' : ''}'
                          },
                          (v) => setState(() => loaderVersion = v),
                          key: const Key('gameLoaderVersion')))
                ],
              ]),
              if (loader != 'vanilla' && loaders.isEmpty)
                Padding(
                    padding: const EdgeInsets.only(top: 10),
                    child: Text(tr('该版本没有可用的加载器构建'),
                        style: TextStyle(color: muted))),
              const SizedBox(height: 20),
              select(
                  tr('Java 运行环境'),
                  javaChoice,
                  {
                    '': tr('自动匹配，缺少时下载官方运行时'),
                    for (final j in widget.javas)
                      j['path'] as String:
                          'Java ${j['version']} · ${j['vendor']} · ${j['arch']}'
                  },
                  (v) => setState(() {
                        javaChoice = v;
                        forceJava = false;
                      }),
                  key: const Key('installGameJava')),
              if (javaChoice.isNotEmpty)
                CheckboxListTile(
                    contentPadding: EdgeInsets.zero,
                    title: Text(tr('允许使用低于推荐版本的 Java'),
                        style: const TextStyle(fontSize: 12)),
                    value: forceJava,
                    onChanged: locked
                        ? null
                        : (v) => setState(() => forceJava = v ?? false)),
              const SizedBox(height: 20),
              Text(tr('游戏目录'), style: TextStyle(fontSize: 12, color: muted)),
              const SizedBox(height: 8),
              Row(children: [
                Expanded(
                    child: SelectableText(root,
                        style:
                            TextStyle(fontSize: 12, color: ink, height: 1.5))),
                const SizedBox(width: 12),
                OutlinedButton.icon(
                    onPressed: locked ? null : chooseRoot,
                    icon: const Icon(Icons.folder_open_outlined, size: 18),
                    label: Text(tr('更改')))
              ]),
              const SizedBox(height: 14),
              Text(tr('存档和设置按实例隔离；游戏文件与依赖下载后进行哈希校验。'),
                  style: TextStyle(fontSize: 12, color: muted, height: 1.6)),
            ])),
      ]));
  Widget get javaPage => SingleChildScrollView(
          child:
              Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          Expanded(
              child: inlineSelect(
                  tr('主版本'),
                  major,
                  {
                    for (final v in [
                      for (final common in [25, 21, 17, 16, 11, 8])
                        if (javaReleases.contains(common)) common,
                      ...javaReleases
                          .where((v) => ![25, 21, 17, 16, 11, 8].contains(v))
                    ])
                      v: 'Java $v'
                  },
                  (v) => invalidate(() => major = v),
                  key: const Key('javaMajor'))),
          const SizedBox(width: 20),
          Expanded(
              flex: 2,
              child: inlineSelect(tr('发行版'), vendor, vendors,
                  (v) => invalidate(() => vendor = v),
                  key: const Key('javaVendor')))
        ]),
        const SizedBox(height: 16),
        Row(children: [
          Expanded(
              child: inlineSelect(
                  tr('操作系统'),
                  os,
                  systems,
                  (v) => invalidate(() {
                        os = v;
                        if (v == 'osx' && arch == 'x86') arch = 'x86_64';
                      }),
                  key: const Key('javaOS'))),
          const SizedBox(width: 20),
          Expanded(
              flex: 2,
              child: inlineSelect(
                  tr('架构'),
                  arch,
                  {
                    for (final e in arches.entries)
                      if (os != 'osx' || ['x86_64', 'aarch64'].contains(e.key))
                        e.key: e.value
                  },
                  (v) => invalidate(() => arch = v),
                  key: const Key('javaArch')))
        ]),
        const SizedBox(height: 14),
        Row(children: [
          OutlinedButton.icon(
              onPressed: locked ? null : detect,
              icon: const Icon(Icons.radar_rounded, size: 17),
              label: Text(tr('自动检测'))),
          const Spacer(),
          FilledButton.icon(
              key: const Key('queryJavaBuilds'),
              onPressed: locked || os.isEmpty || !javaReleases.contains(major)
                  ? null
                  : queryJava,
              icon: const Icon(Icons.search_rounded, size: 17),
              label: Text(tr('查询构建')))
        ]),
        const SizedBox(height: 24),
        Row(children: [
          Expanded(
              child: Text(tr('具体构建'),
                  style: const TextStyle(
                      fontSize: 15, fontWeight: FontWeight.w600))),
          TextButton.icon(
              onPressed: locked
                  ? null
                  : () => unawaited(open({
                        'zulu': 'https://www.azul.com/downloads/',
                        'temurin': 'https://adoptium.net/',
                        'microsoft':
                            'https://learn.microsoft.com/java/openjdk/download',
                        'oracle-openjdk': 'https://openjdk.org/'
                      }[vendor]!)),
              icon: const Icon(Icons.open_in_new_rounded, size: 15),
              label: Text(tr('发行版官网')))
        ]),
        const SizedBox(height: 10),
        KeyedSubtree(
            key: const Key('javaBuilds'),
            child: ManagementTable(
                headers: [
                  const SizedBox(),
                  Text(tr('版本 / 构建')),
                  Text(tr('架构')),
                  Text(tr('类型')),
                  Text(tr('下载大小'))
                ],
                widths: const {
                  0: FixedColumnWidth(42),
                  1: FlexColumnWidth(3.6),
                  2: FlexColumnWidth(2),
                  3: FlexColumnWidth(1.2),
                  4: FlexColumnWidth(1.8)
                },
                selected: selectedBuild == null
                    ? {}
                    : {builds.indexOf(selectedBuild!)},
                rows: [
                  for (var i = 0; i < builds.length; i++)
                    [
                      Radio<int>(
                          key: ValueKey('selectedBuild-$i'),
                          value: i,
                          groupValue: selectedBuild == null
                              ? null
                              : builds.indexOf(selectedBuild!),
                          visualDensity: VisualDensity.compact,
                          onChanged: locked
                              ? null
                              : (_) =>
                                  setState(() => selectedBuild = builds[i])),
                      InkWell(
                          onTap: locked
                              ? null
                              : () => setState(() => selectedBuild = builds[i]),
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text(builds[i]['version'] as String,
                                    style: const TextStyle(
                                        fontSize: 14,
                                        fontWeight: FontWeight.w600,
                                        height: 1.5)),
                                const SizedBox(height: 4),
                                Text(
                                    '${vendors[builds[i]['provider']]}${(builds[i]['distribution_version'] as String? ?? '').isEmpty ? '' : ' ${builds[i]['distribution_version']}'}',
                                    style: TextStyle(
                                        fontSize: 11,
                                        color: muted,
                                        height: 1.5))
                              ])),
                      Text(
                          arches[builds[i]['arch']] ??
                              builds[i]['arch'].toString(),
                          style: TextStyle(fontSize: 12, color: muted)),
                      Text(builds[i]['type'].toString(),
                          style: TextStyle(fontSize: 12, color: muted)),
                      Text(
                          (builds[i]['size'] as num? ?? -1) > 0
                              ? byteLabel(builds[i]['size'] as num)
                              : '—',
                          style: TextStyle(fontSize: 12, color: muted))
                    ]
                ])),
        if (builds.isEmpty)
          SizedBox(
              height: 180,
              child: empty(buildsQueried
                  ? tr('没有匹配的构建，请更改版本、发行版或架构')
                  : tr('点击「查询构建」查看可安装的 Java')))
      ]));
  void invalidate(VoidCallback change) => setState(() {
        change();
        builds = [];
        selectedBuild = null;
        buildsQueried = false;
        problem = null;
        notice = null;
      });
  Widget get progressPanel {
    final p = widget.engine.progress;
    final total = (p['total'] as num? ?? 0).toDouble(),
        done = (p['done'] as num? ?? 0).toDouble();
    return SurfaceCard(
        color: seclColor(0xFFEDF3F8),
        child: Row(children: [
          const SizedBox(
              width: 22,
              height: 22,
              child: CircularProgressIndicator(strokeWidth: 2)),
          const SizedBox(width: 14),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  mainAxisSize: MainAxisSize.min,
                  children: [
                Text(nativeText(p['message']?.toString() ?? tr('准备下载')),
                    style: const TextStyle(
                        fontSize: 13, fontWeight: FontWeight.w600)),
                const SizedBox(height: 8),
                LinearProgressIndicator(
                    value: total > 0 ? (done / total).clamp(0, 1) : null),
                if (total > 0)
                  Padding(
                      padding: const EdgeInsets.only(top: 6),
                      child: Text('${(done / total * 100).toStringAsFixed(0)}%',
                          style: TextStyle(fontSize: 11, color: muted))),
              ])),
          const SizedBox(width: 12),
          TextButton(
              onPressed: () => widget.engine.cancel(), child: Text(tr('取消'))),
        ]));
  }

  @override
  Widget build(BuildContext context) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        PageMasthead(title: tr('下载'), actions: [
          if (widget.onImport != null && tab == 'game')
            OutlinedButton.icon(
                key: const Key('importModpackDownload'),
                onPressed: locked ? null : widget.onImport,
                icon: const Icon(Icons.inventory_2_outlined, size: 17),
                label: Text(tr('导入整合包'))),
          SizedBox(
              width: 190,
              child: DropdownButtonFormField<String>(
                  key: const Key('downloadSource'),
                  isExpanded: true,
                  value: widget.source,
                  style: TextStyle(fontSize: 12, color: ink),
                  borderRadius: BorderRadius.circular(9),
                  decoration: const InputDecoration(
                      prefixIcon: Icon(Icons.cloud_download_outlined, size: 17),
                      contentPadding:
                          EdgeInsets.symmetric(horizontal: 10, vertical: 11)),
                  items: {
                    'auto': tr('自动选择源'),
                    'official': tr('官方源'),
                    'bmclapi': 'BMCLAPI'
                  }
                      .entries
                      .map((e) => DropdownMenuItem(
                          value: e.key,
                          child: Text(e.value,
                              maxLines: 1, overflow: TextOverflow.ellipsis)))
                      .toList(),
                  onChanged: locked
                      ? null
                      : (v) {
                          if (v != null) unawaited(widget.onSource(v));
                        }))
        ]),
        Padding(
            padding: const EdgeInsets.symmetric(horizontal: 28),
            child:
                switches(tab, {'game': tr('游戏安装'), 'java': tr('安装 Java')}, (v) {
              setState(() {
                tab = v;
                problem = null;
                notice = null;
              });
              if (v == 'game' && games.isEmpty) unawaited(guard(loadGames));
              if (v == 'java' && javaReleases.isEmpty) {
                unawaited(guard(loadJavaReleases));
              }
            })),
        const SizedBox(height: 18),
        Expanded(
            child: Padding(
                padding: const EdgeInsets.fromLTRB(28, 0, 28, 18),
                child: problem != null
                    ? ProblemPanel(problem!, retry: retry)
                    : loading
                        ? BusyPanel(label: tr('正在加载'))
                        : installing
                            ? progressPanel
                            : tab == 'game'
                                ? (game == null ? gameList : gameSetup)
                                : javaPage)),
        FooterBar(
            child: Row(children: [
          Expanded(
              child: Text(
                  notice ??
                      (tab == 'game'
                          ? tr('选择版本后安装到独立实例')
                          : tr('不同构建独立保存，可在开始页面选择')),
                  style: TextStyle(
                      fontSize: 12,
                      color: notice == null ? muted : accent,
                      height: 1.5))),
          const SizedBox(width: 12),
          if (tab == 'java' && selectedBuild != null) ...[
            IconButton(
                tooltip: tr('构建详情 / 许可证'),
                onPressed: locked ? null : legal,
                icon: const Icon(Icons.info_outline_rounded, size: 20)),
            const SizedBox(width: 8)
          ],
          if (tab == 'java' || game != null)
            FilledButton.icon(
                key: const Key('installDownload'),
                onPressed: locked || problem != null
                    ? null
                    : tab == 'game'
                        ? (game != null &&
                                nameError == null &&
                                (loader == 'vanilla' ||
                                    loaderVersion.isNotEmpty)
                            ? installGame
                            : null)
                        : (selectedBuild != null && os == host['os']
                            ? installJava
                            : null),
                icon: const Icon(Icons.download_rounded, size: 18),
                label: Text(tab == 'game'
                    ? tr('下载并安装')
                    : os != host['os']
                        ? tr('仅可安装当前系统的构建')
                        : tr('安装 Java'))),
        ])),
      ]);
}
