// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'cover_art.dart';
import 'localization.dart';

/// Application preferences live in the content area, alongside the other pages.
class SettingsPage extends StatelessWidget {
  const SettingsPage(
      {super.key,
      required this.background,
      required this.version,
      required this.onAppearanceChanged,
      required this.onLanguageChanged,
      required this.onChooseBackground,
      required this.onDone,
      required this.onAbout});
  final String background, version;
  final Future<void> Function(String mode, String tone) onAppearanceChanged;
  final Future<void> Function(String language) onLanguageChanged;
  final VoidCallback onChooseBackground, onDone, onAbout;

  Widget section(String title, Widget content) => Padding(
      padding: const EdgeInsets.symmetric(vertical: 23),
      child: LayoutBuilder(
          builder: (context, bounds) =>
              Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
                SizedBox(
                    width: bounds.maxWidth < 540 ? 84 : 110,
                    child: Padding(
                        padding: const EdgeInsets.only(top: 9, right: 14),
                        child: Text(title,
                            style: TextStyle(
                                fontSize: 14,
                                height: 1.5,
                                fontWeight: FontWeight.w600,
                                color: ink)))),
                Expanded(child: content)
              ])));

  @override
  Widget build(BuildContext context) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        PageMasthead(
            title: tr('设置'),
            actions: [TextButton(onPressed: onDone, child: Text(tr('完成')))]),
        Expanded(
            child: SingleChildScrollView(
                key: const Key('settingsPage'),
                padding: const EdgeInsets.fromLTRB(28, 0, 28, 24),
                child: Column(
                    crossAxisAlignment: CrossAxisAlignment.stretch,
                    children: [
                      section(tr('主题模式'),
                          LayoutBuilder(builder: (context, bounds) {
                        final width =
                            ((bounds.maxWidth - 28) / 3).clamp(90.0, 150.0);
                        return Wrap(spacing: 14, runSpacing: 12, children: [
                          for (final entry in const {
                            'light': '浅色',
                            'dark': '深色',
                            'system': '跟随系统'
                          }.entries)
                            SizedBox(
                                width: width,
                                child: _ThemeChoice(
                                    mode: entry.key,
                                    label: tr(entry.value),
                                    selected: AppAppearance.instance.mode ==
                                        entry.key,
                                    onTap: () => unawaited(onAppearanceChanged(
                                        entry.key,
                                        AppAppearance.instance.tone))))
                        ]);
                      })),
                      Divider(color: line.withOpacity(.7)),
                      section(
                          tr('主题色'),
                          Wrap(spacing: 16, runSpacing: 12, children: [
                            for (final entry in const {
                              'blue': '蓝色',
                              'forest': '森林绿',
                              'violet': '紫色'
                            }.entries)
                              _ToneChoice(
                                  tone: entry.key,
                                  label: tr(entry.value),
                                  selected:
                                      AppAppearance.instance.tone == entry.key,
                                  onTap: () => unawaited(onAppearanceChanged(
                                      AppAppearance.instance.mode, entry.key)))
                          ])),
                      Divider(color: line.withOpacity(.7)),
                      section(tr('背景'),
                          LayoutBuilder(builder: (context, bounds) {
                        final preview = ClipRRect(
                            borderRadius: BorderRadius.circular(9),
                            child: Image.asset(
                                homeBackgroundAsset(background, version),
                                width: 126,
                                height: 75,
                                fit: BoxFit.cover,
                                gaplessPlayback: true));
                        final label = Text(
                            background == 'auto'
                                ? tr('按游戏版本自动选择')
                                : background == 'default'
                                    ? tr('默认风景')
                                    : 'Minecraft $background',
                            style: TextStyle(
                                color: ink, fontSize: 13, height: 1.5));
                        final button = OutlinedButton.icon(
                            onPressed: onChooseBackground,
                            icon: const Icon(Icons.wallpaper_rounded, size: 17),
                            label: Text(tr('更换背景')));
                        if (bounds.maxWidth < 440) {
                          return Row(
                              crossAxisAlignment: CrossAxisAlignment.center,
                              children: [
                                preview,
                                const SizedBox(width: 16),
                                Expanded(
                                    child: Column(
                                        crossAxisAlignment:
                                            CrossAxisAlignment.start,
                                        children: [
                                      label,
                                      const SizedBox(height: 10),
                                      button
                                    ]))
                              ]);
                        }
                        return Row(children: [
                          preview,
                          const SizedBox(width: 16),
                          Expanded(child: label),
                          const SizedBox(width: 16),
                          button
                        ]);
                      })),
                      Divider(color: line.withOpacity(.7)),
                      section(
                          tr('界面语言'),
                          Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                ConstrainedBox(
                                    constraints:
                                        const BoxConstraints(maxWidth: 290),
                                    child: DropdownButtonFormField<String>(
                                        key: const Key('languageSelector'),
                                        borderRadius: BorderRadius.circular(9),
                                        focusColor: Colors.transparent,
                                        value: AppLanguage.instance.preference,
                                        isExpanded: true,
                                        style:
                                            TextStyle(fontSize: 13, color: ink),
                                        items: [
                                          DropdownMenuItem(
                                              value: 'system',
                                              child: Text(tr('跟随系统'))),
                                          const DropdownMenuItem(
                                              value: 'zh_Hans',
                                              child: Text('简体中文')),
                                          const DropdownMenuItem(
                                              value: 'zh_Hant',
                                              child: Text('繁體中文')),
                                          const DropdownMenuItem(
                                              value: 'en',
                                              child: Text('English')),
                                        ],
                                        onChanged: (value) {
                                          if (value != null) {
                                            unawaited(onLanguageChanged(value));
                                          }
                                        })),
                                const SizedBox(height: 10),
                                Text(tr('切换后立即生效。项目介绍和文件名保留原文。'),
                                    style: TextStyle(
                                        fontSize: 12,
                                        color: muted,
                                        height: 1.6))
                              ])),
                      Divider(color: line.withOpacity(.7)),
                      Padding(
                          padding: const EdgeInsets.only(top: 17),
                          child: Wrap(
                              crossAxisAlignment: WrapCrossAlignment.center,
                              spacing: 16,
                              runSpacing: 8,
                              children: [
                                Text(tr('0.1.0 · 开发预览'),
                                    style:
                                        TextStyle(fontSize: 12, color: muted)),
                                TextButton.icon(
                                    onPressed: onAbout,
                                    icon: const Icon(Icons.info_outline_rounded,
                                        size: 16),
                                    label: const Text('SECL'))
                              ]))
                    ])))
      ]);
}

class _ThemeChoice extends StatelessWidget {
  const _ThemeChoice(
      {required this.mode,
      required this.label,
      required this.selected,
      required this.onTap});
  final String mode, label;
  final bool selected;
  final VoidCallback onTap;
  @override
  Widget build(BuildContext context) =>
      Column(mainAxisSize: MainAxisSize.min, children: [
        Material(
            color: Colors.transparent,
            child: InkWell(
                key: Key('appearance-$mode'),
                borderRadius: BorderRadius.circular(11),
                onTap: onTap,
                child: AnimatedContainer(
                    duration: const Duration(milliseconds: 150),
                    height: 86,
                    padding: const EdgeInsets.all(4),
                    decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(11),
                        border: Border.all(
                            color: selected ? accent : line,
                            width: selected ? 1.5 : 1)),
                    child: ClipRRect(
                        borderRadius: BorderRadius.circular(7),
                        child: Stack(children: [
                          Positioned.fill(
                              child: Row(children: [
                            Expanded(child: _miniature(mode == 'dark')),
                            if (mode == 'system')
                              Expanded(child: _miniature(true))
                          ])),
                          if (selected)
                            Positioned(
                                right: 5,
                                bottom: 5,
                                child: DecoratedBox(
                                    decoration: BoxDecoration(
                                        color: accent, shape: BoxShape.circle),
                                    child: const Padding(
                                        padding: EdgeInsets.all(3),
                                        child: Icon(Icons.check_rounded,
                                            size: 12, color: Colors.white))))
                        ]))))),
        const SizedBox(height: 8),
        Text(label,
            textAlign: TextAlign.center,
            style: TextStyle(
                fontSize: 12,
                height: 1.5,
                color: selected ? ink : muted,
                fontWeight: selected ? FontWeight.w600 : FontWeight.w400))
      ]);

  Widget _miniature(bool dark) => ColoredBox(
      color: dark ? const Color(0xff252b3b) : const Color(0xfff6f5fc),
      child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Padding(
            padding: const EdgeInsets.fromLTRB(7, 6, 0, 5),
            child: Row(children: [
              for (final color in const [0xffec8b87, 0xffe9cf78, 0xff9acda2])
                Container(
                    width: 4,
                    height: 4,
                    margin: const EdgeInsets.only(right: 3),
                    decoration: BoxDecoration(
                        color: Color(color), shape: BoxShape.circle))
            ])),
        Expanded(
            child: Row(children: [
          SizedBox(
              width: mode == 'system' ? 18 : 28,
              child: ColoredBox(
                  color:
                      dark ? const Color(0xff303648) : const Color(0xffe7e5f2),
                  child: Padding(
                      padding: const EdgeInsets.all(6),
                      child: Column(children: [
                        for (var i = 0; i < 3; i++)
                          Container(
                              height: 5,
                              margin: const EdgeInsets.only(bottom: 5),
                              decoration: BoxDecoration(
                                  color: dark
                                      ? const Color(0xff5b6277)
                                      : const Color(0xffc9cbdc),
                                  borderRadius: BorderRadius.circular(2)))
                      ])))),
          Expanded(
              child: Padding(
                  padding: const EdgeInsets.all(8),
                  child: Column(
                      crossAxisAlignment: CrossAxisAlignment.stretch,
                      children: [
                        Container(
                            height: 13,
                            decoration: BoxDecoration(
                                color: dark
                                    ? const Color(0xff454e64)
                                    : const Color(0xffe4e2f1),
                                borderRadius: BorderRadius.circular(3))),
                        const SizedBox(height: 8),
                        Container(
                            height: 5,
                            decoration: BoxDecoration(
                                color: dark
                                    ? const Color(0xff626b80)
                                    : const Color(0xffd5d6e3),
                                borderRadius: BorderRadius.circular(2)))
                      ])))
        ]))
      ]));
}

class _ToneChoice extends StatelessWidget {
  const _ToneChoice(
      {required this.tone,
      required this.label,
      required this.selected,
      required this.onTap});
  final String tone, label;
  final bool selected;
  final VoidCallback onTap;
  @override
  Widget build(BuildContext context) {
    final color = const {
      'blue': Color(0xff365f7c),
      'forest': Color(0xff26765c),
      'violet': Color(0xff72549d)
    }[tone]!;
    return InkWell(
        key: Key('tone-$tone'),
        borderRadius: BorderRadius.circular(8),
        onTap: onTap,
        child: Padding(
            padding: const EdgeInsets.symmetric(vertical: 3),
            child: Row(mainAxisSize: MainAxisSize.min, children: [
              Container(
                  width: 30,
                  height: 30,
                  decoration: BoxDecoration(
                      color: color,
                      borderRadius: BorderRadius.circular(8),
                      border: Border.all(
                          color: selected ? accent : Colors.transparent,
                          width: 2)),
                  child: selected
                      ? const Icon(Icons.check_rounded,
                          size: 18, color: Colors.white)
                      : null),
              const SizedBox(width: 8),
              Text(label, style: TextStyle(fontSize: 13, color: ink))
            ])));
  }
}
