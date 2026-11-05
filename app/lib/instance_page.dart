// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';
import 'instance_resources.dart';
import 'instance_settings.dart';
import 'instance_logs.dart';
import 'instance_worlds.dart';
import 'version_components.dart';

class InstancePage extends StatefulWidget {
  const InstancePage(
      {super.key,
      required this.engine,
      required this.instance,
      required this.javas,
      required this.onBack,
      required this.onChanged,
      required this.onBrowse,
      this.initialTab = 'mod'});
  final ResourceEngine engine;
  final Data instance;
  final List<Data> javas;
  final VoidCallback onBack, onChanged;
  final String initialTab;
  final void Function(String, {String? query}) onBrowse;
  @override
  State<InstancePage> createState() => _InstancePageState();
}

class _InstancePageState extends State<InstancePage> {
  late String tab;
  @override
  void initState() {
    super.initState();
    tab = widget.initialTab;
  }

  @override
  Widget build(BuildContext context) => ManagementPage(
      title:
          widget.instance['name'] as String? ?? widget.instance['id'] as String,
      subtitle:
          'Minecraft ${widget.instance['version']} · ${widget.instance['loader']} ${widget.instance['loader_version']}',
      onBack: widget.onBack,
      body: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        SingleChildScrollView(
            scrollDirection: Axis.horizontal,
            child: Row(children: [
              for (final entry in {
                'mod': tr('模组'),
                'shader': tr('光影'),
                'resourcepack': tr('材质包'),
                'worlds': tr('存档'),
                'settings': tr('实例设置'),
                'components': tr('版本组件'),
                'logs': tr('日志')
              }.entries)
                Padding(
                    padding: const EdgeInsets.only(right: 8),
                    child: TextButton(
                        onPressed: () => setState(() => tab = entry.key),
                        style: TextButton.styleFrom(
                            foregroundColor: tab == entry.key
                                ? Theme.of(context).colorScheme.primary
                                : Theme.of(context)
                                    .colorScheme
                                    .onSurfaceVariant,
                            backgroundColor: tab == entry.key
                                ? Theme.of(context)
                                    .colorScheme
                                    .primary
                                    .withOpacity(.09)
                                : null,
                            padding: const EdgeInsets.symmetric(
                                horizontal: 14, vertical: 16)),
                        child: Text(entry.value)))
            ])),
        const SizedBox(height: 18),
        Expanded(
            child: tab == 'worlds'
                ? InstanceWorlds(
                    key: ValueKey('${widget.instance['id']}-worlds'),
                    engine: widget.engine,
                    instance: widget.instance)
                : tab == 'settings'
                    ? InstanceSettings(
                        key: ValueKey('${widget.instance['id']}-settings'),
                        engine: widget.engine,
                        instance: widget.instance,
                        javas: widget.javas,
                        onSaved: widget.onChanged)
                    : tab == 'components'
                        ? VersionComponents(
                            key:
                                ValueKey('${widget.instance['id']}-components'),
                            engine: widget.engine,
                            instance: widget.instance,
                            javas: widget.javas,
                            onChanged: widget.onChanged)
                        : tab == 'logs'
                            ? InstanceLogs(
                                engine: widget.engine,
                                instance: widget.instance)
                            : InstanceResources(
                                key: ValueKey('${widget.instance['id']}-$tab'),
                                engine: widget.engine,
                                instance: widget.instance,
                                kind: tab,
                                onBrowse: widget.onBrowse))
      ]));
}
