import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'management_widgets.dart';
import 'localization.dart';
import 'resource_engine.dart';

class InstanceCover extends StatelessWidget {
  const InstanceCover(this.instance, {super.key, this.height = 110});
  final Data instance;
  final double height;
  @override
  Widget build(BuildContext context) {
    final cover = instance['cover'] as String? ?? '';
    Widget fallback() => Image.asset('assets/solar-dawn.jpg',
        height: height, width: double.infinity, fit: BoxFit.cover);
    return ClipRRect(
        borderRadius: BorderRadius.circular(8),
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
      required this.onLaunch});
  final ResourceEngine engine;
  final String root;
  final void Function(Data, String) onOpen;
  final VoidCallback onInstall;
  final ValueChanged<Data> onLaunch;
  @override
  State<InstanceLibrary> createState() => _InstanceLibraryState();
}

class _InstanceLibraryState extends State<InstanceLibrary>
    with ManagedState<InstanceLibrary> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> instances = [];
  String query = '';
  bool favorites = false;
  @override
  void initState() {
    super.initState();
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
    final visible = instances
        .where((i) =>
            (!favorites || i['favorite'] == true) &&
            '${i['name']} ${i['id']} ${i['version']} ${i['loader']}'
                .toLowerCase()
                .contains(query.toLowerCase()))
        .toList()
      ..sort((a, b) => (b['favorite'] == true ? 1 : 0)
          .compareTo(a['favorite'] == true ? 1 : 0));
    return ManagementPage(
        title: tr('游戏库'),
        actions: [
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
                                  mainAxisSpacing: 14,
                                  mainAxisExtent: 246),
                          itemCount: visible.length,
                          itemBuilder: (context, index) {
                            final i = visible[index];
                            return SurfaceCard(
                                padding: const EdgeInsets.all(12),
                                child: Column(
                                    crossAxisAlignment:
                                        CrossAxisAlignment.start,
                                    children: [
                                      Expanded(
                                          child: InkWell(
                                              onTap: () =>
                                                  widget.onOpen(i, 'mod'),
                                              child: InstanceCover(i,
                                                  height: 120))),
                                      const SizedBox(height: 10),
                                      Row(children: [
                                        Expanded(
                                            child: Text(i['name'] ?? i['id'],
                                                maxLines: 1,
                                                overflow: TextOverflow.ellipsis,
                                                style: const TextStyle(
                                                    fontSize: 15,
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
                                                size: 19,
                                                color: i['favorite'] == true
                                                    ? seclColor(0xFFD89C31)
                                                    : muted))
                                      ]),
                                      Text(
                                          '${i['version']} · ${i['loader']} ${i['loader_version']}',
                                          maxLines: 1,
                                          overflow: TextOverflow.ellipsis,
                                          style: TextStyle(
                                              fontSize: 12, color: muted)),
                                      const SizedBox(height: 8),
                                      Row(children: [
                                        Expanded(
                                            child: OutlinedButton.icon(
                                                onPressed: locked
                                                    ? null
                                                    : () =>
                                                        widget.onOpen(i, 'mod'),
                                                icon: const Icon(
                                                    Icons.tune_rounded,
                                                    size: 16),
                                                label: Text(tr('管理实例')))),
                                        const SizedBox(width: 8),
                                        IconButton(
                                            tooltip: tr('启动游戏'),
                                            onPressed: locked
                                                ? null
                                                : () => widget.onLaunch(i),
                                            icon: Icon(Icons.play_arrow_rounded,
                                                color: accent))
                                      ])
                                    ]));
                          });
                    })))
        ]));
  }
}
