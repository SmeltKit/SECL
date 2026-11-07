import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:convert';
import 'dart:typed_data';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'install_workflow.dart';
import 'resource_engine.dart';
import 'resource_detection.dart';
import 'resource_updates.dart';

class LocalResourceIcon extends StatefulWidget {
  const LocalResourceIcon(this.row,
      {super.key,
      this.size = 42,
      this.fallbackIcon = Icons.inventory_2_outlined});
  final Data row;
  final double size;
  final IconData fallbackIcon;
  @override
  State<LocalResourceIcon> createState() => _LocalResourceIconState();
}

class _LocalResourceIconState extends State<LocalResourceIcon> {
  String _encoded = '';
  Uint8List? _bytes;

  @override
  void initState() {
    super.initState();
    _updateIcon();
  }

  @override
  void didUpdateWidget(covariant LocalResourceIcon oldWidget) {
    super.didUpdateWidget(oldWidget);
    _updateIcon();
  }

  void _updateIcon() {
    final encoded = widget.row['icon_base64'] as String? ?? '';
    if (encoded == _encoded) return;
    _encoded = encoded;
    _bytes = null;
    if (encoded.isNotEmpty) {
      try {
        // MemoryImage keys its cache by byte-buffer identity. Keep that buffer
        // across polling rebuilds, instead of restarting the image decode.
        _bytes = base64Decode(encoded);
      } on FormatException {
        // Malformed local metadata falls back to the project icon below.
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    final size = widget.size;
    if (_bytes != null) {
      return SizedBox(
          width: size,
          height: size,
          child: Image.memory(_bytes!,
              fit: BoxFit.contain,
              cacheWidth: (size * 2).round(),
              errorBuilder: (_, __, ___) => ResourceImage(
                  widget.row['icon_url'] as String?,
                  icon: widget.fallbackIcon,
                  size: size)));
    }
    return ResourceImage(widget.row['icon_url'] as String?,
        icon: widget.fallbackIcon, size: size);
  }
}

class InstanceResources extends StatefulWidget {
  const InstanceResources(
      {super.key,
      required this.engine,
      required this.instance,
      required this.kind,
      required this.onBrowse});
  final ResourceEngine engine;
  final Data instance;
  final String kind;
  final void Function(String, {String? query}) onBrowse;
  @override
  State<InstanceResources> createState() => _InstanceResourcesState();
}

class _InstanceResourcesState extends State<InstanceResources>
    with ManagedState<InstanceResources> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> rows = [];
  Data checks = {}, support = {};
  List<Data> sourceErrors = [];
  String query = '', filter = 'all';
  Set<String> selected = {};
  bool updatesOpen = false;
  bool get mods => widget.kind == 'mod';
  String get label => tr(mods
      ? '模组'
      : widget.kind == 'shader'
          ? '光影'
          : '材质包');
  String get folder => widget.kind == 'mod'
      ? 'mods'
      : widget.kind == 'shader'
          ? 'shaderpacks'
          : 'resourcepacks';
  @override
  void initState() {
    super.initState();
    load();
  }

  Future<void> read({bool fresh = false}) async {
    final response = await rpc({
      'op': 'instance.inventory',
      'instance': widget.instance,
      'kind': widget.kind,
      if (fresh) 'fresh': true
    }) as Map;
    applyInventory(response);
  }

  void applyInventory(Map response) {
    if (mounted) {
      setState(() {
        rows = dataRows(response['items']);
        checks = Map<String, dynamic>.from(response['checks'] as Map? ?? {});
        support = Map<String, dynamic>.from(response['support'] as Map? ?? {});
        sourceErrors = response['source_errors'] is List
            ? dataRows(response['source_errors'])
            : response['source_error'] is Map
                ? [Map<String, dynamic>.from(response['source_error'] as Map)]
                : [];
        selected.removeWhere((n) => !rows.any((r) => r['filename'] == n));
      });
    }
  }

  Future<void> load({bool fresh = false}) => perform(() => read(fresh: fresh));
  Widget inventoryResult(Widget child) => rows.isEmpty || problem != null
      ? result(child)
      : Column(children: [
          if (loading) const LinearProgressIndicator(),
          Expanded(child: child)
        ]);
  Future<void> importLocal() => perform(() async {
        if (mods) {
          final files = await openFiles(acceptedTypeGroups: [
            const XTypeGroup(label: 'JAR', extensions: ['jar'])
          ]);
          if (files.isEmpty) return;
          await rpc({
            'op': 'mods.import',
            'instance': widget.instance,
            'files': files.map((f) => f.path).toList()
          });
        } else {
          final file = await openFile(acceptedTypeGroups: [
            const XTypeGroup(label: 'ZIP', extensions: ['zip'])
          ]);
          if (file == null) return;
          await rpc({
            'op': 'packs.import',
            'directory': widget.instance['game_directory'],
            'kind': widget.kind,
            'path': file.path
          });
        }
        await read();
      });
  Future<void> enable(bool value, List<String> names) => perform(() async {
        await rpc({
          'op': 'mods.enable',
          'instance': widget.instance,
          'files': names,
          'enabled': value
        });
        await read();
      });
  Future<void> remove(List<String> names) async {
    if (!await confirmOperation(
        context, tr('移除所选资源'), tr('将所选资源移到系统废纸篓；可从废纸篓恢复。'))) return;
    await perform(() async {
      if (mods) {
        await rpc(
            {'op': 'mods.remove', 'instance': widget.instance, 'files': names});
      } else {
        for (final name in names) {
          await rpc({
            'op': 'packs.remove',
            'directory': widget.instance['game_directory'],
            'kind': widget.kind,
            'name': name
          });
        }
      }
      await read();
    });
  }

  Future<void> setupShaders() => perform(() async {
        final plan = Map<String, dynamic>.from(
            await rpc({'op': 'shaders.plan', 'instance': widget.instance})
                as Map);
        if (!mounted) return;
        await reviewAndInstall(context, engine, plan);
        await read();
      });
  Future<void> enrich() => perform(() async {
        try {
          final response =
              await rpc({'op': 'mods.enrich', 'instance': widget.instance})
                  as Map;
          if (response['items'] is List) {
            applyInventory(response);
          } else {
            // Older cores return only an enrichment summary.
            await read();
          }
        } on EngineFailure catch (e) {
          if (mounted) setState(() => sourceErrors = [e.details]);
        }
      });
  Future<void> compatibility() => perform(() async {
        final result = await rpc({
          'op': 'quilt.update',
          'directory': widget.instance['game_directory'],
          'force': true
        }) as Map;
        await read();
        if (!mounted) return;
        await showDialog<void>(
            context: context,
            builder: (context) => AlertDialog(
                    title: Text(tr('Quilt 兼容性提醒')),
                    content: SizedBox(
                        width: 500,
                        child: SingleChildScrollView(
                            child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                              Text('${tr('来源')}：${result['source']}'),
                              const SizedBox(height: 12),
                              if (result['update_error'] != null)
                                Text(nativeText(result['update_error']
                                            ['message']
                                        ?.toString() ??
                                    '')),
                              for (final warning
                                  in checks['warnings'] as List? ?? [])
                                Padding(
                                    padding: const EdgeInsets.only(bottom: 12),
                                    child:
                                        Text(nativeText(warning.toString()))),
                              Text(tr('官方列表没有精确文件哈希，提醒不代表当前构建必然受影响。'))
                            ]))),
                    actions: [
                      TextButton(
                          onPressed: () => Navigator.pop(context),
                          child: Text(tr('完成')))
                    ]));
      });
  void menu(String action, Data row) {
    final name = row['filename'] as String;
    if (action == 'enable') enable(row['enabled'] != true, [name]);
    if (action == 'remove') remove([name]);
    if (action == 'find') {
      widget.onBrowse('mod',
          query: row['name'] as String? ?? row['id'] as String?);
    }
    if (action == 'folder') {
      perform(() async {
        await rpc({
          'op': 'path.open',
          'path': '${widget.instance['game_directory']}/$folder'
        });
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    if (updatesOpen) {
      return ResourceUpdates(
          engine: engine,
          instance: widget.instance,
          kind: widget.kind,
          onBack: () {
            setState(() => updatesOpen = false);
            load();
          });
    }
    final visible = rows
        .where((r) =>
            '${r['name']} ${r['id']} ${r['filename']}'
                .toLowerCase()
                .contains(query.toLowerCase()) &&
            (filter == 'all' ||
                filter == 'enabled' && r['enabled'] != false ||
                filter == 'disabled' && r['enabled'] == false ||
                filter == 'problem' && ResourceDetection(r).needsAttention))
        .toList();
    final allSelected = visible.isNotEmpty &&
        visible.every((r) => selected.contains(r['filename']));
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      if (widget.kind == 'shader')
        Padding(
            padding: const EdgeInsets.only(bottom: 14),
            child: SurfaceCard(
                color: support['available'] == true
                    ? seclColor(0xFFEDF6F1)
                    : seclColor(0xFFFFF6EA),
                child: Row(children: [
                  Icon(
                      support['available'] == true
                          ? Icons.check_circle_outline
                          : Icons.info_outline,
                      size: 22,
                      color: accent),
                  const SizedBox(width: 12),
                  Expanded(
                      child: Text(
                          support['available'] == true
                              ? '${tr('光影支持已就绪')} · ${(support['components'] as List? ?? []).join(' · ')}'
                              : tr('尚未配置可用的光影支持'),
                          style: const TextStyle(fontSize: 13))),
                  const SizedBox(width: 8),
                  OutlinedButton(
                      onPressed: locked ? null : setupShaders,
                      child: Text(tr('配置光影支持')))
                ]))),
      Wrap(
          spacing: 10,
          runSpacing: 10,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            PopupMenuButton<String>(
                enabled: !locked,
                onSelected: (v) =>
                    v == 'local' ? importLocal() : widget.onBrowse(widget.kind),
                itemBuilder: (_) => [
                      PopupMenuItem(value: 'local', child: Text(tr('从本地导入'))),
                      PopupMenuItem(value: 'market', child: Text(tr('浏览资源市场')))
                    ],
                child: IgnorePointer(
                    child: FilledButton.icon(
                        onPressed: () {},
                        icon: const Icon(Icons.add_rounded, size: 18),
                        label: Text('${tr('添加')}$label ▾')))),
            OutlinedButton.icon(
                onPressed:
                    locked ? null : () => setState(() => updatesOpen = true),
                icon: const Icon(Icons.system_update_alt_rounded, size: 18),
                label: Text(tr('检查更新'))),
            OutlinedButton.icon(
                onPressed: locked
                    ? null
                    : () => perform(() async {
                          await rpc({
                            'op': 'path.open',
                            'path':
                                '${widget.instance['game_directory']}/$folder'
                          });
                        }),
                icon: const Icon(Icons.folder_open_outlined, size: 18),
                label: Text(tr('打开目录'))),
            if (mods)
              TextButton.icon(
                  onPressed: locked ? null : enrich,
                  icon: const Icon(Icons.cloud_sync_outlined, size: 18),
                  label: Text(tr('补全来源信息'))),
            if (mods && widget.instance['loader'] == 'quilt')
              TextButton(
                  onPressed: locked ? null : compatibility,
                  child: Text(tr('更新兼容提醒')))
          ]),
      const SizedBox(height: 14),
      if (sourceErrors.isNotEmpty) ...[
        ResourceSourceFailure(sourceErrors, retry: locked ? null : enrich),
        const SizedBox(height: 12)
      ],
      Row(children: [
        Expanded(
            child: TextField(
                decoration: InputDecoration(
                    hintText: tr(mods ? '搜索名称、Mod ID 或文件名' : '搜索名称或文件名'),
                    prefixIcon: const Icon(Icons.search_rounded)),
                onChanged: (v) => setState(() => query = v))),
        if (mods) ...[
          const SizedBox(width: 10),
          SizedBox(
              width: 130,
              child: DropdownButtonFormField<String>(
                  key: const Key('resourceStatusFilter'),
                  value: filter,
                  isExpanded: true,
                  items: [
                    for (final entry in {
                      'all': tr('全部状态'),
                      'enabled': tr('已启用'),
                      'disabled': tr('已禁用'),
                      'problem': tr('需要处理')
                    }.entries)
                      DropdownMenuItem(
                          value: entry.key,
                          child: Text(entry.value,
                              style: const TextStyle(fontSize: 12)))
                  ],
                  onChanged: (v) => setState(() => filter = v ?? 'all')))
        ],
        const SizedBox(width: 8),
        IconButton(
            tooltip: tr('刷新'),
            onPressed: locked ? null : () => load(fresh: true),
            icon: const Icon(Icons.refresh_rounded))
      ]),
      const SizedBox(height: 12),
      Text(
          mods
              ? tr('{0} 个模组 · {1} 个已启用', [
                  rows.length,
                  rows.where((r) => r['enabled'] != false).length
                ])
              : tr('{0} 个已安装', [rows.length]),
          style: TextStyle(color: muted, fontSize: 12)),
      const SizedBox(height: 10),
      Expanded(
          child: inventoryResult(visible.isEmpty
              ? Center(child: Text(tr('尚未添加资源，从本地导入或浏览资源市场。')))
              : LayoutBuilder(builder: (context, bounds) {
                  final compact = bounds.maxWidth < 780;
                  return SingleChildScrollView(
                      child: ManagementTable(headers: [
                    if (mods)
                      AppCheck(
                          value: allSelected
                              ? true
                              : selected.isEmpty
                                  ? false
                                  : null,
                          tristate: true,
                          onChanged: locked
                              ? null
                              : (v) => setState(() {
                                    if (v == true) {
                                      selected.addAll(visible
                                          .map((r) => r['filename'] as String));
                                    } else {
                                      selected.clear();
                                    }
                                  })),
                    Text(tr('名称')),
                    Text(tr('版本')),
                    if (mods && !compact) Text(tr('加载器')),
                    Text(tr('状态')),
                    if (mods) Text(tr('启用')),
                    Text(tr('操作'))
                  ], widths: {
                    if (mods) 0: const FixedColumnWidth(48),
                    mods ? 1 : 0: const FlexColumnWidth(3),
                    mods ? 2 : 1: const FlexColumnWidth(1.5),
                    if (mods && !compact) 3: const FlexColumnWidth(1),
                    mods ? (compact ? 3 : 4) : 2: const FlexColumnWidth(1.5),
                    if (mods) (compact ? 4 : 5): const FixedColumnWidth(78),
                    mods ? (compact ? 5 : 6) : 3: const FixedColumnWidth(70)
                  }, selected: {
                    for (var n = 0; n < visible.length; n++)
                      if (selected.contains(visible[n]['filename'])) n
                  }, rows: [
                    for (final row in visible)
                      [
                        if (mods)
                          AppCheck(
                              value: selected.contains(row['filename']),
                              onChanged: locked
                                  ? null
                                  : (v) => setState(() => v == true
                                      ? selected.add(row['filename'] as String)
                                      : selected.remove(row['filename']))),
                        Row(children: [
                          LocalResourceIcon(row),
                          const SizedBox(width: 12),
                          Expanded(
                              child: Tooltip(
                                  message: row['filename'] as String,
                                  child: Column(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.start,
                                      children: [
                                        Text(row['name'] ?? row['filename'],
                                            maxLines: 2,
                                            overflow: TextOverflow.ellipsis,
                                            style: const TextStyle(
                                                fontWeight: FontWeight.w600)),
                                        const SizedBox(height: 5),
                                        Text(
                                            mods
                                                ? '${row['id'] ?? ''}${compact ? ' · ${row['loader'] ?? ''}' : ''}'
                                                : row['description']
                                                        as String? ??
                                                    '',
                                            maxLines: 2,
                                            overflow: TextOverflow.ellipsis,
                                            style: TextStyle(
                                                fontSize: 11, color: muted))
                                      ])))
                        ]),
                        Tooltip(
                            message: row['version'] as String? ?? '',
                            child: Text(row['version'] as String? ?? '—',
                                maxLines: 2, overflow: TextOverflow.ellipsis)),
                        if (mods && !compact)
                          Text(row['loader'] as String? ?? '—'),
                        if (mods)
                          ResourceDetectionStatus(row)
                        else
                          Tooltip(
                              message: [
                                ...row['errors'] as List? ?? [],
                                ...row['warnings'] as List? ?? [],
                                row['problem'] ?? ''
                              ]
                                  .map((value) => nativeText(value.toString()))
                                  .join('\n'),
                              child: Text(
                                  (row['errors'] as List? ?? []).isNotEmpty
                                      ? tr('需要处理')
                                      : (row['problem'] as String? ?? '')
                                              .isNotEmpty
                                          ? tr('未识别')
                                          : row['enabled'] == false
                                              ? tr('已禁用')
                                              : tr('已安装'),
                                  style: TextStyle(
                                      color: (row['errors'] as List? ?? [])
                                              .isNotEmpty
                                          ? seclColor(0xFFB66C23)
                                          : row['enabled'] == false
                                              ? muted
                                              : seclColor(0xFF2F7C55)))),
                        if (mods)
                          Switch(
                              value: row['enabled'] != false,
                              onChanged: locked
                                  ? null
                                  : (v) =>
                                      enable(v, [row['filename'] as String])),
                        PopupMenuButton<String>(
                            tooltip: tr('更多操作'),
                            padding: EdgeInsets.zero,
                            icon: const Icon(Icons.more_horiz_rounded),
                            onSelected: (v) => menu(v, row),
                            itemBuilder: (_) => [
                                  if (mods) ...[
                                    PopupMenuItem(
                                        value: 'find',
                                        child: Text(tr('查找适配版本'))),
                                    PopupMenuItem(
                                        value: 'enable',
                                        child: Text(tr(row['enabled'] == false
                                            ? '启用'
                                            : '禁用')))
                                  ],
                                  PopupMenuItem(
                                      value: 'folder', child: Text(tr('打开目录'))),
                                  PopupMenuItem(
                                      value: 'remove',
                                      child: Text(tr('移除'),
                                          style: const TextStyle(
                                              color: Colors.red)))
                                ])
                      ]
                  ]));
                }))),
      if ((checks['errors'] as List? ?? []).isNotEmpty)
        Padding(
            padding: const EdgeInsets.symmetric(vertical: 10),
            child: Text(
                (checks['errors'] as List)
                    .map((e) => nativeText(e.toString()))
                    .join('\n'),
                style: TextStyle(
                    fontSize: 12, color: seclColor(0xFFB66C23), height: 1.6))),
      if (selected.isNotEmpty)
        Padding(
            padding: const EdgeInsets.symmetric(vertical: 10),
            child: SurfaceCard(
                color: seclColor(0xFFEDF3F8),
                child: Wrap(
                    spacing: 10,
                    runSpacing: 8,
                    crossAxisAlignment: WrapCrossAlignment.center,
                    children: [
                      Text(tr('已选择 {0} 个模组', [selected.length])),
                      OutlinedButton.icon(
                          onPressed: locked
                              ? null
                              : () => widget.onBrowse('mod',
                                  query: rows.firstWhere((r) => selected
                                          .contains(r['filename']))['name']
                                      as String?),
                          icon: const Icon(Icons.search_rounded, size: 17),
                          label: Text(tr('查找适配版本'))),
                      OutlinedButton.icon(
                          onPressed: locked
                              ? null
                              : () => enable(false, selected.toList()),
                          icon: const Icon(Icons.block_outlined, size: 17),
                          label: Text(tr('禁用'))),
                      OutlinedButton.icon(
                          onPressed: locked
                              ? null
                              : () => enable(true, selected.toList()),
                          icon:
                              const Icon(Icons.check_circle_outline, size: 17),
                          label: Text(tr('启用'))),
                      TextButton.icon(
                          onPressed:
                              locked ? null : () => remove(selected.toList()),
                          icon: const Icon(Icons.delete_outline, size: 17),
                          label: Text(tr('移除')))
                    ]))),
      if (!mods)
        Padding(
            padding: const EdgeInsets.symmetric(vertical: 14),
            child: Text(
                tr(widget.kind == 'shader'
                    ? '在游戏内选择光影和调整画质。'
                    : '在游戏内选择材质包并调整加载顺序。'),
                style: TextStyle(fontSize: 12, color: muted)))
    ]);
  }
}
