// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

class InstanceLogs extends StatefulWidget {
  const InstanceLogs({super.key, required this.engine, required this.instance});
  final ResourceEngine engine;
  final Data instance;
  @override
  State<InstanceLogs> createState() => _InstanceLogsState();
}

class _InstanceLogsState extends State<InstanceLogs>
    with ManagedState<InstanceLogs> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> logs = [];
  int selected = 0;
  bool follow = false, analyzed = false;
  Timer? timer;
  final search = TextEditingController();
  @override
  void initState() {
    super.initState();
    load();
    timer = Timer.periodic(const Duration(seconds: 3), (_) {
      if (follow && !locked) load();
    });
  }

  @override
  void didUpdateWidget(InstanceLogs oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.instance['game_directory'] !=
        widget.instance['game_directory']) {
      logs = [];
      selected = 0;
      analyzed = false;
      search.clear();
      load();
    }
  }

  @override
  void dispose() {
    timer?.cancel();
    search.dispose();
    super.dispose();
  }

  Future<void> load() => perform(() async {
        final directory = widget.instance['game_directory'];
        final oldName = logs.isEmpty ? '' : logs[selected]['name'];
        final oldText = text;
        final result = dataRows(
            await rpc({'op': 'instance.logs', 'instance': widget.instance}));
        if (mounted && directory == widget.instance['game_directory']) {
          setState(() {
            logs = result;
            final same = logs.indexWhere((row) => row['name'] == oldName);
            selected = same < 0 ? 0 : same;
            if (logs.isEmpty ||
                logs[selected]['name'] != oldName ||
                text != oldText) analyzed = false;
          });
        }
      });
  String get text => logs.isEmpty ? '' : logs[selected]['text'] as String;
  @override
  Widget build(BuildContext context) {
    final query = search.text.toLowerCase();
    final shown = query.isEmpty
        ? text
        : text
            .split('\n')
            .where((line) => line.toLowerCase().contains(query))
            .join('\n');
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Wrap(
          spacing: 10,
          runSpacing: 10,
          crossAxisAlignment: WrapCrossAlignment.center,
          children: [
            for (var i = 0; i < logs.length; i++)
              ChoiceChip(
                  label: Text(logs[i]['name'] as String),
                  selected: selected == i,
                  onSelected: (_) => setState(() {
                        if (selected != i) analyzed = false;
                        selected = i;
                      })),
            FilterChip(
                label: Text(tr('自动刷新')),
                selected: follow,
                onSelected: (v) => setState(() => follow = v)),
            OutlinedButton.icon(
                onPressed: locked ? null : load,
                icon: const Icon(Icons.refresh_rounded, size: 17),
                label: Text(tr('刷新'))),
            OutlinedButton.icon(
                key: const Key('analyzeInstanceLog'),
                onPressed: locked || text.isEmpty
                    ? null
                    : () => setState(() => analyzed = true),
                icon: const Icon(Icons.manage_search_rounded, size: 18),
                label: Text(tr('分析日志'))),
            OutlinedButton.icon(
                onPressed: () => Clipboard.setData(ClipboardData(text: shown)),
                icon: const Icon(Icons.copy_outlined, size: 17),
                label: Text(tr('复制日志'))),
            TextButton.icon(
                onPressed: locked
                    ? null
                    : () => perform(() async {
                          await rpc({
                            'op': 'path.open',
                            'path': '${widget.instance['game_directory']}/logs'
                          });
                        }),
                icon: const Icon(Icons.folder_open_outlined, size: 17),
                label: Text(tr('打开目录')))
          ]),
      const SizedBox(height: 12),
      if (analyzed && logs.isNotEmpty) ...[
        LogDiagnosisPanel(
            key: ValueKey('${logs[selected]['name']}-${text.hashCode}'),
            diagnosis: Map<String, dynamic>.from(
                logs[selected]['diagnosis'] as Map? ?? const {}),
            truncated: logs[selected]['truncated'] == true),
        const SizedBox(height: 12)
      ],
      TextField(
          controller: search,
          decoration: InputDecoration(
              hintText: tr('筛选日志内容'),
              prefixIcon: const Icon(Icons.search_rounded)),
          onChanged: (_) => setState(() {})),
      const SizedBox(height: 12),
      Expanded(
          child: result(SurfaceCard(
              child: logs.isEmpty
                  ? Center(child: Text(tr('尚无游戏日志，启动游戏后可在这里查看。')))
                  : SingleChildScrollView(
                      child: SelectableText(shown,
                          key: const Key('originalInstanceLog'),
                          style: const TextStyle(
                              fontFamily: 'monospace',
                              fontSize: 12,
                              height: 1.65)))))),
      const SizedBox(height: 10),
      Text(tr('显示最近的日志内容；认证令牌已脱敏。'),
          style: TextStyle(color: muted, fontSize: 12)),
      const SizedBox(height: 12)
    ]);
  }
}

class LogDiagnosisPanel extends StatelessWidget {
  const LogDiagnosisPanel(
      {super.key, required this.diagnosis, this.truncated = false});
  final Data diagnosis;
  final bool truncated;
  @override
  Widget build(BuildContext context) {
    final findings = dataRows(diagnosis['findings']);
    return DecoratedBox(
        decoration: BoxDecoration(
            color: Theme.of(context).colorScheme.primary.withOpacity(.05),
            borderRadius: BorderRadius.circular(12),
            border: Border.all(color: line)),
        child: ExpansionTile(
            key: const Key('logDiagnosisExpansion'),
            initiallyExpanded: true,
            shape: const Border(),
            collapsedShape: const Border(),
            title: Text(tr('日志分析')),
            subtitle: Text(
                findings.isEmpty
                    ? tr('未匹配到已知线索')
                    : tr('发现 {0} 类线索', [findings.length]),
                style: TextStyle(color: muted, fontSize: 12)),
            leading: const Icon(Icons.manage_search_rounded),
            children: [
              ConstrainedBox(
                  constraints: BoxConstraints(
                      maxHeight: MediaQuery.sizeOf(context).height * .3),
                  child: SingleChildScrollView(
                      padding: const EdgeInsets.fromLTRB(16, 0, 16, 14),
                      child: Column(
                          crossAxisAlignment: CrossAxisAlignment.stretch,
                          children: [
                            Text(tr('仅分析已脱敏的日志片段。旧日志不一定属于本次启动，线索不代表已确认的崩溃原因。'),
                                style: TextStyle(
                                    color: muted, fontSize: 12, height: 1.55)),
                            if (truncated || diagnosis['truncated'] == true)
                              Padding(
                                  padding: const EdgeInsets.only(top: 8),
                                  child: Text(
                                      tr('日志较长，仅分析末尾片段；行号相对于当前显示的原始日志。'),
                                      style: TextStyle(
                                          color: muted,
                                          fontSize: 12,
                                          height: 1.55))),
                            if (findings.isEmpty)
                              Padding(
                                  padding: const EdgeInsets.only(top: 12),
                                  child: Text(
                                      tr('暂时无法判断。请查看完整日志和首个异常，或提供日志寻求帮助。'),
                                      style: const TextStyle(height: 1.55))),
                            for (final finding in findings)
                              _LogFinding(finding: finding)
                          ])))
            ]));
  }
}

class _LogFinding extends StatelessWidget {
  const _LogFinding({required this.finding});
  final Data finding;
  @override
  Widget build(BuildContext context) {
    final error = finding['severity'] == 'error';
    return Padding(
        padding: const EdgeInsets.only(top: 14),
        child:
            Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Icon(
                error
                    ? Icons.error_outline_rounded
                    : Icons.info_outline_rounded,
                size: 18,
                color: error ? const Color(0xffb0643f) : muted),
            const SizedBox(width: 8),
            Expanded(
                child: Text(tr(finding['title'] as String? ?? ''),
                    style: const TextStyle(
                        fontWeight: FontWeight.w600, height: 1.4)))
          ]),
          const SizedBox(height: 5),
          Text('${tr(error ? '错误线索' : '需核实')} · ${finding['code']}',
              style: TextStyle(fontSize: 11, color: muted)),
          const SizedBox(height: 6),
          SelectableText(tr(finding['suggestion'] as String? ?? ''),
              style: const TextStyle(fontSize: 13, height: 1.55)),
          for (final evidence in dataRows(finding['evidence']))
            Padding(
                padding: const EdgeInsets.only(top: 8),
                child: DecoratedBox(
                    decoration: BoxDecoration(
                        color: Theme.of(context).colorScheme.surface,
                        borderRadius: BorderRadius.circular(6)),
                    child: Padding(
                        padding: const EdgeInsets.all(10),
                        child: SelectableText(
                            '${tr('第 {0} 行', [
                                  evidence['line']
                                ])}\n${evidence['text']}',
                            style: const TextStyle(
                                fontFamily: 'monospace',
                                fontSize: 12,
                                height: 1.5)))))
        ]));
  }
}
