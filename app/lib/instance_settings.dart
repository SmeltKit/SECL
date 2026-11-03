// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'management_widgets.dart';
import 'localization.dart';
import 'resource_engine.dart';

class InstanceSettings extends StatefulWidget {
  const InstanceSettings(
      {super.key,
      required this.engine,
      required this.instance,
      required this.javas,
      required this.onSaved});
  final ResourceEngine engine;
  final Data instance;
  final List<Data> javas;
  final VoidCallback onSaved;
  @override
  State<InstanceSettings> createState() => _InstanceSettingsState();
}

class _InstanceSettingsState extends State<InstanceSettings>
    with ManagedState<InstanceSettings> {
  @override
  ResourceEngine get engine => widget.engine;
  final name = TextEditingController(),
      jvm = TextEditingController(),
      gameArgs = TextEditingController();
  Data settings = {}, recommendation = {}, memory = {};
  List<Data> runtimes = [];
  String section = 'basic', cover = '', javaPath = '', javaMode = 'auto';
  bool favorite = false,
      isolated = true,
      force = false,
      automatic = true,
      resolution = false,
      fullscreen = false;
  int enabledMods = 0;
  int maximum = 2048, width = 1280, height = 720;
  int? minimum;
  @override
  void initState() {
    super.initState();
    load();
  }

  @override
  void dispose() {
    name.dispose();
    jvm.dispose();
    gameArgs.dispose();
    super.dispose();
  }

  Future<void> load() => perform(() async {
        final saved = Map<String, dynamic>.from(await rpc(
                {'op': 'settings.load', 'directory': widget.instance['path']})
            as Map);
        final snapshot = Map<String, dynamic>.from(
            await rpc({'op': 'memory.snapshot'}) as Map);
        final inventory = await rpc({
          'op': 'mods.list',
          'directory': widget.instance['game_directory'],
          'loader': widget.instance['loader']
        });
        enabledMods =
            dataRows(inventory).where((m) => m['enabled'] != false).length;
        final advised = Map<String, dynamic>.from(await rpc({
          'op': 'memory.recommend',
          'snapshot': snapshot,
          'context': {
            'game_version': widget.instance['version'],
            'enabled_mods': enabledMods
          }
        }) as Map);
        final fixed = (saved['java'] as Map?)?['path'] as String? ?? '';
        Data? pinned;
        if (fixed.isNotEmpty && !widget.javas.any((j) => j['path'] == fixed)) {
          final probed = await rpc({'op': 'java.probe', 'path': fixed});
          if (probed is Map) pinned = Map<String, dynamic>.from(probed);
        }
        if (!mounted) return;
        setState(() {
          settings = saved;
          memory = snapshot;
          recommendation = advised;
          runtimes = [...widget.javas, if (pinned != null) pinned];
          final launch =
              Map<String, dynamic>.from(saved['launch'] as Map? ?? {});
          final java = Map<String, dynamic>.from(saved['java'] as Map? ?? {});
          final display =
              Map<String, dynamic>.from(saved['display'] as Map? ?? {});
          name.text = display['name'] as String? ??
              widget.instance['name'] as String? ??
              widget.instance['id'] as String;
          cover = display['cover'] as String? ?? '';
          favorite = display['favorite'] == true;
          isolated =
              saved['isolated'] as bool? ?? widget.instance['isolated'] == true;
          javaMode = java['mode'] as String? ?? 'auto';
          javaPath = java['path'] as String? ?? '';
          force = java['allow_below_recommended'] == true ||
              java['allowBelowRecommended'] == true;
          automatic = launch['auto_memory'] != false;
          maximum = (automatic
                  ? advised['maximum_mib']
                  : launch['max_memory_mib']) as int? ??
              2048;
          minimum = (launch['min_memory_mib'] as int? ?? 0) > 0
              ? launch['min_memory_mib'] as int
              : null;
          resolution = launch['custom_resolution'] == true;
          width = launch['width'] as int? ?? 1280;
          height = launch['height'] as int? ?? 720;
          fullscreen = launch['fullscreen'] == true;
          jvm.text = launch['jvm_arguments'] as String? ?? '';
          gameArgs.text = launch['game_arguments'] as String? ?? '';
        });
      });
  Future<void> refreshRecommendation() => perform(() async {
        final snapshot = Map<String, dynamic>.from(
            await rpc({'op': 'memory.snapshot'}) as Map);
        final advice = Map<String, dynamic>.from(await rpc({
          'op': 'memory.recommend',
          'snapshot': snapshot,
          'context': {
            'game_version': widget.instance['version'],
            'enabled_mods': enabledMods,
            'java_arch': runtimes
                    .where((j) => j['path'] == javaPath)
                    .firstOrNull?['arch'] ??
                ''
          }
        }) as Map);
        if (mounted) {
          setState(() {
            memory = snapshot;
            recommendation = advice;
            if (automatic) maximum = advice['maximum_mib'] as int;
          });
        }
      });
  Future<void> browseJava() => perform(() async {
        final file = await openFile(confirmButtonText: tr('选择 Java'));
        if (file == null) return;
        final runtime = await rpc({'op': 'java.probe', 'path': file.path});
        if (runtime is! Map) {
          throw EngineFailure(
              {'code': 'java_unavailable', 'message': tr('所选 Java 无法运行。')});
        }
        final row = Map<String, dynamic>.from(runtime);
        if (mounted) {
          setState(() {
            if (!runtimes.any((j) => j['path'] == row['path'])) {
              runtimes.add(row);
            }
            javaPath = row['path'] as String;
            javaMode = 'fixed';
            force = false;
          });
        }
      });
  Future<void> save() => perform(() async {
        if (javaMode == 'fixed' && javaPath.isEmpty) {
          throw EngineFailure(
              {'code': 'java_missing', 'message': tr('请选择固定使用的 Java。')});
        }
        final required = widget.instance['java_major'] as int? ?? 8;
        final chosen = runtimes.where((j) => j['path'] == javaPath).firstOrNull;
        if (javaMode == 'fixed' &&
            chosen != null &&
            (chosen['major'] as int? ?? 0) < required &&
            !force) {
          throw EngineFailure({
            'code': 'java_too_old',
            'message': tr('所选 Java 低于默认版本，请主动确认强制使用后重试。')
          });
        }
        final next = <String, dynamic>{
          ...settings,
          'display': {
            'name': name.text.trim(),
            'cover': cover,
            'favorite': favorite
          },
          'isolated': isolated,
          'java': {
            'mode': javaMode,
            if (javaMode == 'fixed') ...{
              'path': javaPath,
              for (final key in [
                'runtime_version',
                'vendor',
                'vendor_version',
                'arch'
              ])
                if (chosen?[key] != null) key: chosen![key]
            },
            'allow_below_recommended': force
          },
          'launch': {
            'auto_memory': automatic,
            'max_memory_mib': maximum,
            'min_memory_mib': minimum ?? 0,
            'custom_resolution': resolution,
            'width': width,
            'height': height,
            'fullscreen': fullscreen,
            'jvm_arguments': jvm.text,
            'game_arguments': gameArgs.text
          }
        };
        await rpc({
          'op': 'settings.save',
          'directory': widget.instance['path'],
          'settings': next
        });
        if (!mounted) return;
        settings = next;
        widget.onSaved();
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(tr('实例设置已保存'))));
      });
  Widget heading(String label) => Padding(
      padding: const EdgeInsets.only(bottom: 14, top: 10),
      child: Text(tr(label),
          style: const TextStyle(fontSize: 17, fontWeight: FontWeight.w600)));
  Widget get basic =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        heading('基础信息'),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(tr('实例名称')),
          const SizedBox(height: 8),
          TextField(controller: name),
          const SizedBox(height: 18),
          Wrap(spacing: 12, runSpacing: 10, children: [
            OutlinedButton.icon(
                onPressed: locked
                    ? null
                    : () async {
                        final file = await openFile(acceptedTypeGroups: [
                          const XTypeGroup(
                              label: 'Image',
                              extensions: ['png', 'jpg', 'jpeg', 'webp'])
                        ]);
                        if (file != null && mounted) {
                          setState(() => cover = file.path);
                        }
                      },
                icon: const Icon(Icons.image_outlined, size: 18),
                label: Text(tr('更换封面'))),
            TextButton(
                onPressed: () => setState(() => cover = ''),
                child: Text(tr('恢复默认封面')))
          ]),
          if (cover.isNotEmpty)
            Padding(
                padding: const EdgeInsets.only(top: 8),
                child:
                    Text(cover, style: TextStyle(color: muted, fontSize: 12))),
          CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              title: Text(tr('收藏实例')),
              value: favorite,
              onChanged: (v) => setState(() => favorite = v ?? false))
        ])),
        const SizedBox(height: 16),
        heading('游戏目录'),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          SwitchListTile(
              contentPadding: EdgeInsets.zero,
              title: Text(tr('使用独立游戏目录')),
              value: isolated,
              onChanged: (v) => setState(() => isolated = v)),
          SelectableText(
              isolated
                  ? (widget.instance['isolated'] == true
                      ? widget.instance['game_directory'] as String
                      : widget.instance['path'] as String)
                  : widget.instance['root'] as String,
              style: TextStyle(fontSize: 12, color: muted, height: 1.6)),
          const SizedBox(height: 10),
          Text(tr('更改此项不会移动已有存档、模组或设置，请确认目标目录。'),
              style: TextStyle(fontSize: 12, color: muted, height: 1.6))
        ]))
      ]);
  Widget get javaSettings =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        heading('Java 运行环境'),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(
              tr('游戏默认 Java {0}，可手动选择更高版本。',
                  [widget.instance['java_major'] ?? 8]),
              style: TextStyle(color: muted, height: 1.6)),
          RadioListTile<String>(
              contentPadding: EdgeInsets.zero,
              value: 'auto',
              groupValue: javaMode,
              title: Text(tr('自动匹配（推荐）')),
              onChanged: (v) => setState(() => javaMode = v!)),
          RadioListTile<String>(
              contentPadding: EdgeInsets.zero,
              value: 'fixed',
              groupValue: javaMode,
              title: Text(tr('固定使用某个 Java')),
              onChanged: (v) => setState(() => javaMode = v!)),
          const SizedBox(height: 12),
          Row(children: [
            Expanded(
                child: DropdownButtonFormField<String>(
                    isExpanded: true,
                    value: runtimes.any((j) => j['path'] == javaPath)
                        ? javaPath
                        : null,
                    hint: Text(tr('请选择已安装的 Java')),
                    items: [
                      for (final j in runtimes)
                        DropdownMenuItem(
                            value: j['path'] as String,
                            child: Text(
                                'Java ${j['version']} · ${j['vendor']} · ${j['arch']}',
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis))
                    ],
                    onChanged: javaMode != 'fixed'
                        ? null
                        : (v) => setState(() {
                              javaPath = v ?? '';
                              force = false;
                            }))),
            const SizedBox(width: 10),
            OutlinedButton(
                onPressed: locked ? null : browseJava, child: Text(tr('浏览')))
          ]),
          if (javaPath.isNotEmpty)
            Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: SelectableText(javaPath,
                    style: TextStyle(fontSize: 12, color: muted))),
          CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              value: force,
              onChanged: javaMode == 'fixed'
                  ? (v) => setState(() => force = v ?? false)
                  : null,
              title: Text(tr('允许使用低于推荐版本的 Java')),
              subtitle: Text(tr('低版本 Java 可能无法运行此游戏或加载器。')))
        ]))
      ]);
  Widget get launch =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Row(children: [
          Expanded(child: heading('内存分配')),
          OutlinedButton.icon(
              onPressed: locked
                  ? null
                  : () => setState(() {
                        automatic = true;
                        maximum = recommendation['maximum_mib'] as int? ?? 2048;
                        minimum = null;
                      }),
              icon: const Icon(Icons.auto_fix_high_rounded, size: 18),
              label: Text(tr('使用推荐设置')))
        ]),
        Wrap(spacing: 22, children: [
          Row(mainAxisSize: MainAxisSize.min, children: [
            Radio<bool>(
                value: true,
                groupValue: automatic,
                onChanged: (v) => setState(() {
                      automatic = v!;
                      maximum =
                          recommendation['maximum_mib'] as int? ?? maximum;
                    })),
            Text(tr('自动分配（推荐）'))
          ]),
          Row(mainAxisSize: MainAxisSize.min, children: [
            Radio<bool>(
                value: false,
                groupValue: automatic,
                onChanged: (v) => setState(() => automatic = v!)),
            Text(tr('手动指定'))
          ])
        ]),
        const SizedBox(height: 12),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(tr('最大内存'),
              style:
                  const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
          const SizedBox(height: 12),
          LayoutBuilder(builder: (context, bounds) {
            final limit = math
                .max(2048, (memory['total_mib'] as int? ?? 16384))
                .clamp(2048, 262144);
            final field = StepNumberField(
                value: maximum,
                minimum: 256,
                maximum: 262144,
                unit: 'MiB',
                onChanged: (v) => setState(() {
                      maximum = v ?? 2048;
                      automatic = false;
                    }));
            final slider = Slider(
                value: maximum.toDouble().clamp(256, limit.toDouble()),
                min: 256,
                max: limit.toDouble(),
                onChanged: (v) => setState(() {
                      maximum = (v / 256).round() * 256;
                      automatic = false;
                    }));
            return bounds.maxWidth < 570
                ? Column(
                    crossAxisAlignment: CrossAxisAlignment.end,
                    children: [slider, field])
                : Row(children: [
                    Expanded(child: slider),
                    const SizedBox(width: 12),
                    field
                  ]);
          }),
          const SizedBox(height: 12),
          Text(tr(automatic ? '启动前根据可用内存重新计算' : '使用手动指定的最大内存'),
              style: TextStyle(fontSize: 12, color: muted)),
          Row(children: [
            Expanded(
                child: ExpansionTile(
                    tilePadding: EdgeInsets.zero,
                    title:
                        Text(tr('推荐依据'), style: const TextStyle(fontSize: 13)),
                    children: [
                  Padding(
                      padding: const EdgeInsets.all(10),
                      child: SelectableText(
                          '${tr('物理内存')}：${memory['total_mib']} MiB\n${tr('可用内存')}：${memory['available_mib']} MiB\n${recommendation['reason'] ?? recommendation['reasons'] ?? ''}',
                          style: TextStyle(
                              fontSize: 12, color: muted, height: 1.6)))
                ])),
            IconButton(
                tooltip: tr('刷新'),
                onPressed: locked ? null : refreshRecommendation,
                icon: const Icon(Icons.refresh_rounded))
          ]),
          const Divider(),
          const SizedBox(height: 12),
          Wrap(
              spacing: 20,
              runSpacing: 12,
              crossAxisAlignment: WrapCrossAlignment.center,
              children: [
                Text(tr('初始内存'),
                    style: const TextStyle(
                        fontSize: 14, fontWeight: FontWeight.w600)),
                StepNumberField(
                    value: minimum,
                    maximum: maximum,
                    placeholder: tr('由 Java 决定'),
                    unit: 'MiB',
                    onChanged: (v) => setState(() => minimum = v))
              ])
        ])),
        const SizedBox(height: 18),
        heading('游戏窗口'),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              value: resolution,
              onChanged: (v) => setState(() => resolution = v ?? false),
              title: Text(tr('自定义窗口大小'))),
          const SizedBox(height: 10),
          Wrap(spacing: 20, runSpacing: 14, children: [
            Row(mainAxisSize: MainAxisSize.min, children: [
              Text(tr('宽度')),
              const SizedBox(width: 10),
              StepNumberField(
                  value: width,
                  step: 80,
                  minimum: 320,
                  maximum: 16384,
                  unit: 'px',
                  onChanged: (v) => setState(() {
                        width = v ?? 1280;
                        resolution = true;
                      }))
            ]),
            Row(mainAxisSize: MainAxisSize.min, children: [
              Text(tr('高度')),
              const SizedBox(width: 10),
              StepNumberField(
                  value: height,
                  step: 60,
                  minimum: 240,
                  maximum: 16384,
                  unit: 'px',
                  onChanged: (v) => setState(() {
                        height = v ?? 720;
                        resolution = true;
                      }))
            ])
          ]),
          const SizedBox(height: 16),
          const Divider(),
          CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              value: fullscreen,
              onChanged: (v) => setState(() => fullscreen = v ?? false),
              title: Text(tr('以全屏模式启动')))
        ])),
        const SizedBox(height: 18),
        heading('附加启动参数'),
        SurfaceCard(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(tr('JVM 参数')),
          const SizedBox(height: 8),
          TextField(
              controller: jvm,
              maxLines: 3,
              decoration: const InputDecoration(hintText: '-XX:+UseG1GC')),
          const SizedBox(height: 16),
          Text(tr('游戏参数')),
          const SizedBox(height: 8),
          TextField(controller: gameArgs, maxLines: 2),
          const SizedBox(height: 12),
          Text(tr('内存、账号、主类和游戏目录由启动器管理；保存时会检查参数冲突。'),
              style: TextStyle(fontSize: 12, color: muted, height: 1.6))
        ]))
      ]);
  @override
  Widget build(BuildContext context) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Wrap(spacing: 10, children: [
          for (final entry in {
            'basic': tr('基础信息'),
            'java': tr('Java 设置'),
            'launch': tr('启动设置')
          }.entries)
            ChoiceChip(
                label: Text(entry.value),
                selected: section == entry.key,
                onSelected: (v) => setState(() => section = entry.key))
        ]),
        const SizedBox(height: 12),
        Expanded(
            child: result(SingleChildScrollView(
                child: section == 'basic'
                    ? basic
                    : section == 'java'
                        ? javaSettings
                        : launch))),
        const SizedBox(height: 12),
        Align(
            alignment: Alignment.centerRight,
            child: FilledButton.icon(
                onPressed: locked ? null : save,
                icon: const Icon(Icons.save_outlined, size: 18),
                label: Text(tr('保存')))),
        const SizedBox(height: 12)
      ]);
}
