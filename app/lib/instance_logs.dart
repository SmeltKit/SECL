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
  bool follow = false;
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
  void dispose() {
    timer?.cancel();
    search.dispose();
    super.dispose();
  }

  Future<void> load() => perform(() async {
        final result = dataRows(
            await rpc({'op': 'instance.logs', 'instance': widget.instance}));
        if (mounted) {
          setState(() {
            logs = result;
            if (selected >= logs.length) selected = 0;
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
                  onSelected: (_) => setState(() => selected = i)),
            FilterChip(
                label: Text(tr('自动刷新')),
                selected: follow,
                onSelected: (v) => setState(() => follow = v)),
            OutlinedButton.icon(
                onPressed: locked ? null : load,
                icon: const Icon(Icons.refresh_rounded, size: 17),
                label: Text(tr('刷新'))),
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
