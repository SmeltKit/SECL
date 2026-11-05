// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';
import 'install_workflow.dart';

class ResourceUpdates extends StatefulWidget {
  const ResourceUpdates(
      {super.key,
      required this.engine,
      required this.instance,
      required this.kind,
      required this.onBack});
  final ResourceEngine engine;
  final Data instance;
  final String kind;
  final VoidCallback onBack;
  @override
  State<ResourceUpdates> createState() => _ResourceUpdatesState();
}

class _ResourceUpdatesState extends State<ResourceUpdates>
    with ManagedState<ResourceUpdates> {
  @override
  ResourceEngine get engine => widget.engine;
  String channel = 'release';
  Data? report, outcome;
  final selected = <String>{};
  bool reviewing = false;
  @override
  bool get locked => super.locked || reviewing;
  List<Data> get rows => dataRows(report?['items']);
  @override
  void initState() {
    super.initState();
    check();
  }

  Future<void> check() => perform(() async {
        setState(() {
          report = null;
          outcome = null;
          selected.clear();
        });
        final value = Map<String, dynamic>.from(await engine.run({
          'op': 'updates.check',
          'instance': widget.instance,
          'kind': widget.kind,
          'channel': channel
        }, title: '正在检查资源更新') as Map);
        if (!mounted) return;
        setState(() {
          report = value;
          selected.addAll(dataRows(value['items'])
              .where((r) => r['status'] == 'update')
              .map((r) => r['filename'] as String)
              .take(128));
        });
      });

  Future<void> install() => perform(() async {
        final plan = Map<String, dynamic>.from(await engine.run({
          'op': 'updates.plan',
          'report': report,
          'selected': selected.toList()
        }, title: '正在检查依赖与冲突') as Map);
        if (!mounted) return;
        setState(() {
          loading = false;
          reviewing = true;
        });
        try {
          await reviewAndInstall(context, engine, plan,
              kind: widget.kind,
              onApplying: () {
                if (mounted) setState(() => loading = true);
              },
              applyOperation: 'updates.apply',
              onResult: (value) {
                if (mounted) {
                  setState(() {
                    outcome = value;
                    report = null;
                    selected.clear();
                  });
                }
              });
        } finally {
          if (mounted) setState(() => reviewing = false);
        }
      });

  String statusLabel(Data row) => tr({
        'update': '可更新',
        'current': '暂无更新',
        'unknown': '无法确认',
        'failed': '查询失败',
        'no_candidate': '没有匹配版本',
        'incompatible': '环境不匹配',
        'duplicate': '重复副本',
        'unsupported': '需手动处理'
      }[row['status']] ??
      '无法确认');

  @override
  Widget build(BuildContext context) {
    final updates = rows.where((r) => r['status'] == 'update').toList();
    final all = updates.isNotEmpty &&
        updates.every((r) => selected.contains(r['filename']));
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Wrap(
          spacing: 12,
          runSpacing: 10,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            TextButton.icon(
                onPressed: locked ? null : widget.onBack,
                icon: const Icon(Icons.arrow_back_rounded, size: 18),
                label: Text(tr('返回资源列表'))),
            Text(tr('资源更新'),
                style:
                    const TextStyle(fontSize: 18, fontWeight: FontWeight.w600)),
            SizedBox(
                width: 190,
                child: DropdownButtonFormField<String>(
                    key: const Key('updateChannel'),
                    value: channel,
                    isExpanded: true,
                    items: [
                      DropdownMenuItem(
                          value: 'release', child: Text(tr('仅正式版'))),
                      DropdownMenuItem(value: 'beta', child: Text(tr('包含测试版'))),
                      DropdownMenuItem(value: 'alpha', child: Text(tr('包含预览版')))
                    ],
                    onChanged: locked
                        ? null
                        : (v) {
                            setState(() => channel = v ?? 'release');
                            check();
                          })),
            OutlinedButton.icon(
                onPressed: locked ? null : check,
                icon: const Icon(Icons.refresh_rounded, size: 18),
                label: Text(tr('重新检查')))
          ]),
      const SizedBox(height: 12),
      Text(tr('按文件哈希识别来源，并筛选适配当前游戏版本和加载器的新版。网站标签及静态检查无法保证所有模组组合兼容。'),
          style: TextStyle(color: muted, fontSize: 12, height: 1.6)),
      const SizedBox(height: 14),
      Expanded(
          child: result(outcome != null
              ? successView()
              : report == null
                  ? const SizedBox.shrink()
                  : rows.isEmpty
                      ? Center(child: Text(tr('尚未添加资源，从本地导入或浏览资源市场。')))
                      : SingleChildScrollView(
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              children: [
                              Text(
                                  tr('发现 {0} 项更新，另有 {1} 项无更新或需要确认。', [
                                    updates.length,
                                    rows.length - updates.length
                                  ]),
                                  style: TextStyle(color: muted, fontSize: 12)),
                              const SizedBox(height: 12),
                              ManagementTable(headers: [
                                AppCheck(
                                    value: all,
                                    onChanged: locked
                                        ? null
                                        : (v) => setState(() {
                                              selected.clear();
                                              if (v == true) {
                                                selected.addAll(updates
                                                    .map((r) =>
                                                        r['filename'] as String)
                                                    .take(128));
                                              }
                                            })),
                                Text(tr('名称')),
                                Text(tr('当前 → 新版本')),
                                Text(tr('状态'))
                              ], widths: const {
                                0: FixedColumnWidth(48),
                                1: FlexColumnWidth(3),
                                2: FlexColumnWidth(2),
                                3: FlexColumnWidth(1.6)
                              }, rows: [
                                for (final row in rows)
                                  [
                                    AppCheck(
                                        value:
                                            selected.contains(row['filename']),
                                        onChanged: locked ||
                                                row['status'] != 'update'
                                            ? null
                                            : (v) => setState(() {
                                                  if (v == true &&
                                                      selected.length < 128) {
                                                    selected.add(row['filename']
                                                        as String);
                                                  } else {
                                                    selected.remove(
                                                        row['filename']);
                                                  }
                                                })),
                                    Column(
                                        crossAxisAlignment:
                                            CrossAxisAlignment.start,
                                        children: [
                                          Text(
                                              row['name'] as String? ??
                                                  row['filename'] as String,
                                              style: const TextStyle(
                                                  fontWeight: FontWeight.w600)),
                                          const SizedBox(height: 5),
                                          Text(row['filename'] as String,
                                              style: TextStyle(
                                                  color: muted, fontSize: 11)),
                                          if (row['enabled'] == false) ...[
                                            const SizedBox(height: 5),
                                            Text(tr('更新后保持禁用'),
                                                style: TextStyle(
                                                    color: muted, fontSize: 11))
                                          ]
                                        ]),
                                    Text(
                                        '${row['current_version'] ?? row['version'] ?? '—'}${row['candidate'] == null ? '' : ' → ${(row['candidate'] as Map)['version']}'}'),
                                    Column(
                                        crossAxisAlignment:
                                            CrossAxisAlignment.start,
                                        children: [
                                          Text(statusLabel(row),
                                              style: TextStyle(
                                                  color:
                                                      row['status'] == 'update'
                                                          ? accent
                                                          : muted)),
                                          if (row['status'] != 'update' &&
                                              row['status'] != 'current') ...[
                                            const SizedBox(height: 5),
                                            Text(
                                                nativeText(
                                                    row['message'] as String? ??
                                                        ''),
                                                style: TextStyle(
                                                    color: muted,
                                                    fontSize: 11,
                                                    height: 1.5)),
                                            if (row['error'] is Map)
                                              Text(
                                                  '${(row['error'] as Map)['code'] ?? ''}${((row['error'] as Map)['http_status'] as num? ?? 0) <= 0 ? '' : ' · HTTP ${(row['error'] as Map)['http_status']}'}',
                                                  style: TextStyle(
                                                      color: muted,
                                                      fontSize: 11))
                                          ]
                                        ])
                                  ]
                              ])
                            ])))),
      const SizedBox(height: 12),
      Wrap(
          alignment: WrapAlignment.end,
          spacing: 12,
          runSpacing: 10,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            if (loading)
              TextButton(onPressed: engine.cancel, child: Text(tr('取消'))),
            if (report != null)
              Text(tr('已选择 {0} 项（最多 128 项）', [selected.length]),
                  style: TextStyle(color: muted, fontSize: 12)),
            FilledButton.icon(
                key: const Key('reviewUpdates'),
                onPressed: locked || selected.isEmpty ? null : install,
                icon: const Icon(Icons.fact_check_outlined, size: 18),
                label: Text(tr('查看更新方案')))
          ])
    ]);
  }

  Widget successView() => SingleChildScrollView(
          child: SurfaceCard(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
            Row(children: [
              Icon(Icons.check_circle_outline, color: accent),
              const SizedBox(width: 10),
              Text(tr('更新已完成'),
                  style: const TextStyle(
                      fontSize: 17, fontWeight: FontWeight.w600))
            ]),
            const SizedBox(height: 12),
            for (final name in outcome!['installed'] as List? ?? [])
              Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Text('✓ $name')),
            for (final warning in outcome!['warnings'] as List? ?? [])
              Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: Text(nativeText(warning.toString()))),
            Text(tr('请启动游戏验证。未发现静态冲突不代表所有组合都兼容。'),
                style: TextStyle(color: muted, fontSize: 12, height: 1.6))
          ])));
}
