// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart';
import 'app_widgets.dart';
import 'instance_resources.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

String worldDate(num? v, {bool seconds = false}) => v == null || v <= 0
    ? tr('未知')
    : DateFormat('yyyy-MM-dd HH:mm').format(
        DateTime.fromMillisecondsSinceEpoch(v.toInt() * (seconds ? 1000 : 1))
            .toLocal());

String worldSize(num bytes) => bytes < 1024
    ? '${bytes.toInt()} B'
    : bytes >= 1073741824
        ? '${(bytes / 1073741824).toStringAsFixed(1)} GiB'
        : byteLabel(bytes);

class InstanceWorlds extends StatefulWidget {
  const InstanceWorlds(
      {super.key, required this.engine, required this.instance});
  final ResourceEngine engine;
  final Data instance;
  @override
  State<InstanceWorlds> createState() => _InstanceWorldsState();
}

class _InstanceWorldsState extends State<InstanceWorlds>
    with ManagedState<InstanceWorlds> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> worlds = [];
  String query = '', sort = 'date';
  String? selected;
  final importMenu = GlobalKey<PopupMenuButtonState<bool>>();
  String get game => widget.instance['game_directory'] as String;
  @override
  void initState() {
    super.initState();
    load();
  }

  Future<dynamic> world(String op, [Data extra = const {}]) =>
      engine.run({'op': 'worlds.$op', 'instance': widget.instance, ...extra},
          title: tr('存档管理'));
  Future<void> read() async {
    final rows = dataRows(await world('list'));
    if (mounted) {
      setState(() {
        worlds = rows;
        if (!rows.any((w) => w['folder'] == selected)) selected = null;
      });
    }
  }

  Future<void> load() => perform(read);
  void done(String text) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(text)));
    }
  }

  Future<String?> name(String title, String initial) => showDialog<String>(
      context: context,
      builder: (_) => WorldNameDialog(title: title, initial: initial));
  Future<void> importWorld(bool folder) async {
    final source = folder
        ? await getDirectoryPath()
        : (await openFile(acceptedTypeGroups: [
            const XTypeGroup(label: 'ZIP', extensions: ['zip'])
          ]))
            ?.path;
    if (source == null || !mounted) return;
    final base = source.replaceAll('\\', '/').split('/').last;
    final choice = await name(
        tr('导入存档'),
        folder
            ? base
            : base.replaceFirst(RegExp(r'\.zip$', caseSensitive: false), ''));
    if (choice == null) return;
    await perform(() async {
      await world('import', {'path': source, 'name': choice});
      await read();
    });
  }

  Future<void> export(Data row) async {
    final location = await getSaveLocation(
        suggestedName: '${row['folder']}.zip',
        acceptedTypeGroups: [
          const XTypeGroup(label: 'ZIP', extensions: ['zip'])
        ]);
    if (location == null) return;
    await perform(() async {
      await world('export', {'folder': row['folder'], 'path': location.path});
      done(tr('存档已导出'));
    });
  }

  Future<void> backup(Data row) => perform(() async {
        await world('backup', {'folder': row['folder']});
        done(tr('备份已完成'));
      });
  Future<void> remove(Data row) async {
    if (!await confirmOperation(
        context, tr('移除存档'), tr('将「{0}」移到系统废纸篓，可从废纸篓恢复。', [row['name']]))) {
      return;
    }
    await perform(() async {
      await world('remove', {'folder': row['folder']});
      await read();
    });
  }

  Future<void> copy(Data row, bool other) async {
    Data? target;
    if (other) {
      List<Data> options = [];
      await perform(() async {
        final response = await rpc({
          'op': 'instances.scan',
          'roots': [widget.instance['root']]
        }) as Map;
        options = dataRows(response['instances'])
            .where((i) => i['game_directory'] != game)
            .toList();
      });
      if (!mounted || problem != null) return;
      target = await showDialog<Data>(
          context: context,
          builder: (context) => AlertDialog(
                  title: Text(tr('复制到其他实例')),
                  content: SizedBox(
                      width: 480,
                      height: 280,
                      child: options.isEmpty
                          ? Center(child: Text(tr('没有其他可用实例')))
                          : ListView(children: [
                              for (final item in options)
                                ListTile(
                                    leading: const Icon(
                                        Icons.sports_esports_outlined),
                                    title: Text(item['name'] ?? item['id']),
                                    subtitle: Text(
                                        '${item['version']} · ${item['loader']} ${item['loader_version']}'),
                                    onTap: () => Navigator.pop(context, item))
                            ])),
                  actions: [
                    TextButton(
                        onPressed: () => Navigator.pop(context),
                        child: Text(tr('取消')))
                  ]));
      if (target == null || !mounted) return;
      if (!await confirmOperation(
          context,
          tr('确认复制存档'),
          tr('目标：{0}。游戏版本或模组环境可能不同，降级可能损坏世界；复制不会改变原存档，也不保证兼容。',
              [target['name'] ?? target['id']]))) return;
    }
    if (!mounted) return;
    final choice = await name(tr('复制存档'), '${row['folder']}-copy');
    if (choice == null) return;
    await perform(() async {
      await world('copy', {
        'folder': row['folder'],
        'name': choice,
        if (target != null) 'target_instance': target
      });
      await read();
      done(tr('存档已复制'));
    });
  }

  Future<void> backups() async {
    List<Data> rows = [];
    await perform(() async {
      rows = dataRows(await world('backups'));
    });
    if (!mounted || problem != null) return;
    final choice = await showDialog<Data>(
        context: context,
        builder: (context) => AlertDialog(
                title: Text(tr('备份记录')),
                content: SizedBox(
                    width: 520,
                    height: 320,
                    child: rows.isEmpty
                        ? Center(child: Text(tr('暂无备份，选中存档后点击备份。')))
                        : ListView(children: [
                            for (final item in rows)
                              ListTile(
                                  leading: const Icon(Icons.restore_rounded),
                                  title: Text(item['name'] ?? item['world']),
                                  subtitle: Text(
                                      '${worldDate(item['created'] as num?, seconds: true)} · ${worldSize(item['size'] as num)}'),
                                  trailing: Text(tr('恢复副本')),
                                  onTap: () => Navigator.pop(context, item))
                          ])),
                actions: [
                  TextButton(
                      onPressed: () => Navigator.pop(context),
                      child: Text(tr('关闭')))
                ]));
    if (choice == null || !mounted) return;
    final folder = await name(tr('恢复为新存档'), '${choice['world']}-restored');
    if (folder == null) return;
    await perform(() async {
      await world('restore', {'archive': choice['archive'], 'name': folder});
      await read();
      done(tr('备份已恢复为副本'));
    });
  }

  String mode(Data row) => row['hardcore'] == true
      ? tr('极限')
      : switch (row['mode']) {
          0 => tr('生存'),
          1 => tr('创造'),
          2 => tr('冒险'),
          3 => tr('旁观'),
          _ => tr('未知')
        };
  @override
  Widget build(BuildContext context) {
    final visible = worlds
        .where((w) => '${w['name']} ${w['folder']}'
            .toLowerCase()
            .contains(query.toLowerCase()))
        .toList();
    visible.sort((a, b) => sort == 'name'
        ? '${a['name']}'.compareTo('${b['name']}')
        : ((b[sort == 'size' ? 'size' : 'last_played'] as num?) ?? 0).compareTo(
            (a[sort == 'size' ? 'size' : 'last_played'] as num?) ?? 0));
    final row = worlds.where((w) => w['folder'] == selected).firstOrNull;
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Wrap(
          spacing: 10,
          runSpacing: 8,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            PopupMenuButton<bool>(
                key: importMenu,
                enabled: !locked,
                onSelected: importWorld,
                itemBuilder: (_) => [
                      PopupMenuItem(value: false, child: Text(tr('从 ZIP 导入'))),
                      PopupMenuItem(value: true, child: Text(tr('从文件夹导入')))
                    ],
                child: FilledButton.icon(
                    onPressed: locked
                        ? null
                        : () => importMenu.currentState?.showButtonMenu(),
                    icon: const Icon(Icons.add_rounded, size: 18),
                    label: Row(mainAxisSize: MainAxisSize.min, children: [
                      Text(tr('导入存档')),
                      const SizedBox(width: 5),
                      const Icon(Icons.expand_more_rounded, size: 18)
                    ]))),
            OutlinedButton.icon(
                onPressed: locked
                    ? null
                    : () => perform(() async {
                          await world('open');
                        }),
                icon: const Icon(Icons.folder_open_outlined, size: 18),
                label: Text(tr('打开目录'))),
            OutlinedButton.icon(
                onPressed: locked ? null : backups,
                icon: const Icon(Icons.history_rounded, size: 18),
                label: Text(tr('备份记录'))),
            if (loading)
              TextButton.icon(
                  onPressed: engine.cancel,
                  icon: const Icon(Icons.close_rounded, size: 18),
                  label: Text(tr('取消')))
          ]),
      const SizedBox(height: 14),
      Row(children: [
        Expanded(
            child: TextField(
                onChanged: (v) => setState(() => query = v),
                decoration: InputDecoration(
                    hintText: tr('搜索存档名称或文件夹'),
                    prefixIcon: const Icon(Icons.search_rounded)))),
        const SizedBox(width: 8),
        PopupMenuButton<String>(
            tooltip: tr('排序'),
            onSelected: (v) => setState(() => sort = v),
            itemBuilder: (_) => [
                  for (final item in {
                    'date': tr('最后游玩'),
                    'name': tr('名称'),
                    'size': tr('占用空间')
                  }.entries)
                    CheckedPopupMenuItem(
                        value: item.key,
                        checked: sort == item.key,
                        child: Text(item.value))
                ],
            icon: const Icon(Icons.sort_rounded)),
        IconButton(
            onPressed: locked ? null : load,
            tooltip: tr('刷新'),
            icon: const Icon(Icons.refresh_rounded))
      ]),
      const SizedBox(height: 12),
      Expanded(
          child: result(visible.isEmpty
              ? Center(
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.public_rounded, size: 40, color: muted),
                  const SizedBox(height: 14),
                  Text(tr(worlds.isEmpty ? '尚未找到存档' : '没有匹配的存档')),
                  const SizedBox(height: 6),
                  Text(tr('导入已有世界，或在游戏中创建世界。'), style: TextStyle(color: muted))
                ]))
              : LayoutBuilder(builder: (context, bounds) {
                  final wide = bounds.maxWidth >= 800;
                  return SingleChildScrollView(
                      child: ManagementTable(headers: [
                    const SizedBox(),
                    Text(tr('名称')),
                    Text(tr('游戏版本')),
                    if (wide) Text(tr('模式')),
                    Text(tr('最后游玩')),
                    Text(tr('占用空间'))
                  ], widths: {
                    0: const FixedColumnWidth(48),
                    1: const FlexColumnWidth(3),
                    2: const FlexColumnWidth(1.2),
                    if (wide) 3: const FlexColumnWidth(1),
                    wide ? 4 : 3: const FlexColumnWidth(1.8),
                    wide ? 5 : 4: const FlexColumnWidth(1.2)
                  }, selected: {
                    for (var i = 0; i < visible.length; i++)
                      if (visible[i]['folder'] == selected) i
                  }, rows: [
                    for (final w in visible)
                      [
                        AppCheck(
                            value: w['folder'] == selected,
                            onChanged: locked
                                ? null
                                : (v) => setState(() => selected =
                                    v == true ? w['folder'] as String : null)),
                        InkWell(
                            onTap: () => setState(
                                () => selected = w['folder'] as String),
                            child: Row(children: [
                              LocalResourceIcon(w,
                                  size: 48,
                                  fallbackIcon: Icons.public_outlined),
                              const SizedBox(width: 12),
                              Expanded(
                                  child: Column(
                                      crossAxisAlignment:
                                          CrossAxisAlignment.start,
                                      children: [
                                    Text(w['name'] as String,
                                        maxLines: 2,
                                        overflow: TextOverflow.ellipsis,
                                        style: const TextStyle(
                                            fontWeight: FontWeight.w600)),
                                    const SizedBox(height: 5),
                                    Text(w['folder'] as String,
                                        maxLines: 1,
                                        overflow: TextOverflow.ellipsis,
                                        style: TextStyle(
                                            color: muted, fontSize: 11)),
                                    if ((w['problem'] as String? ?? '')
                                        .isNotEmpty)
                                      Tooltip(
                                          message: nativeText(
                                              w['problem'] as String),
                                          child: Text(tr('信息读取异常'),
                                              style: TextStyle(
                                                  color: Theme.of(context)
                                                      .colorScheme
                                                      .error,
                                                  fontSize: 11)))
                                  ]))
                            ])),
                        Text((w['version'] as String).isEmpty
                            ? tr('未知')
                            : w['version'] as String),
                        if (wide) Text(mode(w)),
                        Text(worldDate(w['last_played'] as num?)),
                        Text(w['size'] is num
                            ? worldSize(w['size'] as num)
                            : tr('未知'))
                      ]
                  ]));
                }))),
      if (row != null)
        Padding(
            padding: const EdgeInsets.only(top: 12),
            child: SurfaceCard(
                padding: const EdgeInsets.all(10),
                child: Wrap(spacing: 6, runSpacing: 6, children: [
                  TextButton.icon(
                      onPressed: locked ? null : () => backup(row),
                      icon: const Icon(Icons.backup_outlined, size: 18),
                      label: Text(tr('备份'))),
                  TextButton.icon(
                      onPressed: locked ? null : () => export(row),
                      icon: const Icon(Icons.ios_share_rounded, size: 18),
                      label: Text(tr('导出 ZIP'))),
                  TextButton.icon(
                      onPressed: locked ? null : () => copy(row, false),
                      icon: const Icon(Icons.copy_outlined, size: 18),
                      label: Text(tr('复制存档'))),
                  TextButton.icon(
                      onPressed: locked ? null : () => copy(row, true),
                      icon:
                          const Icon(Icons.drive_file_move_outlined, size: 18),
                      label: Text(tr('复制到其他实例'))),
                  TextButton.icon(
                      onPressed: locked ? null : () => remove(row),
                      icon: const Icon(Icons.delete_outline_rounded, size: 18),
                      label: Text(tr('移除存档')))
                ]))),
      const SizedBox(height: 10),
      Text(tr('修改、导出和备份前请退出该实例的游戏。恢复会创建新副本。'),
          style: TextStyle(fontSize: 11, color: muted))
    ]);
  }
}

class WorldNameDialog extends StatefulWidget {
  const WorldNameDialog(
      {super.key, required this.title, required this.initial});
  final String title, initial;
  @override
  State<WorldNameDialog> createState() => _WorldNameDialogState();
}

class _WorldNameDialogState extends State<WorldNameDialog> {
  late final controller = TextEditingController(text: widget.initial);
  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  void submit() {
    if (controller.text.trim().isNotEmpty) {
      Navigator.pop(context, controller.text.trim());
    }
  }

  @override
  Widget build(BuildContext context) => AlertDialog(
          title: Text(widget.title),
          content: SizedBox(
              width: 400,
              child: TextField(
                  controller: controller,
                  autofocus: true,
                  decoration: InputDecoration(
                      labelText: tr('存档文件夹名称'),
                      helperText: tr('使用新名称，原有存档不会被覆盖。'),
                      helperMaxLines: 3),
                  onSubmitted: (_) => submit())),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(context), child: Text(tr('取消'))),
            FilledButton(onPressed: submit, child: Text(tr('确认')))
          ]);
}
