// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'localization.dart';
import 'dart:async';
import 'dart:io';
import 'package:intl/intl.dart';
import 'package:flutter/material.dart';
import 'package:file_selector/file_selector.dart';
import 'app_widgets.dart';
import 'resource_engine.dart';

typedef Data = Map<String, dynamic>;
List<Data> maps(dynamic value) => (value as List? ?? [])
    .map((e) => Map<String, dynamic>.from(e as Map))
    .toList();
Data problem(Object e) => e is EngineFailure
    ? e.details
    : {'code': 'invalid_response', 'message': "服务返回的数据无法读取，请重试。"};
String tagName(String s) =>
    {
      'forge': 'Forge',
      'neoforge': 'NeoForge',
      'fabric': 'Fabric',
      'quilt': 'Quilt',
      'vanilla': tr("原版"),
      'iris': 'Iris',
      'optifine': 'OptiFine',
      'adventure': tr("冒险"),
      'decoration': tr("装饰与建筑"),
      'optimization': tr("性能优化"),
      'library': tr("前置与类库"),
      'utility': tr("实用工具"),
      'worldgen': tr("世界生成"),
      'technology': tr("科技"),
      'magic': tr("魔法"),
      'mobs': tr("生物"),
      'storage': tr("存储"),
      'atmosphere': tr("氛围"),
      'bloom': tr("泛光"),
      'colored-lighting': tr("彩色光照"),
      'realistic': tr("写实"),
      'fantasy': tr("奇幻"),
      'high': tr("高配置"),
      'medium': tr("中等配置"),
      'low': tr("低配置"),
      'potato': tr("极低配置"),
      'cartoon': tr("卡通"),
      'foliage': tr("植被"),
      'path-tracing': tr("路径追踪"),
      'pbr': tr("PBR 材质"),
      'shadows': tr('阴影')
    }[s] ??
    s;
String titleFor(String kind) => {
      'mod': tr("模组市场"),
      'shader': tr("光影市场"),
      'resourcepack': tr("材质包市场")
    }[kind]!;
String countLabel(num n) =>
    NumberFormat.compact(locale: AppLanguage.instance.locale.toString())
        .format(n);
bool compatible(Data file, Data instance, String kind) =>
    (file['game_versions'] as List? ?? []).contains(instance['version']) &&
    (kind != 'mod' ||
        (file['loaders'] as List? ?? []).contains(instance['loader']) ||
        (instance['loader'] == 'quilt' &&
            (file['loaders'] as List? ?? []).contains('fabric')));

class BrowseState {
  String query = '', category = '', game = '', loader = '', sort = 'downloads';
  int offset = 0, total = 0, generation = 0;
  bool loading = false, categoriesLoading = false, loaded = false;
  List<Data> hits = [], categories = [];
  Data? error, categoryError;
}

class ResourceMarket extends StatefulWidget {
  const ResourceMarket(
      {super.key,
      required this.kind,
      required this.engine,
      required this.root,
      required this.selectedVersion});
  final String kind, root, selectedVersion;
  final ResourceEngine engine;
  @override
  State<ResourceMarket> createState() => _ResourceMarketState();
}

class _ResourceMarketState extends State<ResourceMarket> {
  final states = <String, BrowseState>{};
  final query = TextEditingController();
  final roots = <String>{};
  List<Data> games = [];
  Data? project, detailError;
  List<Data> files = [];
  bool detailsLoading = false;
  String channel = 'release', fileGame = '', fileLoader = '';
  Data? installedNotice;
  String get notice {
    final result = installedNotice;
    if (result == null) return '';
    final status = result['reused'] == true ? tr('已安装，无需重复下载') : tr('已安装');
    final warnings = (result['warnings'] as List? ?? [])
        .map((w) => nativeText(w.toString()))
        .join(' · ');
    return '$status · ${result['id']}${warnings.isEmpty ? '' : ' · $warnings'}';
  }

  int detailGeneration = 0;
  BrowseState get browse =>
      states.putIfAbsent(widget.kind, () => BrowseState());
  ResourceEngine get engine => widget.engine;
  @override
  void initState() {
    super.initState();
    if (widget.root.isNotEmpty) roots.add(widget.root);
    unawaited(load());
  }

  @override
  void didUpdateWidget(ResourceMarket old) {
    super.didUpdateWidget(old);
    if (widget.root.isNotEmpty) roots.add(widget.root);
    if (old.kind != widget.kind) {
      detailGeneration++;
      project = null;
      files = [];
      detailError = null;
      query.text = browse.query;
      installedNotice = null;
      if (!browse.loaded) unawaited(load());
    }
  }

  @override
  void dispose() {
    query.dispose();
    super.dispose();
  }

  Future<void> load() async {
    await search();
    if (mounted && browse.categories.isEmpty) await categories();
  }

  Future<void> search({bool reset = false}) async {
    final b = browse, kind = widget.kind;
    final generation = ++b.generation;
    if (reset) b.offset = 0;
    b.query = query.text.trim();
    setState(() {
      b.loading = true;
      b.error = null;
      installedNotice = null;
    });
    try {
      final raw = Map<String, dynamic>.from(await engine.run({
        'op': 'resource.search',
        'type': kind,
        'query': b.query,
        'category': b.category,
        'version': b.game,
        'loader': b.loader,
        'sort': b.sort,
        'offset': b.offset
      }, title: tr("正在搜索{0}", [titleFor(kind)])) as Map);
      if (!mounted || generation != b.generation) return;
      setState(() {
        b.hits = maps(raw['hits']);
        b.total = (raw['total_hits'] as num? ?? 0).toInt();
        b.loaded = true;
      });
    } catch (e) {
      if (mounted && generation == b.generation) {
        setState(() => b.error = problem(e));
      }
    } finally {
      if (mounted && generation == b.generation) {
        setState(() => b.loading = false);
      }
    }
  }

  Future<void> categories() async {
    final b = browse, kind = widget.kind;
    setState(() {
      b.categoriesLoading = true;
      b.categoryError = null;
    });
    try {
      final result = maps(await engine.run(
          {'op': 'resource.categories', 'type': kind},
          title: tr("正在读取分类")));
      if (mounted) setState(() => b.categories = result);
    } catch (e) {
      if (mounted) setState(() => b.categoryError = problem(e));
    } finally {
      if (mounted) setState(() => b.categoriesLoading = false);
    }
  }

  Future<List<Choice>> gameOptions() async {
    if (games.isEmpty) {
      games = maps(
          await engine.run({'op': 'resource.games'}, title: tr("正在读取游戏版本")));
    }
    return games
        .map((v) => Choice(v['version'] as String, v['version'] as String,
            group: v['channel'] as String? ?? 'snapshot'))
        .toList();
  }

  Future<void> pickGame() async {
    final result = await showChoicePicker(context,
        title: tr("选择游戏版本"),
        value: browse.game,
        load: gameOptions,
        groups: {
          'release': tr("正式版"),
          'snapshot': tr("测试版"),
          'special': tr("特殊版"),
          'historical': tr("历史版")
        },
        clearLabel: tr("全部版本"));
    if (result != null && mounted) {
      setState(() => browse.game = result);
      await search(reset: true);
    }
  }

  Future<void> pickLoader() async {
    final options = ['forge', 'neoforge', 'fabric', 'quilt'];
    final result = await showChoicePicker(context,
        title: tr("选择加载器"),
        value: browse.loader,
        load: () async => options.map((s) => Choice(s, tagName(s))).toList(),
        clearLabel: tr("全部加载器"));
    if (result != null && mounted) {
      setState(() => browse.loader = result);
      await search(reset: true);
    }
  }

  Future<void> openProject(Data hit) async {
    final generation = ++detailGeneration;
    setState(() {
      project = hit;
      files = [];
      detailError = null;
      detailsLoading = true;
      fileGame = browse.game;
      fileLoader = browse.loader;
      channel = 'release';
      installedNotice = null;
    });
    try {
      final info = Map<String, dynamic>.from(await engine.run(
          {'op': 'resource.project', 'project': hit['project_id'] ?? hit['id']},
          title: tr("正在读取项目详情")) as Map);
      if (!mounted || generation != detailGeneration) return;
      setState(() =>
          project = {...hit, ...info, 'title': info['title'] ?? hit['title']});
      final versions = maps(await engine.run(
          {'op': 'resource.versions', 'project': info['id']},
          title: tr("正在读取可下载版本")));
      if (mounted && generation == detailGeneration) {
        setState(() => files = versions);
      }
    } catch (e) {
      if (mounted && generation == detailGeneration) {
        setState(() => detailError = problem(e));
      }
    } finally {
      if (mounted && generation == detailGeneration) {
        setState(() => detailsLoading = false);
      }
    }
  }

  Future<void> openLink(String? url) async {
    if (url == null || !url.startsWith('https://')) return;
    try {
      await engine
          .run({'op': 'resource.open', 'url': url}, title: tr("正在打开项目网页"));
    } catch (e) {
      if (mounted) setState(() => detailError = problem(e));
    }
  }

  Future<void> install(Data file) async {
    final kind = widget.kind, info = Map<String, dynamic>.from(project!);
    final target = await showDialog<Data>(
        context: context,
        builder: (_) => TargetPicker(
            engine: engine,
            roots: roots,
            file: file,
            kind: kind,
            selectedVersion: widget.selectedVersion));
    if (target == null || !mounted) return;
    setState(() => installedNotice = null);
    try {
      final payload = {
        ...file,
        'title': info['title'],
        'icon_url': info['icon_url'],
        'description': info['description']
      };
      final plan = Map<String, dynamic>.from(await engine.run(
          kind == 'mod'
              ? {'op': 'mods.plan', 'instance': target, 'file': payload}
              : {
                  'op': 'packs.plan',
                  'instance': target,
                  'file': payload,
                  'kind': kind
                },
          title: tr("正在检查已有组件和依赖")) as Map);
      if (kind == 'mod') {
        final ids = <String>{
          for (final f in maps(plan['files'])) f['project_id'] as String,
          for (final r in maps(plan['reuse'])) r['project_id'] as String
        };
        try {
          final metadata = maps(await engine.run(
              {'op': 'resource.projects', 'ids': ids.toList()},
              title: tr("正在读取组件信息")));
          for (final entry in maps(plan['files'])) {
            final meta = metadata
                .where((m) => m['id'] == entry['project_id'])
                .firstOrNull;
            if (meta != null) {
              entry['title'] = meta['title'];
              entry['icon_url'] = meta['icon_url'];
            }
            final index = (plan['files'] as List).indexWhere(
                (dynamic f) => f['project_id'] == entry['project_id']);
            (plan['files'] as List)[index] = entry;
          }
          for (final dynamic entry in (plan['reuse'] as List? ?? [])) {
            final meta = metadata
                .where((m) => m['id'] == entry['project_id'])
                .firstOrNull;
            if (meta != null) {
              entry['file'] = {
                ...Map<String, dynamic>.from(entry['file'] as Map),
                'title': meta['title'],
                'icon_url': meta['icon_url']
              };
            }
          }
        } catch (_) {
          /* Project images are optional; the verified install plan remains usable. */
        }
      }
      if (!mounted) return;
      final choice = await showDialog<Data>(
          context: context,
          barrierDismissible: false,
          builder: (_) => InstallPlanDialog(plan: plan, kind: kind));
      if (choice == null || !mounted) return;
      Data chosen = plan;
      if (kind == 'mod') {
        chosen = Map<String, dynamic>.from(await engine.run({
          'op': 'mods.select',
          'plan': plan,
          'selected': choice['selected'],
          'redownload': choice['redownload'],
          'enable': choice['enable']
        }, title: tr("正在确认依赖选择")) as Map);
        chosen['disable_files'] = choice['disable'];
      } else {
        chosen = {...plan, ...choice};
      }
      final result = Map<String, dynamic>.from(await engine.run(
          {'op': kind == 'mod' ? 'mods.apply' : 'packs.apply', 'plan': chosen},
          title: tr("正在安装到 {0}", [target['id']])) as Map);
      if (mounted) {
        setState(() => installedNotice = {...result, 'id': target['id']});
      }
    } catch (e) {
      if (mounted) setState(() => detailError = problem(e));
    }
  }

  Widget filterButton(String value, VoidCallback? onTap, {Key? key}) =>
      OutlinedButton(
          key: key,
          onPressed: onTap,
          style: OutlinedButton.styleFrom(
              padding:
                  const EdgeInsets.symmetric(horizontal: 12, vertical: 12)),
          child: Row(mainAxisSize: MainAxisSize.min, children: [
            Text(value, style: const TextStyle(fontSize: 13)),
            const SizedBox(width: 14),
            const Icon(Icons.keyboard_arrow_down_rounded, size: 18)
          ]));
  Widget header() => Padding(
      padding: EdgeInsets.fromLTRB(28, Platform.isMacOS ? 44 : 22, 28, 18),
      child: Row(children: [
        Expanded(
            child: Text(titleFor(widget.kind),
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: const TextStyle(
                    fontSize: 25, fontWeight: FontWeight.w600))),
        TextButton.icon(
            onPressed: engine.active
                ? null
                : () => openLink(
                    'https://modrinth.com/${widget.kind == 'shader' ? 'shaders' : widget.kind == 'resourcepack' ? 'resourcepacks' : 'mods'}'),
            icon: const Icon(Icons.open_in_new, size: 14),
            label: const Text('Modrinth', style: TextStyle(fontSize: 12)))
      ]));
  Widget catalog() =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          Expanded(
              child: TextField(
                  key: const Key('resourceSearch'),
                  controller: query,
                  onSubmitted: (_) => search(reset: true),
                  decoration: InputDecoration(
                      hintText: tr("搜索名称、作者或关键词…"),
                      prefixIcon: const Icon(Icons.search, size: 19)))),
          const SizedBox(width: 10),
          FilledButton(
              onPressed: browse.loading ? null : () => search(reset: true),
              child: Text(tr("搜索")))
        ]),
        const SizedBox(height: 12),
        Wrap(
            spacing: 10,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              Text(tr("游戏版本"),
                  style: const TextStyle(fontSize: 12, color: muted)),
              filterButton(
                  browse.game.isEmpty ? tr("全部版本") : browse.game, pickGame,
                  key: const Key('gameFilter')),
              if (widget.kind == 'mod') ...[
                Text(tr("加载器"),
                    style: const TextStyle(fontSize: 12, color: muted)),
                filterButton(
                    browse.loader.isEmpty
                        ? tr("全部加载器")
                        : tagName(browse.loader),
                    pickLoader)
              ],
              PopupMenuButton<String>(
                  tooltip: tr("排序"),
                  initialValue: browse.sort,
                  shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10)),
                  onSelected: (value) {
                    setState(() => browse.sort = value);
                    unawaited(search(reset: true));
                  },
                  itemBuilder: (_) => [
                        PopupMenuItem(
                            value: 'downloads', child: Text(tr("下载最多"))),
                        PopupMenuItem(
                            value: 'relevance', child: Text(tr("相关度"))),
                        PopupMenuItem(
                            value: 'updated', child: Text(tr("最近更新"))),
                        PopupMenuItem(value: 'newest', child: Text(tr("最新发布")))
                      ],
                  child: Padding(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 8, vertical: 10),
                      child: Row(mainAxisSize: MainAxisSize.min, children: [
                        const Icon(Icons.sort, size: 17, color: muted),
                        const SizedBox(width: 6),
                        Text(
                            {
                              'downloads': tr("下载最多"),
                              'relevance': tr("相关度"),
                              'updated': tr("最近更新"),
                              'newest': tr("最新发布")
                            }[browse.sort]!,
                            style: const TextStyle(fontSize: 12, color: muted))
                      ])))
            ]),
        const SizedBox(height: 16),
        Expanded(
            child:
                Row(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          SizedBox(
              width: AppLanguage.instance.code == 'en'
                  ? ((MediaQuery.sizeOf(context).width - 224) * .22)
                      .clamp(168.0, 200.0)
                  : 148,
              child: SurfaceCard(
                  padding: const EdgeInsets.all(6),
                  child: Column(children: [
                    categoryRow('', tr("全部分类"), Icons.grid_view_rounded),
                    Expanded(
                        child: browse.categoriesLoading
                            ? const BusyPanel()
                            : browse.categoryError != null
                                ? Column(
                                    mainAxisAlignment: MainAxisAlignment.center,
                                    children: [
                                        const Icon(Icons.cloud_off,
                                            size: 22, color: muted),
                                        const SizedBox(height: 8),
                                        Text(tr("分类未加载"),
                                            style: const TextStyle(
                                                fontSize: 12, color: muted)),
                                        TextButton(
                                            onPressed: categories,
                                            child: Text(tr("重试")))
                                      ])
                                : ListView.builder(
                                    itemCount: browse.categories.length,
                                    itemBuilder: (_, i) {
                                      final c = browse.categories[i];
                                      return categoryRow(
                                          c['name'] as String,
                                          tagName(c['name'] as String) !=
                                                  c['name']
                                              ? tagName(c['name'] as String)
                                              : nativeText(
                                                  c['label'] as String? ??
                                                      c['name'] as String),
                                          categoryIcon(c['name'] as String));
                                    }))
                  ]))),
          const SizedBox(width: 16),
          Expanded(
              child: Column(children: [
            Expanded(
                child: browse.loading
                    ? const BusyPanel()
                    : browse.error != null
                        ? ProblemPanel(browse.error!, retry: search)
                        : browse.hits.isEmpty
                            ? Center(
                                child: Text(tr("没有找到匹配的资源\n试试更换关键词或筛选条件"),
                                    textAlign: TextAlign.center,
                                    style: const TextStyle(
                                        color: muted, height: 1.8)))
                            : ListView.separated(
                                key: PageStorageKey(
                                    'catalog-${widget.kind}-${browse.offset}'),
                                itemCount: browse.hits.length,
                                separatorBuilder: (_, __) =>
                                    const SizedBox(height: 10),
                                itemBuilder: (_, i) =>
                                    projectCard(browse.hits[i])))
          ]))
        ]))
      ]);
  IconData categoryIcon(String category) =>
      const {
        'optimization': Icons.speed,
        'adventure': Icons.explore_outlined,
        'decoration': Icons.architecture,
        'library': Icons.account_tree_outlined,
        'utility': Icons.build_outlined,
        'worldgen': Icons.landscape_outlined,
        'food': Icons.restaurant_outlined,
        'economy': Icons.payments_outlined,
        'equipment': Icons.shield_outlined,
        'game-mechanics': Icons.settings_outlined,
        'storage': Icons.inventory_2_outlined,
        'mobs': Icons.pets_outlined,
        'social': Icons.forum_outlined,
        'transportation': Icons.train_outlined,
        'technology': Icons.memory,
        'magic': Icons.auto_awesome_outlined,
        'atmosphere': Icons.cloud_outlined,
        'colored-lighting': Icons.light_mode_outlined,
        'realistic': Icons.photo_outlined,
        'high': Icons.speed,
        'low': Icons.eco_outlined
      }[category] ??
      (widget.kind == 'shader'
          ? Icons.wb_twilight_outlined
          : widget.kind == 'resourcepack'
              ? Icons.layers_outlined
              : Icons.extension_outlined);
  Widget categoryRow(String value, String label, IconData icon) => Material(
      color: browse.category == value
          ? const Color(0xFFEBF1F7)
          : Colors.transparent,
      borderRadius: BorderRadius.circular(7),
      child: InkWell(
          borderRadius: BorderRadius.circular(7),
          onTap: () {
            setState(() => browse.category = value);
            unawaited(search(reset: true));
          },
          child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 11),
              child: Row(children: [
                Icon(icon,
                    size: 16, color: browse.category == value ? accent : muted),
                const SizedBox(width: 8),
                Expanded(
                    child: Text(label,
                        softWrap: true,
                        style: TextStyle(
                            height: 1.4,
                            fontSize: 12,
                            color: browse.category == value ? accent : ink,
                            fontWeight: browse.category == value
                                ? FontWeight.w600
                                : FontWeight.w400)))
              ]))));
  Widget projectCard(Data hit) => SurfaceCard(
      padding: const EdgeInsets.all(14),
      child: InkWell(
          onTap: () => openProject(hit),
          child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            ResourceImage(hit['icon_url'] as String?, size: 52),
            const SizedBox(width: 13),
            Expanded(
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                  Text(hit['title'] as String? ?? '',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                          fontSize: 15, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 5),
                  Text(hit['description'] as String? ?? '',
                      maxLines: 2,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(
                          fontSize: 12, color: muted, height: 1.5)),
                  const SizedBox(height: 9),
                  Wrap(spacing: 5, runSpacing: 4, children: [
                    for (final tag in (hit['display_categories'] as List? ??
                            hit['categories'] as List? ??
                            [])
                        .take(5))
                      chip(nativeText(browse.categories
                              .where((c) => c['name'] == tag)
                              .firstOrNull?['label'] as String? ??
                          tagName(tag.toString())))
                  ]),
                  const SizedBox(height: 9),
                  Row(children: [
                    Expanded(
                        child: Text(
                            tr("{0} 次下载",
                                [countLabel(hit['downloads'] as num? ?? 0)]),
                            style:
                                const TextStyle(fontSize: 11, color: muted))),
                    Text(tr("查看详情"),
                        style: TextStyle(
                            fontSize: 12,
                            color: Theme.of(context).colorScheme.primary)),
                    const SizedBox(width: 3),
                    const Icon(Icons.chevron_right, size: 15, color: accent)
                  ])
                ]))
          ])));
  Widget chip(String label) => Container(
      padding: const EdgeInsets.symmetric(horizontal: 7, vertical: 3),
      decoration: BoxDecoration(
          color: const Color(0xFFEEF2F6),
          borderRadius: BorderRadius.circular(5)),
      child: Text(label, style: const TextStyle(fontSize: 10, color: accent)));
  List<Data> get matching => files
      .where((f) =>
          (fileGame.isEmpty ||
              (f['game_versions'] as List).contains(fileGame)) &&
          (fileLoader.isEmpty || (f['loaders'] as List).contains(fileLoader)))
      .toList();
  Future<void> selectFileGame() async {
    final values = <String>{
      for (final f in files)
        for (final v in f['game_versions'] as List) v.toString()
    }.toList();
    final result = await showChoicePicker(context,
        title: tr("筛选适用的游戏版本"),
        value: fileGame,
        load: () async => values
            .map((v) => Choice(v, v,
                group: RegExp(r'^\d+\.\d+(\.\d+)*$').hasMatch(v)
                    ? 'release'
                    : 'snapshot'))
            .toList(),
        groups: {'release': tr("正式版"), 'snapshot': tr("测试版")},
        clearLabel: tr("全部版本"));
    if (result != null && mounted) setState(() => fileGame = result);
  }

  Widget detail() =>
      Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Row(children: [
          TextButton.icon(
              key: const Key('backToCatalog'),
              onPressed: () {
                detailGeneration++;
                setState(() {
                  project = null;
                  detailError = null;
                });
              },
              icon: const Icon(Icons.arrow_back_rounded, size: 17),
              label: Text(tr("返回列表"))),
          const Spacer(),
          TextButton.icon(
              onPressed: () => openLink(
                  'https://modrinth.com/${widget.kind == 'mod' ? 'mod' : widget.kind == 'shader' ? 'shader' : 'resourcepack'}/${project!['slug'] ?? project!['project_id'] ?? project!['id']}'),
              icon: const Icon(Icons.open_in_new, size: 15),
              label: Text(tr("项目主页")))
        ]),
        const SizedBox(height: 8),
        Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          ResourceImage(project!['icon_url'] as String?, size: 60),
          const SizedBox(width: 14),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(project!['title'] as String? ?? '',
                    style: const TextStyle(
                        fontSize: 20, fontWeight: FontWeight.w600)),
                const SizedBox(height: 5),
                Text(
                    '${project!['author'] ?? tr("社区项目")} · ${project!['license'] is Map ? (project!['license'] as Map)['id'] ?? tr("作者许可") : 'Modrinth'}',
                    style: const TextStyle(fontSize: 12, color: muted))
              ]))
        ]),
        const SizedBox(height: 12),
        Expanded(
            child: detailError != null
                ? ProblemPanel(detailError!, retry: () => openProject(project!))
                : Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                        Flexible(
                            flex: 0,
                            child: ConstrainedBox(
                                constraints:
                                    const BoxConstraints(maxHeight: 140),
                                child: SingleChildScrollView(
                                    child: Column(
                                        crossAxisAlignment:
                                            CrossAxisAlignment.start,
                                        children: [
                                      Align(
                                          alignment: Alignment.centerLeft,
                                          child: SelectableText(
                                              project!['description']
                                                      as String? ??
                                                  '',
                                              style: const TextStyle(
                                                  fontSize: 13,
                                                  height: 1.6,
                                                  color: muted))),
                                      if ((project!['gallery'] as List? ?? [])
                                          .isNotEmpty)
                                        Padding(
                                            padding:
                                                const EdgeInsets.only(top: 10),
                                            child: SizedBox(
                                                height: 86,
                                                child: ListView.separated(
                                                    scrollDirection:
                                                        Axis.horizontal,
                                                    itemCount: (project!['gallery']
                                                            as List)
                                                        .length
                                                        .clamp(0, 6),
                                                    separatorBuilder: (_, __) =>
                                                        const SizedBox(
                                                            width: 8),
                                                    itemBuilder: (_, i) => ClipRRect(
                                                        borderRadius:
                                                            BorderRadius.circular(7),
                                                        child: Image.network((project!['gallery'] as List)[i]['url'] as String, width: 150, fit: BoxFit.cover, errorBuilder: (_, __, ___) => const SizedBox(width: 0))))))
                                    ])))),
                        const SizedBox(height: 12),
                        Wrap(
                            spacing: 8,
                            runSpacing: 6,
                            crossAxisAlignment: WrapCrossAlignment.center,
                            children: [
                              filterButton(
                                  fileGame.isEmpty ? tr("全部游戏版本") : fileGame,
                                  files.isEmpty ? null : selectFileGame),
                              if (widget.kind == 'mod')
                                for (final loader in [
                                  '',
                                  'forge',
                                  'neoforge',
                                  'fabric',
                                  'quilt'
                                ].where((s) =>
                                    s.isEmpty ||
                                    files.any((f) =>
                                        (f['loaders'] as List).contains(s))))
                                  ChoiceChip(
                                      label: Text(
                                          loader.isEmpty
                                              ? tr("全部加载器")
                                              : tagName(loader),
                                          style: const TextStyle(fontSize: 11)),
                                      selected: fileLoader == loader,
                                      onSelected: (_) =>
                                          setState(() => fileLoader = loader),
                                      visualDensity: VisualDensity.compact)
                            ]),
                        const SizedBox(height: 10),
                        Row(children: [
                          for (final c in {
                            'release': tr("正式版"),
                            'beta': tr("测试版"),
                            'alpha': tr("预览版")
                          }.entries)
                            Padding(
                                padding: const EdgeInsets.only(right: 8),
                                child: TextButton(
                                    onPressed: () =>
                                        setState(() => channel = c.key),
                                    style: TextButton.styleFrom(
                                        foregroundColor:
                                            channel == c.key ? accent : muted,
                                        backgroundColor: channel == c.key
                                            ? const Color(0xFFEBF1F7)
                                            : null),
                                    child: Text(
                                        '${c.value} (${matching.where((f) => f['release_type'] == c.key).length})',
                                        style: const TextStyle(fontSize: 12))))
                        ]),
                        const SizedBox(height: 8),
                        Expanded(
                            child: detailsLoading
                                ? const BusyPanel()
                                : versionList())
                      ]))
      ]);
  Widget versionList() {
    final versions =
        matching.where((f) => f['release_type'] == channel).toList();
    if (versions.isEmpty) {
      return Center(
          child: Text(
              tr("没有匹配的{0}文件\n可切换分类或调整游戏版本、加载器", [
                {
                  'release': tr("正式版"),
                  'beta': tr("测试版"),
                  'alpha': tr("预览版")
                }[channel]
              ]),
              textAlign: TextAlign.center,
              style: const TextStyle(color: muted, height: 1.8)));
    }
    return SurfaceCard(
        padding: EdgeInsets.zero,
        child: Column(children: [
          Container(
              padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
              decoration: const BoxDecoration(
                  color: Color(0xFFEEF1F5),
                  borderRadius: BorderRadius.vertical(top: Radius.circular(9))),
              child: Row(children: [
                Expanded(
                    child: Text(tr("版本 / 适用环境"),
                        style: const TextStyle(
                            fontSize: 12, fontWeight: FontWeight.w600))),
                SizedBox(
                    width: 84,
                    child: Text(tr("安装"),
                        textAlign: TextAlign.center,
                        style: const TextStyle(
                            fontSize: 12, fontWeight: FontWeight.w600)))
              ])),
          Expanded(
              child: ListView.separated(
                  itemCount: versions.length,
                  separatorBuilder: (_, __) => const Divider(),
                  itemBuilder: (_, i) {
                    final f = versions[i];
                    return Padding(
                        padding: const EdgeInsets.symmetric(
                            horizontal: 14, vertical: 13),
                        child: Row(children: [
                          Expanded(
                              child: Column(
                                  crossAxisAlignment: CrossAxisAlignment.start,
                                  children: [
                                Tooltip(
                                    message: f['filename'] as String,
                                    child: Text(
                                        f['version'] as String? ??
                                            f['name'] as String,
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                        style: const TextStyle(
                                            fontSize: 13,
                                            fontWeight: FontWeight.w500))),
                                const SizedBox(height: 5),
                                Text(
                                    '${(f['game_versions'] as List).take(3).join('、')} · ${(f['loaders'] as List).map((s) => tagName(s.toString())).join(' / ')} · ${byteLabel(f['size'] as num? ?? 0)}',
                                    maxLines: 1,
                                    overflow: TextOverflow.ellipsis,
                                    style: const TextStyle(
                                        color: muted, fontSize: 11))
                              ])),
                          const SizedBox(width: 12),
                          SizedBox(
                              width: 84,
                              child: OutlinedButton.icon(
                                  key: Key('install-$i'),
                                  onPressed:
                                      engine.active ? null : () => install(f),
                                  icon: const Icon(Icons.download_rounded,
                                      size: 16),
                                  label: Text(tr("安装"),
                                      style: const TextStyle(fontSize: 12)),
                                  style: OutlinedButton.styleFrom(
                                      padding: const EdgeInsets.symmetric(
                                          horizontal: 8, vertical: 10))))
                        ]));
                  }))
        ]));
  }

  Widget footer() => FooterBar(
      child: AnimatedBuilder(
          animation: engine,
          builder: (_, __) => engine.active
              ? Row(children: [
                  Expanded(
                      child: Column(
                          mainAxisSize: MainAxisSize.min,
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                        Text(nativeText(engine.label),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(fontSize: 12)),
                        const SizedBox(height: 9),
                        LinearProgressIndicator(
                            value: (engine.progress['total'] as num? ?? 0) > 0
                                ? ((engine.progress['done'] as num? ?? 0) /
                                        (engine.progress['total'] as num))
                                    .clamp(0.0, 1.0)
                                : null,
                            minHeight: 3),
                        if ((engine.progress['total'] as num? ?? 0) > 0)
                          Padding(
                              padding: const EdgeInsets.only(top: 4),
                              child: Text(
                                  '${byteLabel(engine.progress['done'] as num? ?? 0)} / ${byteLabel(engine.progress['total'] as num)}',
                                  style: const TextStyle(
                                      fontSize: 10, color: muted)))
                      ])),
                  const SizedBox(width: 18),
                  TextButton(onPressed: engine.cancel, child: Text(tr("取消")))
                ])
              : Row(children: [
                  Icon(
                      notice.isEmpty
                          ? Icons.public
                          : Icons.check_circle_outline,
                      size: 17,
                      color: notice.isEmpty ? muted : const Color(0xFF367856)),
                  const SizedBox(width: 9),
                  Expanded(
                      child: Text(
                          notice.isEmpty
                              ? project == null
                                  ? browse.error == null
                                      ? tr("共 {0} 个项目 · Modrinth",
                                          [browse.total])
                                      : 'Modrinth'
                                  : tr("来源：Modrinth · 下载时选择目标实例")
                              : notice,
                          maxLines: 2,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                              fontSize: 12,
                              color: notice.isEmpty
                                  ? muted
                                  : const Color(0xFF367856)))),
                  if (project == null && browse.total > 20) ...[
                    const SizedBox(width: 16),
                    Container(
                        key: const Key('catalogPagination'),
                        decoration: BoxDecoration(
                            color: Colors.white,
                            border: Border.all(color: line),
                            borderRadius: BorderRadius.circular(8)),
                        padding: const EdgeInsets.symmetric(
                            horizontal: 4, vertical: 2),
                        child: Row(mainAxisSize: MainAxisSize.min, children: [
                          IconButton(
                              tooltip: tr("上一页"),
                              constraints: const BoxConstraints.tightFor(
                                  width: 34, height: 34),
                              padding: EdgeInsets.zero,
                              onPressed: browse.loading || browse.offset == 0
                                  ? null
                                  : () {
                                      browse.offset -= 20;
                                      unawaited(search());
                                    },
                              icon: const Icon(Icons.chevron_left_rounded,
                                  size: 20)),
                          Padding(
                              padding:
                                  const EdgeInsets.symmetric(horizontal: 10),
                              child: Text(
                                  '${browse.offset ~/ 20 + 1} / ${(browse.total / 20).ceil()}',
                                  style: const TextStyle(
                                      fontSize: 12, color: ink))),
                          IconButton(
                              tooltip: tr("下一页"),
                              constraints: const BoxConstraints.tightFor(
                                  width: 34, height: 34),
                              padding: EdgeInsets.zero,
                              onPressed: browse.loading ||
                                      browse.offset + 20 >= browse.total
                                  ? null
                                  : () {
                                      browse.offset += 20;
                                      unawaited(search());
                                    },
                              icon: const Icon(Icons.chevron_right_rounded,
                                  size: 20)),
                        ])),
                  ]
                ])));
  @override
  Widget build(BuildContext context) => Column(children: [
        header(),
        Expanded(
            child: Padding(
                padding: const EdgeInsets.fromLTRB(28, 0, 28, 18),
                child: project == null ? catalog() : detail())),
        footer()
      ]);
}

class Choice {
  const Choice(this.value, this.label, {this.group = 'release'});
  final String value, label, group;
}

Future<String?> showChoicePicker(BuildContext context,
        {required String title,
        required String value,
        required Future<List<Choice>> Function() load,
        Map<String, String>? groups,
        String? clearLabel}) =>
    showDialog<String>(
        context: context,
        builder: (_) => ChoicePicker(
            title: title,
            value: value,
            load: load,
            groups: groups,
            clearLabel: clearLabel));

class ChoicePicker extends StatefulWidget {
  const ChoicePicker(
      {super.key,
      required this.title,
      required this.value,
      required this.load,
      this.groups,
      this.clearLabel});
  final String title, value;
  final Future<List<Choice>> Function() load;
  final Map<String, String>? groups;
  final String? clearLabel;
  @override
  State<ChoicePicker> createState() => _ChoicePickerState();
}

class _ChoicePickerState extends State<ChoicePicker> {
  List<Choice> choices = [];
  String search = '', group = 'release';
  bool loading = true;
  Data? error;
  @override
  void initState() {
    super.initState();
    unawaited(load());
  }

  Future<void> load() async {
    setState(() {
      loading = true;
      error = null;
    });
    try {
      final result = await widget.load();
      if (mounted) setState(() => choices = result);
    } catch (e) {
      if (mounted) setState(() => error = problem(e));
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final matches = choices
        .where((c) =>
            (widget.groups == null || c.group == group) &&
            c.label.toLowerCase().contains(search.toLowerCase()))
        .toList();
    return AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        title: Row(children: [
          Expanded(
              child: Text(widget.title,
                  style: const TextStyle(
                      fontSize: 19, fontWeight: FontWeight.w600))),
          if (widget.clearLabel != null)
            TextButton(
                onPressed: () => Navigator.pop(context, ''),
                child: Text(widget.clearLabel!,
                    style: const TextStyle(fontSize: 12)))
        ]),
        content: SizedBox(
            width: 460,
            height: 350,
            child: Column(children: [
              TextField(
                  autofocus: true,
                  onChanged: (v) => setState(() => search = v),
                  decoration: InputDecoration(
                      hintText: tr("搜索选项…"),
                      prefixIcon: const Icon(Icons.search, size: 18))),
              const SizedBox(height: 10),
              if (widget.groups != null)
                Wrap(spacing: 6, children: [
                  for (final g in widget.groups!.entries)
                    ChoiceChip(
                        label:
                            Text(g.value, style: const TextStyle(fontSize: 12)),
                        selected: group == g.key,
                        onSelected: (_) => setState(() => group = g.key),
                        visualDensity: VisualDensity.compact)
                ]),
              const SizedBox(height: 8),
              Expanded(
                  child: loading
                      ? const BusyPanel()
                      : error != null
                          ? ProblemPanel(error!, retry: load)
                          : matches.isEmpty
                              ? Center(
                                  child: Text(tr("没有匹配的选项"),
                                      style: const TextStyle(color: muted)))
                              : ListView.builder(
                                  itemCount: matches.length,
                                  itemBuilder: (_, i) {
                                    final c = matches[i];
                                    return Padding(
                                        padding: const EdgeInsets.symmetric(
                                            vertical: 2),
                                        child: Material(
                                            color: c.value == widget.value
                                                ? const Color(0xFFEBF1F7)
                                                : Colors.transparent,
                                            borderRadius:
                                                BorderRadius.circular(7),
                                            child: ListTile(
                                                dense: true,
                                                title: Text(c.label,
                                                    style: const TextStyle(
                                                        fontSize: 13)),
                                                trailing:
                                                    c.value == widget.value
                                                        ? const Icon(
                                                            Icons.check_rounded,
                                                            size: 18,
                                                            color: accent)
                                                        : null,
                                                onTap: () => Navigator.pop(
                                                    context, c.value))));
                                  }))
            ])),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context), child: Text(tr("取消")))
        ]);
  }
}

class TargetPicker extends StatefulWidget {
  const TargetPicker(
      {super.key,
      required this.engine,
      required this.roots,
      required this.file,
      required this.kind,
      required this.selectedVersion});
  final ResourceEngine engine;
  final Set<String> roots;
  final Data file;
  final String kind, selectedVersion;
  @override
  State<TargetPicker> createState() => _TargetPickerState();
}

class _TargetPickerState extends State<TargetPicker> {
  List<Data> instances = [];
  Data? selected, error;
  bool loading = true;
  @override
  void initState() {
    super.initState();
    unawaited(load());
  }

  Future<void> load() async {
    setState(() {
      loading = true;
      error = null;
      selected = null;
    });
    try {
      final result = Map<String, dynamic>.from(await widget.engine.run(
          {'op': 'instances.scan', 'roots': widget.roots.toList()},
          title: tr("正在识别本地实例")) as Map);
      if (mounted) {
        setState(() {
          instances = maps(result['instances']);
          selected = instances
              .where((i) =>
                  i['id'] == widget.selectedVersion &&
                  compatible(widget.file, i, widget.kind))
              .firstOrNull;
        });
      }
    } catch (e) {
      if (mounted) setState(() => error = problem(e));
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  Future<void> addRoot() async {
    final path = await getDirectoryPath(confirmButtonText: tr("选择游戏根目录"));
    if (path != null && mounted) {
      widget.roots.add(path);
      await load();
    }
  }

  Widget target(Data i) {
    final can =
        compatible(widget.file, i, widget.kind) && i['loader'] != 'conflict';
    return Material(
        color: selected?['path'] == i['path']
            ? const Color(0xFFEBF1F7)
            : const Color(0xFFF6F8FA),
        borderRadius: BorderRadius.circular(8),
        child: ListTile(
            enabled: can,
            dense: true,
            leading: Radio<String>(
                value: i['path'] as String,
                groupValue: selected?['path'] as String?,
                onChanged: can ? (_) => setState(() => selected = i) : null),
            title: Text(i['id'] as String,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style:
                    const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
            subtitle:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Text(
                  '${i['version']} · ${tagName(i['loader'] as String)}${can ? '' : tr(" · 与此文件不兼容")}',
                  style: const TextStyle(fontSize: 11)),
              Tooltip(
                  message: i['game_directory'] as String,
                  child: Text(i['game_directory'] as String,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 10, color: muted)))
            ]),
            onTap: can ? () => setState(() => selected = i) : null));
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
          title: Text(tr("安装到哪个实例？"),
              style:
                  const TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
          content: SizedBox(
              width: 550,
              height: 340,
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                        widget.file['title'] as String? ??
                            widget.file['name'] as String? ??
                            '',
                        style: const TextStyle(fontWeight: FontWeight.w500)),
                    const SizedBox(height: 5),
                    Text(
                        '${widget.file['version']} · ${(widget.file['loaders'] as List? ?? []).map((s) => tagName(s.toString())).join(' / ')}',
                        style: const TextStyle(fontSize: 12, color: muted)),
                    const SizedBox(height: 15),
                    Expanded(
                        child: loading
                            ? const BusyPanel()
                            : error != null
                                ? ProblemPanel(error!, retry: load)
                                : instances.isEmpty
                                    ? Center(
                                        child: Column(
                                            mainAxisSize: MainAxisSize.min,
                                            children: [
                                            const Icon(
                                                Icons.folder_open_outlined,
                                                size: 32,
                                                color: muted),
                                            const SizedBox(height: 12),
                                            Text(tr("尚未找到本地实例"),
                                                style: const TextStyle(
                                                    fontWeight:
                                                        FontWeight.w500)),
                                            const SizedBox(height: 6),
                                            Text(tr("选择包含 versions 文件夹的游戏根目录"),
                                                style: const TextStyle(
                                                    fontSize: 12,
                                                    color: muted)),
                                            const SizedBox(height: 15),
                                            OutlinedButton.icon(
                                                onPressed: addRoot,
                                                icon: const Icon(
                                                    Icons.folder_open,
                                                    size: 17),
                                                label: Text(tr("选择游戏目录")))
                                          ]))
                                    : ListView.separated(
                                        itemCount: instances.length,
                                        separatorBuilder: (_, __) =>
                                            const SizedBox(height: 7),
                                        itemBuilder: (_, i) =>
                                            target(instances[i])))
                  ])),
          actions: [
            TextButton(
                onPressed: loading ? null : addRoot, child: Text(tr("添加游戏目录"))),
            TextButton(
                onPressed: () => Navigator.pop(context), child: Text(tr("取消"))),
            FilledButton(
                key: const Key('confirmTarget'),
                onPressed: selected == null
                    ? null
                    : () => Navigator.pop(context, selected),
                child: Text(tr("继续")))
          ]);
}

class InstallPlanDialog extends StatefulWidget {
  const InstallPlanDialog({super.key, required this.plan, required this.kind});
  final Data plan;
  final String kind;
  @override
  State<InstallPlanDialog> createState() => _InstallPlanDialogState();
}

class _InstallPlanDialogState extends State<InstallPlanDialog> {
  final selected = <String>{},
      redownload = <String>{},
      enable = <String>{},
      disabled = <String>{};
  bool replace = false, acknowledge = false, shaderAcknowledged = false;
  bool get mods => widget.kind == 'mod';
  List<Data> get files => mods
      ? maps(widget.plan['files'])
      : [Map<String, dynamic>.from(widget.plan['file'] as Map)];
  List<Data> get reuse =>
      maps(widget.plan['reuse'] is List ? widget.plan['reuse'] : []);
  List<Data> get conflicts => maps(widget.plan['conflicts']);
  @override
  void initState() {
    super.initState();
    for (final f in [
      ...files,
      ...reuse.map((r) => Map<String, dynamic>.from(r['file'] as Map))
    ]) {
      if (f['optional'] != true) selected.add(f['project_id'] as String);
    }
  }

  void toggle(String id, bool value) {
    setState(() {
      if (value) {
        selected.add(id);
        bool changed = true;
        while (changed) {
          changed = false;
          for (final e in maps(widget.plan['edges'])) {
            if (selected.contains(e['from']) && e['optional'] != true) {
              changed = selected.add(e['to'] as String) || changed;
            }
          }
        }
      } else {
        selected.remove(id);
        bool changed = true;
        while (changed) {
          changed = false;
          for (final e in maps(widget.plan['edges'])) {
            final child = e['to'] as String;
            if (selected.contains(child) &&
                !maps(widget.plan['edges']).any(
                    (p) => p['to'] == child && selected.contains(p['from']))) {
              selected.remove(child);
              changed = true;
            }
          }
        }
      }
      acknowledge = false;
    });
  }

  int get omitted => [
        ...files,
        ...reuse.map((r) => Map<String, dynamic>.from(r['file'] as Map))
      ]
          .where((f) =>
              f['root'] != true &&
              f['optional'] != true &&
              !selected.contains(f['project_id']))
          .length;
  bool get shaderMissing =>
      !mods &&
      widget.kind == 'shader' &&
      (widget.plan['support'] as Map?)?['available'] != true;
  bool get duplicate =>
      !mods &&
      (widget.plan['expected_hash'] as String? ?? '').isNotEmpty &&
      widget.plan['reuse'] != true;
  Future<void> askRedownload(Data r) async {
    final yes = await showDialog<bool>(
        context: context,
        builder: (_) => AlertDialog(
                title:
                    Text(tr("重新下载已有组件？"), style: const TextStyle(fontSize: 18)),
                content: Text(tr("将校验并替换已有副本，不会额外保留一个重复 JAR。")),
                actions: [
                  TextButton(
                      onPressed: () => Navigator.pop(context, false),
                      child: Text(tr("取消"))),
                  FilledButton(
                      onPressed: () => Navigator.pop(context, true),
                      child: Text(tr("重新下载")))
                ]));
    if (yes == true && mounted) {
      setState(() => redownload.add(r['project_id'] as String));
    }
  }

  Widget component(Data f, {Data? existing}) {
    final id = f['project_id'] as String? ?? '',
        root = f['root'] == true,
        isReuse = existing != null;
    return Padding(
        padding: const EdgeInsets.symmetric(vertical: 9),
        child: Row(children: [
          if (isReuse)
            const SizedBox(
                width: 34,
                child: Icon(Icons.check_circle_outline,
                    color: Color(0xFF4E7A95), size: 21))
          else
            SizedBox(
                width: 34,
                child: Checkbox(
                    value: !mods || selected.contains(id),
                    onChanged:
                        !mods || root ? null : (v) => toggle(id, v ?? false),
                    shape: RoundedRectangleBorder(
                        borderRadius: BorderRadius.circular(4)),
                    visualDensity: VisualDensity.compact)),
          const SizedBox(width: 8),
          ResourceImage(f['icon_url'] as String?, size: 35),
          const SizedBox(width: 11),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(f['title'] as String? ?? f['name'] as String? ?? tr("组件"),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: const TextStyle(
                        fontSize: 13, fontWeight: FontWeight.w500)),
                const SizedBox(height: 5),
                Text(
                    '${f['version'] ?? ''} · ${isReuse ? existing['embedded'] == true ? tr("已安装（内嵌）") : existing['enabled'] == true ? tr("已安装") : tr("已停用") : root ? tr("主模组") : f['optional'] == true ? tr("可选依赖") : mods ? tr("必需依赖") : widget.kind == 'shader' ? tr("光影包") : tr("材质包")}',
                    style: const TextStyle(fontSize: 11, color: muted)),
                if (isReuse && existing['enabled'] != true)
                  Row(children: [
                    Checkbox(
                        value: enable.contains(id),
                        onChanged: (v) => setState(() {
                              if (v == true) {
                                enable.add(id);
                              } else {
                                enable.remove(id);
                              }
                            }),
                        visualDensity: VisualDensity.compact),
                    Text(tr("同时启用"), style: const TextStyle(fontSize: 11))
                  ])
              ])),
          if (isReuse && existing['embedded'] != true)
            TextButton(
                onPressed: () => redownload.contains(id)
                    ? setState(() => redownload.remove(id))
                    : askRedownload(existing),
                child: Text(
                    redownload.contains(id) ? tr("取消重新下载") : tr("重新下载…"),
                    style: const TextStyle(fontSize: 11))),
          if (!isReuse)
            Text(byteLabel(f['size'] as num? ?? 0),
                style: const TextStyle(fontSize: 11, color: muted))
        ]));
  }

  Widget section(String title, Color color, List<Widget> children) => Padding(
      padding: const EdgeInsets.only(bottom: 10),
      child: SurfaceCard(
          color: color,
          padding: const EdgeInsets.fromLTRB(14, 12, 14, 8),
          child:
              Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(title,
                style:
                    const TextStyle(fontSize: 12, fontWeight: FontWeight.w600)),
            const SizedBox(height: 4),
            ...children
          ])));
  @override
  Widget build(BuildContext context) {
    final target = Map<String, dynamic>.from(widget.plan['target'] as Map);
    final unresolved = conflicts.any((c) => !disabled.contains(c['filename']));
    final inactive = reuse.any((r) =>
        selected.contains(r['project_id']) &&
        r['enabled'] != true &&
        !enable.contains(r['project_id']) &&
        !redownload.contains(r['project_id']));
    final can = !unresolved &&
        (!mods || omitted == 0 || acknowledge) &&
        (!mods || !inactive) &&
        (!duplicate || replace) &&
        (!shaderMissing || shaderAcknowledged);
    return AlertDialog(
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
        title: Text(tr("确认安装方案"),
            style: const TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
        content: SizedBox(
            width: 600,
            height: 390,
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              Row(children: [
                const Icon(Icons.sports_esports_outlined,
                    size: 23, color: accent),
                const SizedBox(width: 11),
                Expanded(
                    child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                      Text(target['id'] as String? ?? tr("目标实例"),
                          style: const TextStyle(
                              fontSize: 15, fontWeight: FontWeight.w600)),
                      const SizedBox(height: 5),
                      Text(
                          '${target['version']} · ${tagName(target['loader'] as String? ?? 'vanilla')}',
                          style: const TextStyle(fontSize: 12, color: muted))
                    ]))
              ]),
              const SizedBox(height: 14),
              Expanded(
                  child: SingleChildScrollView(
                      child: Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                    if (files.isNotEmpty)
                      section(
                          !mods && widget.plan['reuse'] == true
                              ? tr("已有资源（无需重复下载）")
                              : tr("将添加 · {0} 项", [files.length]),
                          const Color(0xFFF1F6F9),
                          [for (final f in files) component(f)]),
                    if (reuse.isNotEmpty)
                      section(tr("已有组件 · {0} 项", [reuse.length]),
                          const Color(0xFFEDF4F9), [
                        for (final r in reuse)
                          component(Map<String, dynamic>.from(r['file'] as Map),
                              existing: r)
                      ]),
                    if (conflicts.isNotEmpty)
                      section(tr("需要处理 · 不兼容组件"), const Color(0xFFFFF7E7), [
                        for (final c in conflicts)
                          CheckboxListTile(
                              dense: true,
                              contentPadding: EdgeInsets.zero,
                              controlAffinity: ListTileControlAffinity.leading,
                              value: disabled.contains(c['filename']),
                              onChanged: (v) => setState(() {
                                    if (v == true) {
                                      disabled.add(c['filename'] as String);
                                    } else {
                                      disabled.remove(c['filename']);
                                    }
                                  }),
                              title: Text(tr("停用 {0}", [c['filename']]),
                                  style: const TextStyle(fontSize: 12)),
                              subtitle: Text(
                                  nativeText(
                                      c['reason'] as String? ?? '作者声明不兼容'),
                                  style: const TextStyle(fontSize: 11)))
                      ]),
                    if (omitted > 0)
                      CheckboxListTile(
                          dense: true,
                          contentPadding: EdgeInsets.zero,
                          value: acknowledge,
                          onChanged: (v) =>
                              setState(() => acknowledge = v ?? false),
                          controlAffinity: ListTileControlAffinity.leading,
                          title: Text(
                              tr("已取消 {0} 项必需依赖，我了解游戏可能无法启动", [omitted]),
                              style: const TextStyle(
                                  fontSize: 12, color: Color(0xFFAD6342)))),
                    if (inactive)
                      Padding(
                          padding: const EdgeInsets.all(8),
                          child: Text(tr("所需组件已停用，请明确勾选“同时启用”后安装。"),
                              style: const TextStyle(
                                  fontSize: 12, color: Color(0xFFAD6342)))),
                    if (duplicate)
                      CheckboxListTile(
                          value: replace,
                          onChanged: (v) =>
                              setState(() => replace = v ?? false),
                          controlAffinity: ListTileControlAffinity.leading,
                          title: Text(tr("替换已有的同名资源文件"),
                              style: const TextStyle(fontSize: 12))),
                    if (shaderMissing)
                      section(tr("光影支持尚未就绪"), const Color(0xFFFFF7E7), [
                        Padding(
                            padding: const EdgeInsets.symmetric(vertical: 7),
                            child: Text(
                                tr(
                                    "此步骤只安装光影包。仍需在该实例中安装兼容的 Iris 或 OptiFine，并在游戏内选择启用。"),
                                style: const TextStyle(
                                    fontSize: 12, height: 1.6))),
                        CheckboxListTile(
                            contentPadding: EdgeInsets.zero,
                            value: shaderAcknowledged,
                            onChanged: (v) =>
                                setState(() => shaderAcknowledged = v ?? false),
                            controlAffinity: ListTileControlAffinity.leading,
                            title: Text(tr("先安装光影包，稍后配置支持组件"),
                                style: const TextStyle(fontSize: 12)))
                      ]),
                    if (!mods && widget.kind == 'shader' && !shaderMissing)
                      Padding(
                          padding: const EdgeInsets.all(8),
                          child: Text(tr("已检测到光影支持组件。在游戏内选择光影并调整画质。"),
                              style:
                                  const TextStyle(fontSize: 12, color: muted)))
                  ])))
            ])),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context), child: Text(tr("取消"))),
          FilledButton(
              key: const Key('applyInstall'),
              onPressed: can
                  ? () => Navigator.pop(
                      context,
                      mods
                          ? {
                              'selected': selected.toList(),
                              'redownload': redownload.toList(),
                              'enable': enable.toList(),
                              'disable': disabled.toList()
                            }
                          : {'replace': replace})
                  : null,
              child: Text(!mods && widget.plan['reuse'] == true
                  ? tr("完成")
                  : tr("确认并安装")))
        ]);
  }
}
