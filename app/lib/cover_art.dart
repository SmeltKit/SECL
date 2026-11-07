// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';

const defaultCoverAsset = 'assets/default-background.webp';
const versionCoverFamilies = [
  '1.0',
  '1.1',
  '1.2',
  '1.3',
  '1.4',
  '1.5',
  '1.6',
  '1.7',
  '1.8',
  '1.9',
  '1.10',
  '1.11',
  '1.12',
  '1.13',
  '1.14',
  '1.15',
  '1.16',
  '1.17',
  '1.18',
  '1.19',
  '1.20',
  '1.21',
  '26.1',
  '26.2',
  '26.3',
];

String? coverFamily(String version) {
  final family =
      RegExp(r'^(1\.\d+|26\.\d+)(?=\.|-|$)').firstMatch(version)?.group(1);
  return versionCoverFamilies.contains(family) ? family : null;
}

String versionCoverAsset(String version) {
  final family = coverFamily(version);
  return family == null
      ? defaultCoverAsset
      : 'assets/version-covers/minecraft-$family.webp';
}

String homeBackgroundAsset(String preference, String version) {
  if (preference == 'auto') return versionCoverAsset(version);
  if (versionCoverFamilies.contains(preference)) {
    return versionCoverAsset(preference);
  }
  return defaultCoverAsset;
}

Future<String?> showBackgroundPicker(BuildContext context,
        {required String current, required String version}) =>
    showDialog<String>(
        context: context,
        builder: (_) => _BackgroundPicker(current: current, version: version));

class _BackgroundPicker extends StatefulWidget {
  const _BackgroundPicker({required this.current, required this.version});
  final String current, version;
  @override
  State<_BackgroundPicker> createState() => _BackgroundPickerState();
}

class _BackgroundPickerState extends State<_BackgroundPicker> {
  late String selected = widget.current;
  String label(String value) => value == 'auto'
      ? tr('按游戏版本自动选择')
      : value == 'default'
          ? tr('默认风景')
          : 'Minecraft $value';

  @override
  Widget build(BuildContext context) {
    final size = MediaQuery.sizeOf(context);
    final choices = ['auto', 'default', ...versionCoverFamilies.reversed];
    return AlertDialog(
        title: Text(tr('更换背景')),
        content: SizedBox(
            width: math.min(800, size.width - 96),
            height: math.min(580, size.height - 200),
            child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Expanded(
                      flex: 3,
                      child: ClipRRect(
                          borderRadius: BorderRadius.circular(9),
                          child: Image.asset(
                              homeBackgroundAsset(selected, widget.version),
                              fit: BoxFit.cover,
                              gaplessPlayback: true))),
                  const SizedBox(height: 12),
                  Text(label(selected),
                      style: const TextStyle(fontWeight: FontWeight.w600)),
                  const SizedBox(height: 12),
                  Expanded(
                      flex: 4,
                      child: LayoutBuilder(builder: (context, bounds) {
                        final columns = bounds.maxWidth < 580 ? 2 : 3;
                        return GridView.builder(
                            key: const Key('backgroundChoices'),
                            gridDelegate:
                                SliverGridDelegateWithFixedCrossAxisCount(
                                    crossAxisCount: columns,
                                    mainAxisExtent: 112,
                                    crossAxisSpacing: 10,
                                    mainAxisSpacing: 10),
                            itemCount: choices.length,
                            itemBuilder: (context, index) {
                              final value = choices[index];
                              final active = selected == value;
                              return Material(
                                  color: Colors.transparent,
                                  child: InkWell(
                                      key: Key('background-$value'),
                                      borderRadius: BorderRadius.circular(8),
                                      onTap: () =>
                                          setState(() => selected = value),
                                      child: Container(
                                          padding: const EdgeInsets.all(3),
                                          decoration: BoxDecoration(
                                              border: Border.all(
                                                  color: active ? accent : line,
                                                  width: active ? 2 : 1),
                                              borderRadius:
                                                  BorderRadius.circular(8)),
                                          child: Column(children: [
                                            Expanded(
                                                child: ClipRRect(
                                                    borderRadius:
                                                        BorderRadius.circular(
                                                            5),
                                                    child: Image.asset(
                                                        homeBackgroundAsset(
                                                            value,
                                                            widget.version),
                                                        cacheWidth: 360,
                                                        fit: BoxFit.cover,
                                                        width:
                                                            double.infinity))),
                                            Padding(
                                                padding:
                                                    const EdgeInsets.fromLTRB(
                                                        4, 5, 4, 3),
                                                child: Row(children: [
                                                  Expanded(
                                                      child: Text(label(value),
                                                          maxLines: 1,
                                                          overflow: TextOverflow
                                                              .ellipsis,
                                                          style:
                                                              const TextStyle(
                                                                  fontSize:
                                                                      12))),
                                                  if (active)
                                                    Icon(Icons.check_rounded,
                                                        size: 16, color: accent)
                                                ]))
                                          ]))));
                            });
                      }))
                ])),
        actions: [
          TextButton(
              onPressed: () => Navigator.pop(context), child: Text(tr('取消'))),
          FilledButton(
              key: const Key('applyBackground'),
              onPressed: () => Navigator.pop(context, selected),
              child: Text(tr('应用')))
        ]);
  }
}
