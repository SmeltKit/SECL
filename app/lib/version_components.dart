// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

class VersionComponents extends StatefulWidget {
  const VersionComponents(
      {super.key,
      required this.engine,
      required this.instance,
      required this.javas,
      required this.onChanged});
  final ResourceEngine engine;
  final Data instance;
  final List<Data> javas;
  final VoidCallback onChanged;
  @override
  State<VersionComponents> createState() => _VersionComponentsState();
}

class _VersionComponentsState extends State<VersionComponents>
    with ManagedState<VersionComponents> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> builds = [], loaderBuilds = [];
  Data? selected;
  String channel = 'release',
      loader = '',
      loaderVersion = '',
      localFile = '',
      javaPath = '';
  bool queried = false;
  @override
  void initState() {
    super.initState();
    loader = widget.instance['loader'] as String;
    javaPath = widget.javas.firstOrNull?['path'] as String? ?? '';
  }

  Future<void> query() => perform(() async {
        final response = dataRows(await rpc(
            {'op': 'optifine.builds', 'game': widget.instance['version']}));
        if (mounted) {
          setState(() {
            builds = response;
            queried = true;
            selected =
                response.where((b) => b['preview'] != true).firstOrNull ??
                    response.firstOrNull;
            localFile = '';
          });
        }
      });
  Future<void> queryLoader() => perform(() async {
        final response = dataRows(await rpc({
          'op': 'loader.versions',
          'loader': loader,
          'game': widget.instance['version']
        }));
        if (mounted) {
          setState(() {
            loaderBuilds = response
                .map((r) => Map<String, dynamic>.from(r['loader'] as Map? ?? r))
                .toList();
            loaderVersion =
                loaderBuilds.firstOrNull?['version'] as String? ?? '';
          });
        }
      });
  Future<void> importInstaller() => perform(() async {
        final file = await openFile(acceptedTypeGroups: [
          const XTypeGroup(label: 'OptiFine JAR', extensions: ['jar'])
        ]);
        if (file == null) return;
        final info = Map<String, dynamic>.from(
            await rpc({'op': 'optifine.inspect', 'path': file.path}) as Map);
        final known = builds
            .where((b) =>
                b['version'] == info['version'] && b['game'] == info['game'])
            .firstOrNull;
        if (mounted) {
          setState(() {
            localFile = file.path;
            selected = {...?known, ...info};
          });
        }
      });
  Future<void> install({bool remove = false}) async {
    if (!remove && selected == null) return;
    if (!await confirmOperation(
        context,
        tr('确认版本组件变更'),
        remove
            ? tr('移除 OptiFine 并恢复安装前的版本配置，保留存档和设置。')
            : tr('将按所选构建校验游戏、Forge 版本和本地冲突组件，再安装 OptiFine。'))) return;
    await perform(() async {
      if (!remove) {
        final result = await rpc({
          'op': 'optifine.compatibility',
          'build': selected,
          'instance': widget.instance
        }) as Map;
        if ((result['problem'] as String? ?? '').isNotEmpty) {
          throw EngineFailure(
              {'code': 'incompatible_optifine', 'message': result['problem']});
        }
      }
      await rpc({
        'op': 'optifine.install',
        'request': {
          'root': widget.instance['root'],
          'id': widget.instance['id'],
          'directory': widget.instance['game_directory'],
          'operation': remove ? 'remove' : 'install',
          if (!remove) 'build': selected,
          if (localFile.isNotEmpty) 'local_file': localFile,
          'java': javaPath
        }
      });
      if (mounted) {
        widget.onChanged();
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(tr('版本组件已更新'))));
      }
    });
  }

  Future<void> installLoader() async {
    if (loaderVersion.isEmpty) return;
    if (!await confirmOperation(
        context, tr('确认加载器变更'), tr('更新主加载器后将保留实例目录、存档和模组，并重新校验安装输出。'))) return;
    await perform(() async {
      await rpc({
        'op': 'game.install',
        'request': {
          'root': widget.instance['root'],
          'id': widget.instance['id'],
          'loader': loader,
          'loader_version': loaderVersion,
          if (javaPath.isNotEmpty)
            'java_choice': {'mode': 'fixed', 'path': javaPath}
        }
      });
      widget.onChanged();
    });
  }

  @override
  Widget build(BuildContext context) {
    final installed =
        Map<String, dynamic>.from(widget.instance['optifine'] as Map? ?? {});
    final shown = builds
        .where((b) => (b['preview'] == true ? 'preview' : 'release') == channel)
        .toList();
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Expanded(
          child: result(SingleChildScrollView(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
            SurfaceCard(
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                  Text(tr('已安装组件'),
                      style: const TextStyle(
                          fontSize: 16, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 14),
                  Row(children: [
                    const Icon(Icons.layers_outlined, size: 22),
                    const SizedBox(width: 12),
                    Expanded(
                        child: Text(
                            '${widget.instance['loader']} ${widget.instance['loader_version']}'))
                  ]),
                  if (installed.isNotEmpty) ...[
                    const Divider(height: 28),
                    Row(children: [
                      const Icon(Icons.view_in_ar_outlined, size: 22),
                      const SizedBox(width: 12),
                      Expanded(
                          child:
                              Text('OptiFine ${installed['version'] ?? ''}')),
                      TextButton(
                          onPressed:
                              locked ? null : () => install(remove: true),
                          child: Text(tr('移除')))
                    ])
                  ]
                ])),
            const SizedBox(height: 18),
            SurfaceCard(
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                  Text(tr('主加载器'),
                      style: const TextStyle(
                          fontSize: 16, fontWeight: FontWeight.w600)),
                  const SizedBox(height: 12),
                  Row(children: [
                    Expanded(
                        child: DropdownButtonFormField<String>(
                            value: loader == 'vanilla' ? null : loader,
                            isExpanded: true,
                            hint: Text(tr('选择加载器')),
                            items: [
                              for (final value in [
                                'forge',
                                'neoforge',
                                'fabric',
                                'quilt'
                              ])
                                if (widget.instance['loader'] == 'vanilla' ||
                                    widget.instance['loader'] == value)
                                  DropdownMenuItem(
                                      value: value, child: Text(value))
                            ],
                            onChanged: locked
                                ? null
                                : (v) => setState(() {
                                      loader = v!;
                                      loaderBuilds = [];
                                      loaderVersion = '';
                                    }))),
                    const SizedBox(width: 10),
                    OutlinedButton(
                        onPressed:
                            locked || loader == 'vanilla' ? null : queryLoader,
                        child: Text(tr('查询构建')))
                  ]),
                  if (loaderBuilds.isNotEmpty) ...[
                    const SizedBox(height: 12),
                    DropdownButtonFormField<String>(
                        value: loaderVersion,
                        items: [
                          for (final b in loaderBuilds)
                            DropdownMenuItem(
                                value: b['version'] as String,
                                child: Text(b['version'] as String))
                        ],
                        onChanged: locked
                            ? null
                            : (v) => setState(() => loaderVersion = v!)),
                    const SizedBox(height: 12),
                    Align(
                        alignment: Alignment.centerRight,
                        child: FilledButton(
                            onPressed: locked ? null : installLoader,
                            child: Text(tr('安装 / 更新加载器'))))
                  ],
                  const SizedBox(height: 10),
                  Text(tr('不同主加载器请使用独立实例。已有 OptiFine 时，须先移除再更改加载器。'),
                      style: TextStyle(color: muted, fontSize: 12, height: 1.6))
                ])),
            const SizedBox(height: 18),
            SurfaceCard(
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                  Row(children: [
                    const Expanded(
                        child: Text('OptiFine',
                            style: TextStyle(
                                fontSize: 16, fontWeight: FontWeight.w600))),
                    TextButton.icon(
                        onPressed: locked
                            ? null
                            : () => perform(() async {
                                  await rpc({
                                    'op': 'resource.open',
                                    'url': 'https://optifine.net/downloads'
                                  });
                                }),
                        icon: const Icon(Icons.open_in_new_rounded, size: 16),
                        label: Text(tr('项目官网')))
                  ]),
                  const SizedBox(height: 12),
                  Wrap(spacing: 10, runSpacing: 10, children: [
                    ChoiceChip(
                        label: Text(tr('正式版')),
                        selected: channel == 'release',
                        onSelected: (v) => setState(() => channel = 'release')),
                    ChoiceChip(
                        label: Text(tr('预览版')),
                        selected: channel == 'preview',
                        onSelected: (v) => setState(() => channel = 'preview')),
                    OutlinedButton.icon(
                        onPressed: locked ? null : query,
                        icon: const Icon(Icons.refresh_rounded, size: 17),
                        label: Text(tr('查询构建'))),
                    OutlinedButton.icon(
                        onPressed: locked ? null : importInstaller,
                        icon: const Icon(Icons.folder_open_outlined, size: 17),
                        label: Text(tr('选择本地安装包')))
                  ]),
                  const SizedBox(height: 14),
                  if (!queried && localFile.isEmpty)
                    Padding(
                        padding: const EdgeInsets.all(24),
                        child: Text(tr('查询构建，或选择本地 OptiFine 安装包。'),
                            style: TextStyle(color: muted)))
                  else if (shown.isEmpty && localFile.isEmpty)
                    Padding(
                        padding: const EdgeInsets.all(24),
                        child: Text(tr('没有符合条件的构建'))),
                  if (shown.isNotEmpty)
                    ManagementTable(headers: [
                      Text(tr('选择')),
                      Text(tr('版本')),
                      const Text('Forge')
                    ], widths: const {
                      0: FixedColumnWidth(64)
                    }, rows: [
                      for (final b in shown)
                        [
                          Radio<String>(
                              value: b['version'] as String,
                              groupValue: selected?['version'] as String?,
                              onChanged: locked
                                  ? null
                                  : (v) => setState(() {
                                        selected = b;
                                        localFile = '';
                                      })),
                          Text(b['version'] as String),
                          Text(b['forge'] as String? ?? '—')
                        ]
                    ]),
                  if (localFile.isNotEmpty)
                    Padding(
                        padding: const EdgeInsets.symmetric(vertical: 14),
                        child: Text('$localFile\n${selected?['version']}',
                            style: const TextStyle(fontSize: 12, height: 1.6))),
                  const SizedBox(height: 14),
                  DropdownButtonFormField<String>(
                      value: widget.javas.any((j) => j['path'] == javaPath)
                          ? javaPath
                          : null,
                      isExpanded: true,
                      decoration: InputDecoration(labelText: tr('安装器使用的 Java')),
                      items: [
                        for (final j in widget.javas)
                          DropdownMenuItem(
                              value: j['path'] as String,
                              child: Text(
                                  'Java ${j['version']} · ${j['vendor']}',
                                  overflow: TextOverflow.ellipsis))
                      ],
                      onChanged: locked
                          ? null
                          : (v) => setState(() => javaPath = v ?? '')),
                  const SizedBox(height: 16),
                  Align(
                      alignment: Alignment.centerRight,
                      child: FilledButton.icon(
                          onPressed: locked || selected == null
                              ? null
                              : () => install(),
                          icon: const Icon(Icons.download_rounded, size: 18),
                          label: Text(tr('安装 / 更新 OptiFine'))))
                ]))
          ]))))
    ]);
  }
}
