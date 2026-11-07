// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'localization.dart';

/// The home screen is one continuous scene. Controls sit over its quiet lower
/// edge instead of putting the artwork inside another bordered panel.
class HomeScene extends StatelessWidget {
  const HomeScene({
    super.key,
    required this.backgroundAsset,
    required this.title,
    required this.versionLabel,
    required this.launchStatus,
    required this.versionSelector,
    required this.javaSelector,
    required this.canLaunch,
    required this.gameRunning,
    this.launchPending = false,
    this.error,
    this.instanceBrowser,
    this.onLaunch,
    this.onManage,
    this.onInstallGame,
    this.onImport,
    this.onChooseRoot,
    this.onInstallJava,
    this.onDetectJava,
    this.onChooseJava,
    this.onChangeBackground,
  });

  final String backgroundAsset, title, versionLabel, launchStatus;
  final Widget versionSelector, javaSelector;
  final Widget? error, instanceBrowser;
  final bool canLaunch, gameRunning, launchPending;
  final VoidCallback? onLaunch,
      onManage,
      onInstallGame,
      onImport,
      onChooseRoot,
      onInstallJava,
      onDetectJava,
      onChooseJava,
      onChangeBackground;

  @override
  Widget build(BuildContext context) {
    final dark = AppAppearance.instance.dark;
    final wash = dark ? const Color(0xff131c29) : const Color(0xfff5f2fc);
    return LayoutBuilder(builder: (context, bounds) {
      final compact = bounds.maxWidth < 760;
      final inset = compact ? 26.0 : 38.0;
      final showShelf = instanceBrowser != null && error == null;
      final shelfWidth = compact ? 196.0 : 216.0;
      // Keep the cover flow close to the right edge, clear of the Java row and
      // launch controls. Short windows show less of the fading outer covers.
      final shelfHeight =
          (bounds.maxHeight - 260).clamp(300.0, 460.0).toDouble();
      final shelfTop = (bounds.maxHeight * .47 - shelfHeight / 2)
          .clamp(Platform.isMacOS ? 104.0 : 88.0, double.infinity)
          .toDouble();
      final summaryWidth = showShelf
          ? (bounds.maxWidth - inset * 2 - shelfWidth - 24)
              .clamp(280.0, double.infinity)
              .toDouble()
          : bounds.maxWidth - inset * 2;
      return Stack(fit: StackFit.expand, children: [
        Image.asset(backgroundAsset,
            key: const Key('homeBackground'),
            fit: BoxFit.cover,
            alignment: const Alignment(.18, 0),
            gaplessPlayback: true,
            excludeFromSemantics: true),
        // The backdrop stays visible; the readable area grows only where text
        // and controls need it. No blur is needed to keep the block edges sharp.
        DecoratedBox(
            decoration: BoxDecoration(
                gradient: LinearGradient(
                    begin: Alignment.topCenter,
                    end: Alignment.bottomCenter,
                    stops: const [
              0,
              .28,
              .58,
              .83,
              1
            ],
                    colors: [
              wash.withOpacity(dark ? .30 : .13),
              wash.withOpacity(.02),
              wash.withOpacity(dark ? .30 : .18),
              wash.withOpacity(dark ? .94 : .93),
              wash.withOpacity(.99),
            ]))),
        DecoratedBox(
            decoration: BoxDecoration(
                gradient: LinearGradient(
                    begin: Alignment.centerLeft,
                    end: Alignment.centerRight,
                    stops: const [
              0,
              .55,
              1
            ],
                    colors: [
              wash.withOpacity(dark ? .28 : .34),
              wash.withOpacity(.07),
              Colors.transparent
            ]))),
        Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          Padding(
              padding: EdgeInsets.fromLTRB(inset, 42, inset + 76, 14),
              child: Row(children: [
                Text(tr('开始'),
                    style: TextStyle(
                        fontSize: 23,
                        fontWeight: FontWeight.w600,
                        color: ink,
                        shadows: [
                          Shadow(color: wash.withOpacity(.85), blurRadius: 12)
                        ])),
                const Spacer(),
                _utilityButton(
                    key: const Key('backgroundButton'),
                    tooltip: tr('更换背景'),
                    icon: Icons.wallpaper_rounded,
                    onPressed: onChangeBackground,
                    wash: wash),
                const SizedBox(width: 6),
                _utilityButton(
                    tooltip: tr('选择游戏目录'),
                    icon: Icons.folder_open_rounded,
                    onPressed: onChooseRoot,
                    wash: wash),
              ])),
          Expanded(
              child: LayoutBuilder(
                  builder: (context, content) => SingleChildScrollView(
                      reverse: true,
                      padding: EdgeInsets.fromLTRB(inset, 20, inset, 20),
                      child: ConstrainedBox(
                          constraints: BoxConstraints(
                              minHeight: (content.maxHeight - 40)
                                  .clamp(0.0, double.infinity)
                                  .toDouble()),
                          child: Column(
                              mainAxisSize: MainAxisSize.min,
                              mainAxisAlignment: MainAxisAlignment.end,
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                SizedBox(
                                    width: summaryWidth,
                                    child: versionSelector),
                                if (versionLabel.isNotEmpty) ...[
                                  const SizedBox(height: 9),
                                  SizedBox(
                                      width: summaryWidth,
                                      child: Row(
                                          crossAxisAlignment:
                                              CrossAxisAlignment.start,
                                          children: [
                                            Padding(
                                                padding: const EdgeInsets.only(
                                                    top: 2),
                                                child: Icon(
                                                    Icons
                                                        .sports_esports_outlined,
                                                    size: 20,
                                                    color: ink)),
                                            const SizedBox(width: 9),
                                            Expanded(
                                                child: Text(versionLabel,
                                                    style: TextStyle(
                                                        color: ink,
                                                        fontSize:
                                                            compact ? 14 : 15,
                                                        height: 1.5))),
                                          ])),
                                ],
                                const SizedBox(height: 20),
                                SizedBox(
                                    width: summaryWidth,
                                    child: Wrap(
                                        spacing: 6,
                                        runSpacing: 4,
                                        children: [
                                          if (onManage != null)
                                            _sceneAction(tr('管理实例'),
                                                Icons.tune_rounded, onManage),
                                          _sceneAction(tr('安装游戏'),
                                              Icons.add_rounded, onInstallGame,
                                              key: const Key(
                                                  'installGameShortcut')),
                                          _sceneAction(
                                              tr('导入整合包'),
                                              Icons.inventory_2_outlined,
                                              onImport),
                                        ])),
                                const SizedBox(height: 18),
                                Row(children: [
                                  Icon(Icons.coffee_outlined,
                                      size: 20, color: muted),
                                  const SizedBox(width: 10),
                                  Expanded(child: javaSelector),
                                  const SizedBox(width: 4),
                                  IconButton(
                                      key: const Key('installJavaShortcut'),
                                      tooltip: tr('安装 Java'),
                                      onPressed: onInstallJava,
                                      icon: const Icon(Icons.download_outlined,
                                          size: 20)),
                                  IconButton(
                                      tooltip: tr('检测本机 Java'),
                                      onPressed: onDetectJava,
                                      icon: const Icon(Icons.refresh_rounded,
                                          size: 20)),
                                  IconButton(
                                      tooltip: tr('选择本地 Java'),
                                      onPressed: onChooseJava,
                                      icon: const Icon(Icons.more_horiz_rounded,
                                          size: 20)),
                                ]),
                                if (error != null) ...[
                                  const SizedBox(height: 6),
                                  ConstrainedBox(
                                      constraints:
                                          const BoxConstraints(maxHeight: 140),
                                      child:
                                          SingleChildScrollView(child: error!)),
                                ],
                              ]))))),
          Padding(
              padding: EdgeInsets.fromLTRB(inset, 8, inset, 26),
              child:
                  Row(crossAxisAlignment: CrossAxisAlignment.center, children: [
                Expanded(
                    child: Text(launchStatus,
                        maxLines: 2,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(
                            color: muted, fontSize: 12, height: 1.6))),
                const SizedBox(width: 18),
                FilledButton.icon(
                    key: const Key('launchButton'),
                    onPressed: canLaunch ? onLaunch : null,
                    style: FilledButton.styleFrom(
                        minimumSize: Size(compact ? 182 : 202, 56),
                        backgroundColor: accent,
                        disabledBackgroundColor:
                            accent.withOpacity(dark ? .18 : .14),
                        disabledForegroundColor: muted,
                        textStyle: const TextStyle(
                            fontSize: 16, fontWeight: FontWeight.w600),
                        shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(12))),
                    icon: launchPending
                        ? const SizedBox(
                            width: 20,
                            height: 20,
                            child: CircularProgressIndicator(strokeWidth: 2))
                        : Icon(
                            gameRunning
                                ? Icons.sports_esports_outlined
                                : Icons.play_arrow_rounded,
                            size: 24),
                    label: Text(tr(gameRunning ? '游戏正在运行' : '启动游戏'))),
              ])),
        ]),
        if (showShelf)
          Positioned(
              key: const Key('homeCarouselArea'),
              top: shelfTop,
              right: compact ? 24 : 32,
              width: shelfWidth,
              height: shelfHeight,
              child: instanceBrowser!),
      ]);
    });
  }

  Widget _sceneAction(String label, IconData icon, VoidCallback? onPressed,
          {Key? key}) =>
      TextButton.icon(
          key: key,
          onPressed: onPressed,
          style: TextButton.styleFrom(
              foregroundColor: ink,
              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
              textStyle:
                  const TextStyle(fontSize: 13, fontWeight: FontWeight.w500)),
          icon: Icon(icon, size: 18),
          label: Text(label));

  Widget _utilityButton(
          {Key? key,
          required String tooltip,
          required IconData icon,
          required VoidCallback? onPressed,
          required Color wash}) =>
      IconButton(
          key: key,
          tooltip: tooltip,
          onPressed: onPressed,
          style: IconButton.styleFrom(
              foregroundColor: ink, backgroundColor: wash.withOpacity(.65)),
          icon: Icon(icon, size: 19));
}
