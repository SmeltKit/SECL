// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'instance_worlds.dart' show worldDate, worldSize;
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

class InstanceBackups extends StatefulWidget {
  const InstanceBackups(
      {super.key,
      required this.engine,
      required this.instance,
      required this.onRestored,
      this.pickDirectory});
  final ResourceEngine engine;
  final Data instance;
  final VoidCallback onRestored;
  final Future<String?> Function()? pickDirectory;
  @override
  State<InstanceBackups> createState() => _InstanceBackupsState();
}

class _InstanceBackupsState extends State<InstanceBackups>
    with ManagedState<InstanceBackups> {
  @override
  ResourceEngine get engine => widget.engine;
  String directory = '';
  List<Data> backups = [];
  @override
  void initState() {
    super.initState();
    load();
  }

  Future<dynamic> backup(String op, [Data extra = const {}]) => engine.run({
        'op': 'instance.backups.$op',
        'instance': widget.instance,
        if (directory.isNotEmpty) 'directory': directory,
        ...extra
      }, title: tr('实例备份'));
  Future<void> read() async {
    final response = Map<String, dynamic>.from(await backup('list') as Map);
    if (mounted) {
      setState(() {
        directory = response['directory'] as String;
        backups = dataRows(response['items']);
      });
    }
  }

  Future<void> load() => perform(read);
  void notice(String message) {
    if (mounted) {
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text(message)));
    }
  }

  Future<void> chooseDirectory() async {
    final selected = await (widget.pickDirectory?.call() ?? getDirectoryPath());
    if (selected == null || !mounted) return;
    setState(() => directory = selected);
    await load();
  }

  Future<void> create() => perform(() async {
        await backup('create');
        await read();
        notice(tr('实例备份已完成'));
      });
  Future<void> restore(Data row) async {
    final choice = await showDialog<Data>(
        context: context,
        builder: (_) => RestoreInstanceBackupDialog(
            sourceId: row['id'] as String,
            sourceName: row['name'] as String,
            root: widget.instance['root'] as String));
    if (choice == null || !mounted) return;
    await perform(() async {
      await backup('restore',
          {'backup': row['path'], 'root': widget.instance['root'], ...choice});
      if (mounted) {
        widget.onRestored();
        notice(tr('已恢复为新的独立实例'));
      }
    });
  }

  Widget scope(BuildContext context) => SurfaceCard(
      padding: EdgeInsets.zero,
      child: ExpansionTile(
          key: const Key('instanceBackupScope'),
          shape: const Border(),
          collapsedShape: const Border(),
          leading: Icon(Icons.backup_outlined, color: accent, size: 22),
          title: Text(tr('备份内容与限制'),
              style:
                  const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
          subtitle: Text(tr('不含账号、共享资源库或 Java；恢复仍需已有游戏依赖。'),
              style: TextStyle(color: muted, fontSize: 12, height: 1.45)),
          children: [
            Padding(
                padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      Text(tr('包含独立游戏目录、版本配置与客户端 JAR、实例设置、启动统计和资源版本锁。'),
                          style: const TextStyle(fontSize: 13, height: 1.55)),
                      const SizedBox(height: 6),
                      Text(tr('不包含启动器账号、共享 assets / libraries 或 Java。恢复仍需当前游戏根目录中的依赖；请保留这些文件。'),
                          style: TextStyle(
                              color: muted, fontSize: 12, height: 1.55)),
                      const SizedBox(height: 6),
                      Text(tr('请先退出游戏。备份是带 SHA-256 清单的文件夹，不是 ZIP；模组配置和日志可能包含个人信息。'),
                          style: TextStyle(
                              color: muted, fontSize: 12, height: 1.55))
                    ]))
          ]));

  Widget backupRow(BuildContext context, Data row) {
    final problem = row['problem'] as String? ?? '';
    return SurfaceCard(
        padding: const EdgeInsets.all(14),
        child: Row(crossAxisAlignment: CrossAxisAlignment.center, children: [
          Icon(
              problem.isEmpty
                  ? Icons.inventory_2_outlined
                  : Icons.warning_amber_rounded,
              color: problem.isEmpty
                  ? accent
                  : Theme.of(context).colorScheme.error,
              size: 26),
          const SizedBox(width: 12),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(row['name'] as String,
                    style: const TextStyle(
                        fontWeight: FontWeight.w600, height: 1.45)),
                const SizedBox(height: 5),
                if (problem.isNotEmpty)
                  Text(tr(problem),
                      style: TextStyle(
                          color: Theme.of(context).colorScheme.error,
                          fontSize: 12))
                else
                  Text(
                      '${worldDate(row['created'] as num?, seconds: true)} · '
                      '${worldSize(row['size'] as num)} · ${tr('{0} 个文件', [
                            row['file_count']
                          ])}',
                      style:
                          TextStyle(color: muted, fontSize: 12, height: 1.5)),
                Tooltip(
                    message: row['path'] as String,
                    child: Text(
                        (row['path'] as String)
                            .replaceAll('\\', '/')
                            .split('/')
                            .last,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style:
                            TextStyle(color: muted, fontSize: 11, height: 1.5)))
              ])),
          const SizedBox(width: 10),
          OutlinedButton.icon(
              onPressed:
                  locked || problem.isNotEmpty ? null : () => restore(row),
              icon: const Icon(Icons.restore_rounded, size: 18),
              label: Text(tr('恢复副本')))
        ]));
  }

  @override
  Widget build(BuildContext context) =>
      CustomScrollView(key: const Key('instanceBackupScroll'), slivers: [
        SliverToBoxAdapter(
            child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
              scope(context),
              const SizedBox(height: 14),
              Wrap(
                  spacing: 10,
                  runSpacing: 8,
                  crossAxisAlignment: WrapCrossAlignment.center,
                  children: [
                    FilledButton.icon(
                        key: const Key('createInstanceBackup'),
                        onPressed: locked ? null : create,
                        icon: const Icon(Icons.add_rounded, size: 19),
                        label: Text(tr('创建实例备份'))),
                    OutlinedButton.icon(
                        key: const Key('chooseInstanceBackupDirectory'),
                        onPressed: locked ? null : chooseDirectory,
                        icon: const Icon(Icons.drive_file_move_outlined,
                            size: 18),
                        label: Text(tr('选择备份位置'))),
                    OutlinedButton.icon(
                        onPressed: locked ? null : load,
                        icon: const Icon(Icons.refresh_rounded, size: 18),
                        label: Text(tr('刷新'))),
                    TextButton.icon(
                        onPressed: locked || directory.isEmpty
                            ? null
                            : () => perform(() async {
                                  await rpc(
                                      {'op': 'path.open', 'path': directory});
                                }),
                        icon: const Icon(Icons.folder_open_outlined, size: 18),
                        label: Text(tr('打开目录')))
                  ]),
              const SizedBox(height: 8),
              SelectableText(directory.isEmpty ? tr('正在读取备份位置') : directory,
                  style: TextStyle(color: muted, fontSize: 11, height: 1.5)),
              const SizedBox(height: 12)
            ])),
        if (loading)
          SliverToBoxAdapter(
              child:
                  SizedBox(height: 220, child: _BackupProgress(engine: engine)))
        else if (problem != null)
          SliverToBoxAdapter(
              child: SizedBox(
                  height: 280,
                  child:
                      ProblemPanel(problem!, retry: () => perform(retryWork!))))
        else if (backups.isEmpty)
          SliverToBoxAdapter(
              child: Padding(
                  padding: const EdgeInsets.symmetric(vertical: 40),
                  child: Center(
                      child: Column(mainAxisSize: MainAxisSize.min, children: [
                    Icon(Icons.inventory_2_outlined, color: muted, size: 36),
                    const SizedBox(height: 12),
                    Text(tr('还没有实例备份')),
                    const SizedBox(height: 6),
                    Text(tr('创建备份，或选择包含 .secl-backup 文件夹的目录。'),
                        textAlign: TextAlign.center,
                        style: TextStyle(color: muted, fontSize: 12))
                  ]))))
        else
          SliverList(
              delegate: SliverChildBuilderDelegate(
                  (context, index) => index.isOdd
                      ? const SizedBox(height: 10)
                      : backupRow(context, backups[index ~/ 2]),
                  childCount: backups.length * 2 - 1)),
        const SliverToBoxAdapter(child: SizedBox(height: 10))
      ]);
}

class _BackupProgress extends StatelessWidget {
  const _BackupProgress({required this.engine});
  final ResourceEngine engine;
  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation: engine,
      builder: (context, _) {
        final done = engine.progress['done'] as num? ?? -1;
        final total = engine.progress['total'] as num? ?? -1;
        final message = engine.progress['message'] as String? ?? '';
        return LayoutBuilder(
            builder: (context, bounds) => SingleChildScrollView(
                child: ConstrainedBox(
                    constraints: BoxConstraints(minHeight: bounds.maxHeight),
                    child: Center(
                        child: ConstrainedBox(
                            constraints: const BoxConstraints(maxWidth: 420),
                            child: Padding(
                                padding:
                                    const EdgeInsets.symmetric(vertical: 12),
                                child: Column(
                                    mainAxisSize: MainAxisSize.min,
                                    children: [
                                      const Icon(Icons.backup_outlined,
                                          size: 36),
                                      const SizedBox(height: 16),
                                      Text(
                                          message.isEmpty
                                              ? tr('正在准备文件')
                                              : tr(message),
                                          textAlign: TextAlign.center),
                                      const SizedBox(height: 14),
                                      LinearProgressIndicator(
                                          value: done >= 0 && total > 0
                                              ? (done / total).clamp(0.0, 1.0)
                                              : null),
                                      if (done >= 0 && total > 0)
                                        Padding(
                                            padding:
                                                const EdgeInsets.only(top: 8),
                                            child: Text(
                                                '${worldSize(done)} / ${worldSize(total)}',
                                                style: TextStyle(
                                                    fontSize: 12,
                                                    color: muted))),
                                      const SizedBox(height: 14),
                                      OutlinedButton.icon(
                                          key:
                                              const Key('cancelInstanceBackup'),
                                          onPressed: engine.active
                                              ? engine.cancel
                                              : null,
                                          icon: const Icon(Icons.close_rounded,
                                              size: 18),
                                          label: Text(tr('取消')))
                                    ])))))));
      });
}

class RestoreInstanceBackupDialog extends StatefulWidget {
  const RestoreInstanceBackupDialog(
      {super.key,
      required this.sourceId,
      required this.sourceName,
      required this.root});
  final String sourceId, sourceName, root;
  @override
  State<RestoreInstanceBackupDialog> createState() =>
      _RestoreInstanceBackupDialogState();
}

class _RestoreInstanceBackupDialogState
    extends State<RestoreInstanceBackupDialog> {
  late final id = TextEditingController(text: '${widget.sourceId}-restored');
  late final name =
      TextEditingController(text: '${widget.sourceName} (${tr('恢复副本')})');
  @override
  void dispose() {
    id.dispose();
    name.dispose();
    super.dispose();
  }

  bool get valid =>
      id.text.trim().isNotEmpty &&
      id.text.trim() != widget.sourceId &&
      !RegExp(r'[\\/:*?"<>|]').hasMatch(id.text) &&
      name.text.trim().isNotEmpty;
  @override
  Widget build(BuildContext context) => AlertDialog(
          title: Text(tr('恢复为新实例')),
          content: SizedBox(
              width: 440,
              child: SingleChildScrollView(
                  child: Column(
                      mainAxisSize: MainAxisSize.min,
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                    TextField(
                        key: const Key('restoreInstanceId'),
                        controller: id,
                        onChanged: (_) => setState(() {}),
                        decoration: InputDecoration(
                            labelText: tr('新实例标识'),
                            helperText: tr('必须使用新标识，不会覆盖原实例。'),
                            helperMaxLines: 2)),
                    const SizedBox(height: 14),
                    TextField(
                        key: const Key('restoreInstanceName'),
                        controller: name,
                        onChanged: (_) => setState(() {}),
                        decoration: InputDecoration(labelText: tr('实例名称'))),
                    const SizedBox(height: 16),
                    Text(tr('恢复位置'),
                        style: const TextStyle(fontWeight: FontWeight.w600)),
                    const SizedBox(height: 6),
                    SelectableText('${widget.root}/versions/${id.text.trim()}',
                        style:
                            TextStyle(fontSize: 12, height: 1.5, color: muted)),
                    const SizedBox(height: 14),
                    Text(
                        tr('共享游戏依赖不会从备份补齐。固定 Java 和外部封面会重置；自定义参数中的绝对路径请恢复后检查。'),
                        style:
                            TextStyle(fontSize: 12, height: 1.6, color: muted))
                  ]))),
          actions: [
            TextButton(
                onPressed: () => Navigator.pop(context), child: Text(tr('取消'))),
            FilledButton(
                key: const Key('confirmInstanceRestore'),
                onPressed: valid
                    ? () => Navigator.pop(context,
                        {'id': id.text.trim(), 'name': name.text.trim()})
                    : null,
                child: Text(tr('恢复副本')))
          ]);
}
