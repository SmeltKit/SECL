// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'resource_engine.dart';
import 'localization.dart';
import 'app_widgets.dart';

class TaskPanel extends StatelessWidget {
  const TaskPanel({super.key, required this.engine});
  final ResourceEngine engine;
  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation: engine,
      builder: (_, __) => AlertDialog(
              title: Text(tr('下载任务')),
              content: SizedBox(
                  width: 520,
                  height: 340,
                  child: engine.history.isEmpty
                      ? Center(child: Text(tr('还没有下载任务')))
                      : ListView.separated(
                          itemCount: engine.history.length,
                          separatorBuilder: (_, __) =>
                              const Divider(height: 20),
                          itemBuilder: (_, i) {
                            final t = engine.history[i];
                            final active = t['status'] == 'running';
                            return Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                                  Row(children: [
                                    Icon(
                                        active
                                            ? Icons.downloading_rounded
                                            : t['status'] == 'complete'
                                                ? Icons
                                                    .check_circle_outline_rounded
                                                : Icons.info_outline_rounded,
                                        size: 20,
                                        color: accent),
                                    const SizedBox(width: 10),
                                    Expanded(
                                        child: Text(
                                            nativeText(t['title'] as String))),
                                    Text(tr({
                                          'running': '进行中',
                                          'complete': '已完成',
                                          'failed': '失败',
                                          'queued': '等待中',
                                          'cancelled': '已取消'
                                        }[t['status']] ??
                                        ''))
                                  ]),
                                  if (active) ...[
                                    const SizedBox(height: 12),
                                    LinearProgressIndicator(
                                        value:
                                            (engine.progress['total'] as num? ??
                                                        0) >
                                                    0
                                                ? (engine.progress['done']
                                                            as num? ??
                                                        0) /
                                                    (engine.progress['total']
                                                        as num)
                                                : null)
                                  ],
                                  if (t['error'] is Map)
                                    Padding(
                                        padding: const EdgeInsets.only(top: 8),
                                        child: Text(
                                            errorMessage(
                                                Map<String, dynamic>.from(
                                                    t['error'] as Map)),
                                            style:
                                                const TextStyle(fontSize: 12)))
                                ]);
                          })),
              actions: [
                if (engine.active)
                  TextButton(
                      onPressed: engine.cancel, child: Text(tr('取消当前任务'))),
                TextButton(
                    onPressed: engine.clearFinished, child: Text(tr('清除已完成'))),
                TextButton(
                    onPressed: () => Navigator.pop(context),
                    child: Text(tr('关闭')))
              ]));
}
