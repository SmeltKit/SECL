// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:io';
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

Future<void> showModpackImport(BuildContext context, ResourceEngine engine,
    {required String root,
    required Future<void> Function(Data) onInstalled}) async {
  final file = await openFile(acceptedTypeGroups: [
    XTypeGroup(
        label: tr('整合包（.mrpack、.zip）'), extensions: const ['mrpack', 'zip'])
  ], confirmButtonText: tr('导入整合包'));
  if (file == null || !context.mounted) return;
  final result = await showDialog<Data>(
      context: context,
      barrierDismissible: false,
      builder: (_) =>
          ModpackImportDialog(engine: engine, path: file.path, root: root));
  if (result != null) await onInstalled(result);
}

class ModpackImportDialog extends StatefulWidget {
  const ModpackImportDialog(
      {super.key,
      required this.engine,
      required this.path,
      required this.root});
  final ResourceEngine engine;
  final String path, root;
  @override
  State<ModpackImportDialog> createState() => _ModpackImportDialogState();
}

class _ModpackImportDialogState extends State<ModpackImportDialog> {
  final name = TextEditingController(), root = TextEditingController();
  final selectedOptional = <String>{};
  late final String instanceId;
  Data? pack, plan, problem;
  bool working = false, applying = false, cancelling = false;
  String? notice;
  String? supplementDirectory;
  Future<void> Function()? retry;
  List<Data> get blockedFiles => dataRows(plan?['blocked_files']);

  @override
  void initState() {
    super.initState();
    root.text = widget.root;
    instanceId = 'pack-${DateTime.now().microsecondsSinceEpoch}';
    widget.engine.addListener(changed);
    unawaited(inspect());
  }

  void changed() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    widget.engine.removeListener(changed);
    name.dispose();
    root.dispose();
    super.dispose();
  }

  Future<void> perform(Future<void> Function() work,
      {bool install = false}) async {
    if (working) return;
    setState(() {
      working = true;
      applying = install;
      cancelling = false;
      problem = null;
      notice = null;
      retry = () => perform(work, install: install);
    });
    try {
      await work();
    } on EngineFailure catch (e) {
      if (!mounted) return;
      if (cancelling && !install) {
        Navigator.pop(context);
      } else if (e.details['code'] == 'cancelled') {
        setState(() => notice = tr('操作已取消，可重新确认并继续导入'));
      } else {
        setState(() {
          problem = e.details;
          if (e.details['code'] == 'invalid_supplement') {
            supplementDirectory = null;
          }
          retry = switch (e.details['code']) {
            'curseforge_key_missing' || 'curseforge_key_invalid' => null,
            'modpack_files_blocked' || 'invalid_supplement' => prepare,
            'plan_stale' => install ? prepare : inspect,
            _ => retry,
          };
        });
      }
    } catch (_) {
      if (mounted) {
        setState(() => problem = {
              'code': 'invalid_modpack_result',
              'message': tr('无法读取整合包结果，请重试。')
            });
      }
    } finally {
      if (mounted) {
        setState(() {
          working = false;
          applying = false;
          cancelling = false;
        });
      }
    }
  }

  Future<dynamic> run(Data request) =>
      widget.engine.run(request, title: '导入整合包');

  Future<void> inspect() => perform(() async {
        final result = Map<String, dynamic>.from(
            await run({'op': 'modpacks.inspect', 'path': widget.path}) as Map);
        if (!mounted) return;
        setState(() {
          pack = result;
          plan = null;
          name.text = result['name']?.toString() ?? '';
          selectedOptional.clear();
          supplementDirectory = null;
        });
      });

  Future<void> prepare() => perform(() async {
        final result = Map<String, dynamic>.from(await run({
          'op': 'modpacks.plan',
          'path': widget.path,
          'root': root.text.trim(),
          'id': instanceId,
          'name': name.text.trim(),
          if (pack?['archive_sha512'] case final String hash)
            'archive_sha512': hash,
          'optional_files': selectedOptional.toList()..sort(),
          if (supplementDirectory != null)
            'supplement_directory': supplementDirectory,
        }) as Map);
        if (mounted) setState(() => plan = result);
      });

  Future<void> apply() => perform(() async {
        // Keep this exact plan for explicit retries, including its archive hash.
        final result = Map<String, dynamic>.from(
            await run({'op': 'modpacks.apply', 'plan': plan}) as Map);
        if (mounted) Navigator.pop(context, result);
      }, install: true);

  Future<void> cancel() async {
    if (!working) {
      Navigator.pop(context);
      return;
    }
    setState(() => cancelling = true);
    await widget.engine.cancel();
  }

  Future<void> chooseRoot() async {
    final value = await getDirectoryPath(
        initialDirectory: Directory(root.text).existsSync() ? root.text : null,
        confirmButtonText: tr('选择游戏目录'));
    if (value != null && mounted) setState(() => root.text = value);
  }

  Future<void> chooseSupplementDirectory() async {
    final value = await getDirectoryPath(
        initialDirectory: supplementDirectory,
        confirmButtonText: tr('选择补充文件目录'));
    if (value == null || !mounted) return;
    supplementDirectory = value;
    await prepare();
  }

  Future<void> openFilePage(Uri url) async {
    try {
      await widget.engine.run({'op': 'resource.open', 'url': url.toString()},
          title: '正在打开项目网页');
    } on EngineFailure catch (e) {
      if (mounted) setState(() => notice = errorMessage(e.details));
    }
  }

  Uri? filePage(Data file) {
    final url = Uri.tryParse(file['website_url']?.toString() ?? '');
    if (url == null ||
        url.scheme != 'https' ||
        url.userInfo.isNotEmpty ||
        url.hasPort ||
        !const {'curseforge.com', 'www.curseforge.com'}.contains(url.host)) {
      return null;
    }
    return url;
  }

  String formatLabel(Data metadata) =>
      metadata['format'] == 'curseforge' ? 'CurseForge' : 'Modrinth';

  String packLabel(Data metadata) {
    final version = metadata['version_id']?.toString() ?? '';
    return version.isEmpty
        ? '${metadata['name']}'
        : '${metadata['name']} · $version';
  }

  String curseForgeFileId(Data file) =>
      tr('项目 {0} · 文件 {1}', [file['project_id'], file['file_id']]);

  String versionLabel(Data value) {
    final loader = value['loader']?.toString() ?? 'vanilla';
    return 'Minecraft ${value['minecraft']} · '
        '${loader == 'vanilla' ? tr('原版') : loader} '
        '${value['loader_version'] ?? ''}';
  }

  Widget detail(String label, String value, {bool selectable = false}) =>
      Padding(
          padding: const EdgeInsets.only(bottom: 10),
          child:
              Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
            Text(label, style: TextStyle(color: muted, fontSize: 12)),
            const SizedBox(height: 4),
            if (selectable)
              SelectableText(value, style: const TextStyle(fontSize: 13))
            else
              Text(value, style: const TextStyle(fontSize: 13))
          ]));

  Widget get setup {
    final curseForge = pack!['format'] == 'curseforge';
    final files = dataRows(pack!['files'])
        .where((file) => file['client'] != 'unsupported')
        .toList();
    return SingleChildScrollView(
        key: const Key('modpackSetup'),
        child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              Text(packLabel(pack!),
                  style: const TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 6),
              Text('${formatLabel(pack!)} · ${versionLabel(pack!)}',
                  style: TextStyle(color: muted)),
              if ((pack!['summary'] ?? '').toString().isNotEmpty)
                Padding(
                    padding: const EdgeInsets.only(top: 10),
                    child: Text(pack!['summary'].toString(),
                        style: TextStyle(color: muted, height: 1.5))),
              const SizedBox(height: 18),
              TextField(
                  key: const Key('modpackName'),
                  controller: name,
                  maxLength: 180,
                  decoration: InputDecoration(labelText: tr('实例名称')),
                  onChanged: (_) => setState(() {})),
              const SizedBox(height: 8),
              Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Expanded(
                    child: TextField(
                        key: const Key('modpackRoot'),
                        controller: root,
                        decoration: InputDecoration(labelText: tr('游戏目录')),
                        onChanged: (_) => setState(() {}))),
                const SizedBox(width: 8),
                IconButton(
                    tooltip: tr('选择游戏目录'),
                    onPressed: chooseRoot,
                    icon: const Icon(Icons.folder_open_rounded))
              ]),
              const SizedBox(height: 14),
              Text(tr('将创建独立实例，不会合并到已有实例。'),
                  style: TextStyle(color: muted, fontSize: 12)),
              const SizedBox(height: 18),
              Text(tr('客户端文件'),
                  style: const TextStyle(fontWeight: FontWeight.w600)),
              const SizedBox(height: 4),
              Text(tr('必需文件自动安装，可选文件默认不安装。'),
                  style: TextStyle(color: muted, fontSize: 12)),
              if (curseForge && files.isNotEmpty)
                Text(tr('生成导入计划时会查询 CurseForge 文件信息。'),
                    style: TextStyle(color: muted, fontSize: 12)),
              const SizedBox(height: 8),
              SurfaceCard(
                  padding: const EdgeInsets.symmetric(vertical: 4),
                  child: SizedBox(
                      height: (files.length * 64.0).clamp(0.0, 220.0),
                      child: ListView.builder(
                          primary: false,
                          itemCount: files.length,
                          itemBuilder: (_, index) {
                            final file = files[index];
                            final path = file['path'].toString();
                            final optional = file['client'] == 'optional';
                            return CheckboxListTile(
                                key: ValueKey('modpack-file-$path'),
                                dense: true,
                                controlAffinity:
                                    ListTileControlAffinity.leading,
                                contentPadding:
                                    const EdgeInsets.symmetric(horizontal: 8),
                                value: !optional ||
                                    selectedOptional.contains(path),
                                onChanged: optional
                                    ? (value) => setState(() {
                                          if (value == true) {
                                            selectedOptional.add(path);
                                          } else {
                                            selectedOptional.remove(path);
                                          }
                                        })
                                    : null,
                                title: Text(
                                    curseForge ? curseForgeFileId(file) : path,
                                    style: const TextStyle(fontSize: 12)),
                                subtitle: Text(
                                    [
                                      tr(optional ? '可选' : '必需'),
                                      if (file['size'] is num)
                                        byteLabel(file['size'] as num)
                                    ].join(' · '),
                                    style:
                                        TextStyle(color: muted, fontSize: 11)));
                          }))),
              const SizedBox(height: 10),
              Text(
                  tr(curseForge ? '覆盖文件：{0}' : '覆盖文件：{0}；仅服务端文件将跳过。',
                      [pack!['override_count'] ?? 0]),
                  style: TextStyle(color: muted, fontSize: 12)),
            ]));
  }

  String blockedReason(Data file) => tr(
      switch (file['reason']) {
        'declared_conflict' => '作者声明与 {0} 不兼容。',
        'unsupported_project_type' => '启动器暂不支持此资源类型。',
        'missing_checksum' => 'CurseForge 未提供用于校验的文件哈希。',
        'invalid_file_size' => 'CurseForge 提供的文件大小无效。',
        'unsafe_path' || 'reserved_path' => '该文件的名称无法安全导入。',
        'manual_download_required' => '请从 CurseForge 文件页面手动下载。',
        'supplement_mismatch' => '本地文件校验失败，请核对文件版本。',
        _ => 'CurseForge 上的指定文件不可用。'
      },
      [file['conflict_title']]);

  Widget blockedFileRow(Data file) {
    final url = filePage(file);
    final title = file['title']?.toString() ?? '';
    final filename = file['filename']?.toString() ?? '';
    final label = title.isNotEmpty ? title : filename;
    return Padding(
        padding: const EdgeInsets.symmetric(vertical: 8),
        child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Expanded(
              child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                Text(label.isEmpty ? curseForgeFileId(file) : label,
                    style: const TextStyle(
                        fontSize: 13, fontWeight: FontWeight.w600)),
                if (filename.isNotEmpty && filename != label)
                  SelectableText(filename,
                      style: TextStyle(color: muted, fontSize: 12)),
                if (label.isNotEmpty)
                  Text(curseForgeFileId(file),
                      style: TextStyle(color: muted, fontSize: 12)),
                const SizedBox(height: 3),
                Text(
                    '${tr(file['client'] == 'optional' ? '可选' : '必需')} · '
                    '${blockedReason(file)}',
                    style: TextStyle(color: muted, fontSize: 12)),
              ])),
          if (url != null)
            TextButton.icon(
                onPressed:
                    widget.engine.active ? null : () => openFilePage(url),
                icon: const Icon(Icons.open_in_new, size: 16),
                label: Text(tr('在官网打开'))),
        ]));
  }

  Widget blockedFilePanel(List<Data> blocked) {
    final canSupplement = blocked.any((file) =>
        file['reason'] == 'manual_download_required' ||
        file['reason'] == 'supplement_mismatch');
    return SurfaceCard(
        color: seclColor(0xFFFFF6F1),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Icon(Icons.warning_amber_rounded,
                color: seclColor(0xFFAD6342), size: 20),
            const SizedBox(width: 8),
            Expanded(
                child: Text(tr('{0} 个文件需要处理', [blocked.length]),
                    style: const TextStyle(fontWeight: FontWeight.w600))),
          ]),
          const SizedBox(height: 8),
          Text(tr('处理这些文件后才能开始导入。可选文件可以返回取消选择。'),
              style: TextStyle(color: muted, fontSize: 12, height: 1.5)),
          if (blocked.length == 1)
            blockedFileRow(blocked.single)
          else
            SizedBox(
                height: (blocked.length * 100.0).clamp(160.0, 260.0),
                child: ListView.separated(
                    primary: false,
                    itemCount: blocked.length,
                    separatorBuilder: (_, __) =>
                        Divider(height: 1, color: line),
                    itemBuilder: (_, index) => blockedFileRow(blocked[index]))),
          if (canSupplement) ...[
            Text(tr('将手动下载的文件放在同一文件夹，然后选择该目录以校验。'),
                style: TextStyle(color: muted, fontSize: 12, height: 1.5)),
            const SizedBox(height: 8),
            OutlinedButton.icon(
                key: const Key('chooseModpackSupplementDirectory'),
                onPressed: chooseSupplementDirectory,
                icon: const Icon(Icons.folder_open_rounded, size: 17),
                label: Text(tr('选择补充文件目录'))),
            if (supplementDirectory case final directory?)
              SelectableText(directory,
                  style: TextStyle(color: muted, fontSize: 12)),
          ]
        ]));
  }

  Widget get review {
    final value = plan!;
    final metadata = Map<String, dynamic>.from(value['pack'] as Map);
    final files = dataRows(value['files']);
    final blocked = blockedFiles;
    final supplementalCount =
        files.where((file) => file['supplemental'] == true).length;
    final optionalFiles =
        List<String>.from(value['optional_files'] as List? ?? []);
    final curseForgeFiles = <String, Data>{
      if (metadata['format'] == 'curseforge')
        for (final file in [...files, ...blocked])
          '${file['project_id']}/${file['file_id']}': file,
    };
    return SingleChildScrollView(
        key: const Key('modpackReview'),
        child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            mainAxisSize: MainAxisSize.min,
            children: [
              if (blocked.isNotEmpty) ...[
                blockedFilePanel(blocked),
                const SizedBox(height: 14),
              ],
              detail(tr('实例名称'), value['name'].toString()),
              detail(tr('独立实例目录'), value['directory'].toString(),
                  selectable: true),
              detail(tr('游戏与加载器'), versionLabel(metadata)),
              detail(tr('整合包版本'), packLabel(metadata)),
              detail(tr('来源'), formatLabel(metadata)),
              detail(
                  tr('下载文件'),
                  tr('{0} 个文件 · {1}', [
                    files.length - supplementalCount,
                    byteLabel(value['total_download_bytes'] as num? ?? 0)
                  ])),
              Padding(
                  padding: const EdgeInsets.only(bottom: 10),
                  child: Text(tr('下载大小仅为整合包文件，不含游戏、Java 和加载器。'),
                      style:
                          TextStyle(color: muted, fontSize: 12, height: 1.5))),
              detail(tr('覆盖文件'), '${value['override_count'] ?? 0}'),
              if (metadata['format'] != 'curseforge')
                detail(tr('跳过的服务端文件'), '${value['skipped_server_count'] ?? 0}'),
              if (supplementalCount > 0)
                detail(tr('已校验的本地文件'), '$supplementalCount'),
              if (optionalFiles.isNotEmpty)
                detail(
                    tr('已选可选文件'),
                    optionalFiles
                        .map((id) =>
                            curseForgeFiles[id]?['filename']?.toString() ?? id)
                        .join('\n')),
              if (value['resumable'] != false) ...[
                const SizedBox(height: 4),
                SurfaceCard(
                    child: Text(tr('导入中断后可重试，已校验的文件会保留并继续使用。'),
                        style:
                            TextStyle(color: muted, fontSize: 12, height: 1.5)))
              ]
            ]));
  }

  Widget get progress {
    final value = widget.engine.progress;
    final done = (value['done'] as num? ?? 0).toDouble();
    final total = (value['total'] as num? ?? 0).toDouble();
    return SizedBox(
        height: 240,
        child: Center(
            child: SurfaceCard(
                child: Column(mainAxisSize: MainAxisSize.min, children: [
          Text(nativeText(value['message']?.toString() ?? tr('准备导入')),
              style:
                  const TextStyle(fontSize: 13, fontWeight: FontWeight.w600)),
          const SizedBox(height: 12),
          LinearProgressIndicator(
              value: total > 0 ? (done / total).clamp(0, 1) : null),
          if (plan?['resumable'] != false) ...[
            const SizedBox(height: 10),
            Text(tr('取消后保留已校验的文件，可稍后重新导入。'),
                style: TextStyle(color: muted, fontSize: 12))
          ]
        ]))));
  }

  @override
  Widget build(BuildContext context) => PopScope(
      canPop: !working,
      child: Dialog(
          child: ConstrainedBox(
              constraints: BoxConstraints(
                  maxWidth: 600,
                  maxHeight:
                      (MediaQuery.sizeOf(context).height - 64).clamp(200, 640)),
              child: Padding(
                  padding: const EdgeInsets.all(24),
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                    Row(children: [
                      Icon(Icons.inventory_2_outlined, color: accent, size: 23),
                      const SizedBox(width: 10),
                      Expanded(
                          child: Text(
                              plan == null ? tr('导入整合包') : tr('确认导入整合包'),
                              style: const TextStyle(
                                  fontSize: 19, fontWeight: FontWeight.w600)))
                    ]),
                    const SizedBox(height: 18),
                    Flexible(
                        child: problem != null
                            ? SizedBox(
                                height: 300,
                                child: ProblemPanel(problem!, retry: retry))
                            : working
                                ? applying
                                    ? progress
                                    : SizedBox(
                                        height: 300,
                                        child: BusyPanel(
                                            label: plan == null && pack == null
                                                ? tr('正在读取整合包')
                                                : tr('正在生成导入计划')))
                                : plan == null
                                    ? pack == null
                                        ? const SizedBox()
                                        : setup
                                    : review),
                    if (notice != null)
                      Padding(
                          padding: const EdgeInsets.only(top: 12),
                          child: Text(notice!,
                              style: TextStyle(color: muted, fontSize: 12))),
                    const SizedBox(height: 20),
                    Row(children: [
                      TextButton(
                          key: const Key('cancelModpackImport'),
                          onPressed: cancelling ? null : cancel,
                          child: Text(cancelling ? tr('正在取消') : tr('取消'))),
                      const Spacer(),
                      if (pack != null &&
                          !working &&
                          (plan != null || problem != null))
                        TextButton(
                            onPressed: () => setState(() {
                                  plan = null;
                                  problem = null;
                                  notice = null;
                                }),
                            child: Text(tr('返回修改'))),
                      if (plan != null &&
                          blockedFiles.isNotEmpty &&
                          !working &&
                          problem == null)
                        TextButton(onPressed: prepare, child: Text(tr('重新检查'))),
                      if (problem == null)
                        FilledButton.icon(
                            key: const Key('confirmModpackImport'),
                            onPressed: working ||
                                    pack == null ||
                                    name.text.trim().isEmpty ||
                                    root.text.trim().isEmpty ||
                                    blockedFiles.isNotEmpty
                                ? null
                                : plan == null
                                    ? prepare
                                    : apply,
                            icon: Icon(
                                plan == null
                                    ? Icons.arrow_forward_rounded
                                    : Icons.download_rounded,
                                size: 18),
                            label:
                                Text(plan == null ? tr('查看导入计划') : tr('开始导入')))
                    ])
                  ])))));
}
