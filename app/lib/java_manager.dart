import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

class JavaManager extends StatefulWidget {
  const JavaManager(
      {super.key,
      required this.engine,
      required this.onDownload,
      required this.onChanged});
  final ResourceEngine engine;
  final VoidCallback onDownload, onChanged;
  @override
  State<JavaManager> createState() => _JavaManagerState();
}

class _JavaManagerState extends State<JavaManager>
    with ManagedState<JavaManager> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> runtimes = [];
  @override
  void initState() {
    super.initState();
    load();
  }

  Future<void> read() async {
    final response = dataRows(await rpc({'op': 'java.catalog'}));
    if (mounted) setState(() => runtimes = response);
  }

  Future<void> load() => perform(read);
  Future<void> add() => perform(() async {
        final file = await openFile(confirmButtonText: tr('选择 Java 可执行文件'));
        if (file == null) return;
        final runtime = await rpc({'op': 'java.probe', 'path': file.path});
        if (runtime == null) {
          throw EngineFailure(
              {'code': 'java_unavailable', 'message': tr('所选 Java 无法运行。')});
        }
        await rpc({
          'op': 'java.catalog',
          'paths': [file.path]
        });
        await read();
        widget.onChanged();
      });
  Future<void> scan(bool deep) => perform(() async {
        List<String> roots;
        if (deep) {
          roots = (await rpc({'op': 'java.roots'}) as List).cast<String>();
        } else {
          roots = runtimes.map((j) => j['path'] as String).toList();
          final platform = await rpc({'op': 'platform'}) as Map;
          roots.addAll(platform['os'] == 'osx'
              ? [
                  '/Library/Java/JavaVirtualMachines',
                  '/opt/homebrew/opt',
                  '/usr/local/opt'
                ]
              : platform['os'] == 'windows'
                  ? [
                      'C:/Program Files/Java',
                      'C:/Program Files/Microsoft',
                      'C:/Program Files/Eclipse Adoptium'
                    ]
                  : ['/usr/lib/jvm', '/opt/java']);
        }
        final scanned = await rpc({
          'op': 'java.scan',
          'roots': roots,
          'deep': deep,
          'depth': deep ? 32 : 10,
          'budget': deep ? 1000000 : 60000
        }) as Map;
        await rpc({
          'op': 'java.catalog',
          'paths': [for (final j in dataRows(scanned['runtimes'])) j['path']]
        });
        await read();
        widget.onChanged();
      });
  Future<void> legal(Data runtime) => perform(() async {
        final data =
            await rpc({'op': 'java.legal', 'path': runtime['path']}) as Map;
        if (!mounted) return;
        final paths = <String>[
          ...(data['licenses'] as List? ?? []).cast<String>(),
          if ((data['legal_directory'] as String? ?? '').isNotEmpty)
            data['legal_directory'] as String
        ];
        await showDialog<void>(
            context: context,
            builder: (context) => AlertDialog(
                    title: Text(tr('许可证与第三方声明')),
                    content: SizedBox(
                        width: 490,
                        child: SingleChildScrollView(
                            child: Column(
                                crossAxisAlignment: CrossAxisAlignment.start,
                                children: [
                              SelectableText(data['home'] as String),
                              const SizedBox(height: 16),
                              if (paths.isEmpty)
                                Text(tr('未找到单独的许可证文件，请查看运行时目录。')),
                              for (final path in paths)
                                TextButton.icon(
                                    onPressed: () async {
                                      await engine.run(
                                          {'op': 'path.open', 'path': path});
                                    },
                                    icon: const Icon(Icons.open_in_new_rounded,
                                        size: 16),
                                    label:
                                        Text(path.split(RegExp(r'[/\\]')).last))
                            ]))),
                    actions: [
                      TextButton(
                          onPressed: () => Navigator.pop(context),
                          child: Text(tr('完成')))
                    ]));
      });
  @override
  Widget build(BuildContext context) => ManagementPage(
      title: tr('Java 管理'),
      subtitle: tr('管理本机运行环境；每个实例独立选择 Java。'),
      actions: [
        FilledButton.icon(
            onPressed: locked ? null : widget.onDownload,
            icon: const Icon(Icons.download_rounded, size: 18),
            label: Text(tr('安装 Java')))
      ],
      body: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Wrap(spacing: 10, runSpacing: 10, children: [
          PopupMenuButton<bool>(
              enabled: !locked,
              onSelected: scan,
              itemBuilder: (_) => [
                    PopupMenuItem(value: false, child: Text(tr('快速检测常见目录'))),
                    PopupMenuItem(value: true, child: Text(tr('全盘扫描可访问的磁盘')))
                  ],
              child: IgnorePointer(
                  child: OutlinedButton.icon(
                      onPressed: () {},
                      icon: const Icon(Icons.manage_search_rounded, size: 18),
                      label: Text(tr('检测本机 Java'))))),
          OutlinedButton.icon(
              onPressed: locked ? null : add,
              icon: const Icon(Icons.add_rounded, size: 18),
              label: Text(tr('添加本地 Java'))),
          IconButton(
              tooltip: tr('刷新'),
              onPressed: locked ? null : load,
              icon: const Icon(Icons.refresh_rounded))
        ]),
        const SizedBox(height: 18),
        Expanded(
            child: result(runtimes.isEmpty
                ? Center(child: Text(tr('未检测到 Java，可扫描、导入或下载安装。')))
                : SingleChildScrollView(
                    child: ManagementTable(headers: [
                    Text(tr('版本 / 构建')),
                    Text(tr('发行版')),
                    Text(tr('架构')),
                    Text(tr('状态')),
                    Text(tr('操作'))
                  ], widths: const {
                    0: FlexColumnWidth(2),
                    1: FlexColumnWidth(1.5),
                    2: FlexColumnWidth(1),
                    3: FlexColumnWidth(1),
                    4: FixedColumnWidth(70)
                  }, rows: [
                    for (final j in runtimes)
                      [
                        Tooltip(
                            message: j['path'] as String,
                            child: Text(j['version'] as String)),
                        Text(j['vendor'] as String? ?? '',
                            maxLines: 2, overflow: TextOverflow.ellipsis),
                        Text(j['arch'] as String? ?? ''),
                        Text('● ${tr('可用')}',
                            style: TextStyle(color: seclColor(0xFF2F7C55))),
                        PopupMenuButton<String>(
                            tooltip: tr('更多操作'),
                            icon: const Icon(Icons.more_horiz_rounded),
                            onSelected: (action) {
                              if (action == 'legal') legal(j);
                              if (action == 'open') {
                                perform(() async {
                                  final data = await rpc({
                                    'op': 'java.legal',
                                    'path': j['path']
                                  }) as Map;
                                  await rpc({
                                    'op': 'path.open',
                                    'path': data['home']
                                  });
                                });
                              }
                            },
                            itemBuilder: (_) => [
                                  PopupMenuItem(
                                      value: 'open', child: Text(tr('打开位置'))),
                                  PopupMenuItem(
                                      value: 'legal', child: Text(tr('查看许可证')))
                                ])
                      ]
                  ])))),
        if (loading)
          Padding(
              padding: const EdgeInsets.symmetric(vertical: 14),
              child: Row(children: [
                Expanded(
                    child: Text(
                        nativeText(engine.progress['message'] as String? ??
                            tr('正在扫描可访问目录')),
                        style: TextStyle(fontSize: 12, color: muted))),
                TextButton(onPressed: engine.cancel, child: Text(tr('取消')))
              ]))
      ]));
}
