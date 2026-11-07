// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'resource_engine.dart';
import 'localization.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'launcher_icons.dart';

/// Contents for the shared top-right download popover. The caller owns its
/// route and anchor; this panel stays usable in a small window too.
class TaskPanel extends StatelessWidget {
  const TaskPanel({super.key, required this.engine});
  final ResourceEngine engine;

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation: engine,
      builder: (_, __) => Material(
          color: panelSurface,
          elevation: 12,
          shadowColor: Colors.black.withOpacity(.18),
          borderRadius: BorderRadius.circular(15),
          clipBehavior: Clip.antiAlias,
          child: ConstrainedBox(
              constraints: BoxConstraints(
                  maxWidth: 420,
                  maxHeight: math.max(180,
                      math.min(560, MediaQuery.sizeOf(context).height - 96))),
              child: Column(mainAxisSize: MainAxisSize.min, children: [
                Padding(
                    padding: const EdgeInsets.fromLTRB(20, 12, 8, 10),
                    child: Row(children: [
                      Expanded(
                          child: Text(tr('下载任务'),
                              style: TextStyle(
                                  fontSize: 18,
                                  fontWeight: FontWeight.w600,
                                  color: ink))),
                      if (engine.activeDownloadCount > 0)
                        Text(tr('{0} 项进行中', [engine.activeDownloadCount]),
                            style: TextStyle(fontSize: 12, color: muted)),
                      const SizedBox(width: 8),
                      IconButton(
                          key: const Key('closeDownloadTasks'),
                          tooltip: tr('关闭'),
                          onPressed: () => Navigator.pop(context),
                          icon: const LauncherIcon(LauncherIconKind.close,
                              size: 19))
                    ])),
                Divider(height: 1, color: line),
                Flexible(
                    child: engine.downloadHistory.isEmpty
                        ? Padding(
                            key: const Key('emptyDownloadTasks'),
                            padding: const EdgeInsets.symmetric(
                                horizontal: 28, vertical: 38),
                            child: Column(
                                mainAxisSize: MainAxisSize.min,
                                children: [
                                  LauncherIcon(LauncherIconKind.download,
                                      size: 30, color: muted),
                                  const SizedBox(height: 12),
                                  Text(tr('还没有下载任务'),
                                      style:
                                          TextStyle(fontSize: 14, color: ink)),
                                  const SizedBox(height: 7),
                                  Text(tr('Java、游戏、模组和资源的下载会显示在这里'),
                                      textAlign: TextAlign.center,
                                      style: TextStyle(
                                          fontSize: 12,
                                          height: 1.5,
                                          color: muted)),
                                ]))
                        : ListView.separated(
                            key: const Key('downloadTaskList'),
                            shrinkWrap: true,
                            padding: const EdgeInsets.symmetric(
                                horizontal: 20, vertical: 4),
                            itemCount: engine.downloadHistory.length,
                            separatorBuilder: (_, __) =>
                                Divider(height: 1, color: line),
                            itemBuilder: (_, index) => _DownloadTask(
                                engine.downloadHistory[index],
                                key:
                                    ObjectKey(engine.downloadHistory[index])))),
                Divider(height: 1, color: line),
                Padding(
                    padding: const EdgeInsets.fromLTRB(10, 5, 10, 7),
                    child: Row(children: [
                      TextButton.icon(
                          onPressed: engine.downloadHistory.any((task) =>
                                  task['status'] != 'running' &&
                                  task['status'] != 'queued')
                              ? engine.clearFinished
                              : null,
                          icon: const Icon(Icons.done_all_rounded, size: 17),
                          label: Text(tr('清除已结束'))),
                      const Spacer(),
                      if (engine.currentDownload != null)
                        TextButton(
                            onPressed: engine.cancel,
                            child: Text(tr('取消全部待完成任务')))
                    ]))
              ]))));
}

class _DownloadTask extends StatefulWidget {
  const _DownloadTask(this.task, {super.key});
  final Map<String, dynamic> task;
  @override
  State<_DownloadTask> createState() => _DownloadTaskState();
}

class _DownloadTaskState extends State<_DownloadTask> {
  bool expanded = false;
  @override
  Widget build(BuildContext context) {
    final task = widget.task;
    final active = task['status'] == 'running';
    final progress = Map<String, dynamic>.from(task['progress'] as Map? ?? {});
    final message = progress['message']?.toString() ?? '';
    final total = progress['total'] as num? ?? -1;
    final done = progress['done'] as num? ?? -1;
    final ratio =
        total > 0 && done >= 0 ? (done / total).clamp(0.0, 1.0) : null;
    final kind = task['kind'] as String? ?? '';
    final category = tr({
          'java': 'Java 运行环境',
          'game': '游戏',
          'mod': '模组',
          'shader': '光影包',
          'resourcepack': '材质包',
          'component': '版本组件',
          'modpack': '整合包'
        }[kind] ??
        '资源');
    final status = tr({
          'running': '进行中',
          'complete': '已完成',
          'failed': '失败',
          'queued': '等待中',
          'cancelled': '已取消'
        }[task['status']] ??
        '');
    final statusColor = task['status'] == 'failed'
        ? seclColor(0xffb46544)
        : task['status'] == 'complete'
            ? seclColor(0xff278560)
            : muted;
    final iconKind = {
      'java': LauncherIconKind.java,
      'game': LauncherIconKind.home,
      'shader': LauncherIconKind.shader,
      'resourcepack': LauncherIconKind.resourcepack,
      'mod': LauncherIconKind.mod,
      'modpack': LauncherIconKind.library,
    }[kind];
    final items = (task['items'] as List? ?? []).whereType<String>().toList();
    final target = task['target']?.toString() ?? '';
    final subtitle = task['subtitle']?.toString() ?? '';
    final error = task['error'] is Map
        ? Map<String, dynamic>.from(task['error'] as Map)
        : null;
    return Padding(
        padding: const EdgeInsets.symmetric(vertical: 16),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Container(
              width: 38,
              height: 38,
              decoration: BoxDecoration(
                  color: accent.withOpacity(.08),
                  borderRadius: BorderRadius.circular(10)),
              child: iconKind == null
                  ? Icon(
                      kind == 'component'
                          ? Icons.tune_rounded
                          : Icons.download_outlined,
                      size: 21,
                      color: accent)
                  : LauncherIcon(iconKind, size: 21, color: accent)),
          const SizedBox(width: 12),
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(nativeText(task['title']?.toString() ?? ''),
                    style: TextStyle(
                        fontSize: 14,
                        height: 1.4,
                        fontWeight: FontWeight.w600,
                        color: ink)),
                const SizedBox(height: 4),
                Text(
                    [
                      category,
                      if (subtitle.isNotEmpty) subtitle,
                      if (target.isNotEmpty) tr('安装到 {0}', [target])
                    ].join(' · '),
                    style: TextStyle(fontSize: 11, height: 1.5, color: muted)),
                if (active) ...[
                  const SizedBox(height: 11),
                  ClipRRect(
                      borderRadius: BorderRadius.circular(3),
                      child: LinearProgressIndicator(
                          value: ratio?.toDouble(),
                          minHeight: 4,
                          backgroundColor: accent.withOpacity(.10))),
                  const SizedBox(height: 7),
                  Text(message.isEmpty ? tr('准备下载与校验') : nativeText(message),
                      style:
                          TextStyle(fontSize: 11, height: 1.4, color: muted)),
                  if (ratio != null) ...[
                    const SizedBox(height: 3),
                    Text(
                        '${message == '下载文件' ? '${byteLabel(done)} / ${byteLabel(total)}' : '$done / $total'} · ${(ratio * 100).round()}%',
                        style: TextStyle(fontSize: 11, color: muted)),
                  ]
                ],
                if (items.length > 1) ...[
                  const SizedBox(height: 5),
                  InkWell(
                      onTap: () => setState(() => expanded = !expanded),
                      child: Padding(
                          padding: const EdgeInsets.symmetric(vertical: 5),
                          child: Row(children: [
                            Icon(
                                expanded
                                    ? Icons.expand_less_rounded
                                    : Icons.expand_more_rounded,
                                size: 15,
                                color: muted),
                            const SizedBox(width: 3),
                            Text(tr('包含 {0} 个文件', [items.length]),
                                style: TextStyle(fontSize: 11, color: muted))
                          ]))),
                  if (expanded)
                    for (final item in items)
                      Padding(
                          padding: const EdgeInsets.only(top: 3),
                          child: Text(item,
                              style: TextStyle(
                                  fontSize: 11, height: 1.5, color: muted))),
                ],
                if (error != null) ...[
                  const SizedBox(height: 9),
                  Text(errorMessage(error),
                      style: TextStyle(
                          fontSize: 11, height: 1.5, color: statusColor)),
                  const SizedBox(height: 4),
                  SelectableText(
                      '${error['code'] ?? ''}${(error['http_status'] as num? ?? 0) > 0 ? ' · HTTP ${error['http_status']}' : ''}',
                      style:
                          TextStyle(fontSize: 10, height: 1.5, color: muted)),
                ],
              ])),
          const SizedBox(width: 10),
          Padding(
              padding: const EdgeInsets.only(top: 2),
              child: active
                  ? Text(ratio == null ? status : '${(ratio * 100).round()}%',
                      style: TextStyle(fontSize: 11, color: accent))
                  : Text(status,
                      style: TextStyle(fontSize: 11, color: statusColor)))
        ]));
  }
}
