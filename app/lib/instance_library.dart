import 'appearance.dart';
import 'cover_art.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:intl/intl.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'management_widgets.dart';
import 'localization.dart';
import 'resource_engine.dart';

int instanceHistoryNumber(Data instance, String key) {
  final value = instance[key];
  if (value is! num || !value.isFinite || value < 0) return 0;
  if (key == 'last_launch' && value > 253402300799) return 0;
  return value.toInt();
}

int _compareGameVersions(String a, String b) {
  final tokens = RegExp(r'\d+|[^\d]+');
  final left = tokens.allMatches(a.toLowerCase()).map((m) => m[0]!).toList();
  final right = tokens.allMatches(b.toLowerCase()).map((m) => m[0]!).toList();
  for (var index = 0; index < left.length && index < right.length; index++) {
    final n = int.tryParse(left[index]), m = int.tryParse(right[index]);
    final order = n != null && m != null
        ? n.compareTo(m)
        : left[index].compareTo(right[index]);
    if (order != 0) return order;
  }
  // A final release precedes its own preview suffix in descending version order.
  if (left.length != right.length) {
    final shorter = left.length < right.length ? a : b;
    final longer = left.length < right.length ? b : a;
    if (longer.startsWith('$shorter-')) {
      return left.length < right.length ? 1 : -1;
    }
  }
  return left.length.compareTo(right.length);
}

/// A deterministic copy: the scan's order and data stay untouched.
List<Data> orderedLibraryInstances(Iterable<Data> instances, String order) =>
    instances.toList()
      ..sort((a, b) {
        final favorite = (b['favorite'] == true ? 1 : 0)
            .compareTo(a['favorite'] == true ? 1 : 0);
        if (favorite != 0) return favorite;
        int selected = 0;
        if (order == 'recent') {
          selected = instanceHistoryNumber(b, 'last_launch')
              .compareTo(instanceHistoryNumber(a, 'last_launch'));
        } else if (order == 'version') {
          selected = _compareGameVersions(
              '${b['version'] ?? ''}', '${a['version'] ?? ''}');
        }
        if (selected != 0) return selected;
        final name = '${a['name'] ?? a['id']}'
            .toLowerCase()
            .compareTo('${b['name'] ?? b['id']}'.toLowerCase());
        return name != 0 ? name : '${a['id']}'.compareTo('${b['id']}');
      });

String libraryRuntimeLabel(int seconds) {
  if (seconds < 60) return '<1m';
  if (seconds < 3600) return '${seconds ~/ 60}m';
  return '${seconds ~/ 3600}h ${(seconds % 3600) ~/ 60}m';
}

String libraryHistorySummary(Data instance) {
  if (instance['history_warning'] is Map) return tr('统计暂不可用');
  final launched = instanceHistoryNumber(instance, 'last_launch');
  final count = instanceHistoryNumber(instance, 'launch_count');
  if (launched == 0 && count == 0) return tr('尚无启动记录');
  final date = launched > 0
      ? DateFormat('MM-dd HH:mm').format(
          DateTime.fromMillisecondsSinceEpoch(launched * 1000).toLocal())
      : '';
  return [
    if (date.isNotEmpty) date,
    tr('{0} 次启动', [count]),
    libraryRuntimeLabel(instanceHistoryNumber(instance, 'runtime_seconds'))
  ].join(' · ');
}

String libraryHistoryDetails(Data instance) {
  if (instance['history_warning'] is Map) {
    return nativeText(
        (instance['history_warning'] as Map)['message']?.toString() ??
            tr('统计暂不可用'));
  }
  final launched = instanceHistoryNumber(instance, 'last_launch');
  return [
    if (launched > 0)
      tr('最近启动：{0}', [
        DateFormat('yyyy-MM-dd HH:mm').format(
            DateTime.fromMillisecondsSinceEpoch(launched * 1000).toLocal())
      ]),
    tr('已记录 {0} 次启动', [instanceHistoryNumber(instance, 'launch_count')]),
    tr('已观察运行时间：{0}', [
      libraryRuntimeLabel(instanceHistoryNumber(instance, 'runtime_seconds'))
    ]),
    tr('运行时间包括挂起和闲置，仅记录启动器观察到的进程运行时长。'),
  ].join('\n');
}

class InstanceCover extends StatelessWidget {
  const InstanceCover(this.instance, {super.key, this.height = 110});
  final Data instance;
  final double height;
  @override
  Widget build(BuildContext context) {
    final cover = instance['cover'] as String? ?? '';
    Widget fallback() => Image.asset(
        versionCoverAsset(
            instance['version'] as String? ?? instance['id'] as String? ?? ''),
        height: height,
        width: double.infinity,
        fit: BoxFit.cover);
    return ClipRRect(
        borderRadius: BorderRadius.circular(12),
        child: cover.isNotEmpty
            ? Image.file(File(cover),
                height: height,
                width: double.infinity,
                fit: BoxFit.cover,
                errorBuilder: (_, __, ___) => fallback())
            : fallback());
  }
}

class InstanceLibrary extends StatefulWidget {
  const InstanceLibrary(
      {super.key,
      required this.engine,
      required this.root,
      required this.onOpen,
      required this.onInstall,
      this.onImport,
      required this.onLaunch});
  final ResourceEngine engine;
  final String root;
  final void Function(Data, String) onOpen;
  final VoidCallback onInstall;
  final VoidCallback? onImport;
  final ValueChanged<Data> onLaunch;
  @override
  State<InstanceLibrary> createState() => _InstanceLibraryState();
}

class _InstanceLibraryState extends State<InstanceLibrary>
    with ManagedState<InstanceLibrary> {
  int _replayRevision = 0;
  void _downloadReplayed() {
    if (_replayRevision == engine.replayRevision) return;
    _replayRevision = engine.replayRevision;
    if (mounted && !loading) load();
  }

  @override
  void dispose() {
    engine.removeListener(_downloadReplayed);
    super.dispose();
  }

  @override
  ResourceEngine get engine => widget.engine;
  List<Data> instances = [];
  String query = '', order = 'recent';
  bool favorites = false;
  @override
  void initState() {
    super.initState();
    _replayRevision = engine.replayRevision;
    engine.addListener(_downloadReplayed);
    load();
  }

  @override
  void didUpdateWidget(InstanceLibrary old) {
    super.didUpdateWidget(old);
    if (old.root != widget.root) load();
  }

  Future<void> load() => perform(() async {
        final response = await rpc({
          'op': 'instances.scan',
          'roots': [widget.root]
        });
        if (mounted) {
          setState(() => instances = dataRows(response['instances']));
        }
      });
  Future<void> favorite(Data instance) => perform(() async {
        final settings = Map<String, dynamic>.from(
            await rpc({'op': 'settings.load', 'directory': instance['path']})
                as Map);
        settings['display'] = {
          ...Map<String, dynamic>.from(settings['display'] as Map? ?? {}),
          'favorite': instance['favorite'] != true
        };
        await rpc({
          'op': 'settings.save',
          'directory': instance['path'],
          'settings': settings
        });
        if (mounted) {
          setState(() => instance['favorite'] = instance['favorite'] != true);
        }
      });
  @override
  Widget build(BuildContext context) {
    final visible = orderedLibraryInstances(
        instances.where((i) =>
            (!favorites || i['favorite'] == true) &&
            '${i['name']} ${i['id']} ${i['version']} ${i['loader']}'
                .toLowerCase()
                .contains(query.toLowerCase())),
        order);
    return ManagementPage(
        title: tr('游戏库'),
        actions: [
          if (widget.onImport != null) ...[
            OutlinedButton.icon(
                key: const Key('importModpackLibrary'),
                onPressed: locked ? null : widget.onImport,
                icon: const Icon(Icons.inventory_2_outlined, size: 18),
                label: Text(tr('导入整合包'))),
            const SizedBox(width: 10)
          ],
          FilledButton.icon(
              onPressed: locked ? null : widget.onInstall,
              icon: const Icon(Icons.add_rounded, size: 18),
              label: Text(tr('安装游戏')))
        ],
        body: Column(children: [
          Row(children: [
            Expanded(
                child: TextField(
                    decoration: InputDecoration(
                        hintText: tr('搜索实例名称、版本或加载器'),
                        prefixIcon: const Icon(Icons.search_rounded)),
                    onChanged: (v) => setState(() => query = v))),
            const SizedBox(width: 12),
            FilterChip(
                label: Text(tr('收藏')),
                selected: favorites,
                onSelected: (v) => setState(() => favorites = v)),
            const SizedBox(width: 8),
            PopupMenuButton<String>(
                key: const Key('librarySort'),
                tooltip: tr('排序'),
                initialValue: order,
                onSelected: (value) => setState(() => order = value),
                itemBuilder: (_) => [
                      for (final entry in {
                        'recent': tr('最近启动'),
                        'name': tr('名称'),
                        'version': tr('游戏版本')
                      }.entries)
                        PopupMenuItem(
                            value: entry.key, child: Text(entry.value))
                    ],
                child: Padding(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 8, vertical: 10),
                    child: Row(mainAxisSize: MainAxisSize.min, children: [
                      Icon(Icons.sort_rounded, size: 18, color: muted),
                      const SizedBox(width: 6),
                      Text(
                          {
                            'recent': tr('最近启动'),
                            'name': tr('名称'),
                            'version': tr('游戏版本')
                          }[order]!,
                          style: TextStyle(fontSize: 12, color: ink)),
                      const SizedBox(width: 4),
                      Icon(Icons.keyboard_arrow_down_rounded,
                          size: 16, color: muted)
                    ]))),
            const SizedBox(width: 8),
            IconButton(
                onPressed: locked ? null : load,
                tooltip: tr('刷新'),
                icon: const Icon(Icons.refresh_rounded))
          ]),
          const SizedBox(height: 18),
          Expanded(
              child: result(visible.isEmpty
                  ? Center(child: Text(tr('尚未找到游戏实例，安装游戏或添加已有目录。')))
                  : LayoutBuilder(builder: (context, bounds) {
                      final columns = bounds.maxWidth < 720
                          ? 2
                          : bounds.maxWidth < 1100
                              ? 3
                              : 4;
                      return GridView.builder(
                          gridDelegate:
                              SliverGridDelegateWithFixedCrossAxisCount(
                                  crossAxisCount: columns,
                                  crossAxisSpacing: 14,
                                  mainAxisSpacing: 24,
                                  mainAxisExtent: 252),
                          itemCount: visible.length,
                          itemBuilder: (context, index) {
                            final i = visible[index];
                            final loader = {
                                  'vanilla': tr('原版'),
                                  'fabric': 'Fabric',
                                  'forge': 'Forge',
                                  'neoforge': 'NeoForge',
                                  'quilt': 'Quilt'
                                }[i['loader']] ??
                                i['loader']?.toString() ??
                                '';
                            final metadata = [
                              i['version']?.toString() ?? '',
                              '$loader ${i['loader_version'] ?? ''}'.trim()
                            ].where((v) => v.isNotEmpty).join(' · ');
                            return Material(
                                color: Colors.transparent,
                                child: Column(
                                    crossAxisAlignment:
                                        CrossAxisAlignment.start,
                                    children: [
                                      Expanded(
                                          child: Tooltip(
                                              message: tr('管理实例'),
                                              child: InkWell(
                                                  key: ValueKey(
                                                      'library-cover-${i['id']}'),
                                                  onTap: () =>
                                                      widget.onOpen(i, 'mod'),
                                                  borderRadius:
                                                      BorderRadius.circular(12),
                                                  child: InstanceCover(i,
                                                      height: 160)))),
                                      const SizedBox(height: 8),
                                      Row(children: [
                                        Expanded(
                                            child: Text(i['name'] ?? i['id'],
                                                maxLines: 1,
                                                overflow: TextOverflow.ellipsis,
                                                style: TextStyle(
                                                    fontSize: 15,
                                                    height: 1.4,
                                                    color: ink,
                                                    fontWeight:
                                                        FontWeight.w600))),
                                        IconButton(
                                            tooltip: tr('收藏'),
                                            padding: EdgeInsets.zero,
                                            constraints:
                                                const BoxConstraints.tightFor(
                                                    width: 28, height: 28),
                                            onPressed: locked
                                                ? null
                                                : () => favorite(i),
                                            icon: Icon(
                                                i['favorite'] == true
                                                    ? Icons.star_rounded
                                                    : Icons
                                                        .star_outline_rounded,
                                                size: 18,
                                                color: i['favorite'] == true
                                                    ? seclColor(0xFFD89C31)
                                                    : muted))
                                      ]),
                                      Row(children: [
                                        Expanded(
                                            child: Tooltip(
                                                message: metadata,
                                                child: Text(metadata,
                                                    maxLines: 1,
                                                    overflow:
                                                        TextOverflow.ellipsis,
                                                    style: TextStyle(
                                                        fontSize: 12,
                                                        height: 1.5,
                                                        color: muted)))),
                                        const SizedBox(width: 4),
                                        SizedBox(
                                            width: 30,
                                            height: 32,
                                            child: PopupMenuButton<String>(
                                                key: ValueKey(
                                                    'library-menu-${i['id']}'),
                                                enabled: !locked,
                                                tooltip: tr('更多操作'),
                                                padding: EdgeInsets.zero,
                                                shape: RoundedRectangleBorder(
                                                    borderRadius:
                                                        BorderRadius.circular(
                                                            10)),
                                                onSelected: (tab) =>
                                                    widget.onOpen(i, tab),
                                                itemBuilder: (_) => [
                                                      PopupMenuItem(
                                                          value: 'mod',
                                                          child:
                                                              Text(tr('管理实例'))),
                                                      PopupMenuItem(
                                                          value: 'settings',
                                                          child:
                                                              Text(tr('实例设置')))
                                                    ],
                                                icon: Icon(
                                                    Icons.more_horiz_rounded,
                                                    size: 19,
                                                    color: muted))),
                                        const SizedBox(width: 4),
                                        SizedBox(
                                            width: 32,
                                            height: 32,
                                            child: IconButton(
                                                key: ValueKey(
                                                    'library-launch-${i['id']}'),
                                                tooltip: tr('启动游戏'),
                                                onPressed: locked
                                                    ? null
                                                    : () => widget.onLaunch(i),
                                                style: IconButton.styleFrom(
                                                    padding: EdgeInsets.zero,
                                                    backgroundColor:
                                                        accent.withOpacity(.1),
                                                    shape:
                                                        RoundedRectangleBorder(
                                                            borderRadius:
                                                                BorderRadius
                                                                    .circular(
                                                                        8))),
                                                icon: Icon(
                                                    Icons.play_arrow_rounded,
                                                    size: 21,
                                                    color: accent)))
                                      ]),
                                      const SizedBox(height: 5),
                                      Tooltip(
                                          message: libraryHistoryDetails(i),
                                          child: Row(children: [
                                            Icon(
                                                i['history_warning'] is Map
                                                    ? Icons.info_outline_rounded
                                                    : Icons.history_rounded,
                                                size: 12,
                                                color: muted),
                                            const SizedBox(width: 4),
                                            Expanded(
                                                child: Text(
                                                    libraryHistorySummary(i),
                                                    key: ValueKey(
                                                        'library-history-${i['id']}'),
                                                    maxLines: 1,
                                                    overflow:
                                                        TextOverflow.ellipsis,
                                                    style: TextStyle(
                                                        fontSize: 11,
                                                        height: 1.5,
                                                        color: muted)))
                                          ]))
                                    ]));
                          });
                    })))
        ]));
  }
}
