// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'launcher_icons.dart';
import 'localization.dart';
import 'resource_engine.dart';
import 'task_panel.dart';

class DownloadTaskButton extends StatelessWidget {
  const DownloadTaskButton({super.key, required this.engine});
  final ResourceEngine engine;

  Future<void> _open(BuildContext context) => showGeneralDialog<void>(
      context: context,
      barrierDismissible: true,
      barrierLabel: tr('关闭'),
      barrierColor: Colors.black.withOpacity(.06),
      transitionDuration: const Duration(milliseconds: 160),
      transitionBuilder: (context, animation, secondary, child) =>
          FadeTransition(
              opacity: animation,
              child: SlideTransition(
                  position: Tween<Offset>(
                          begin: const Offset(0, -.025), end: Offset.zero)
                      .animate(animation),
                  child: child)),
      pageBuilder: (context, _, __) {
        final size = MediaQuery.sizeOf(context);
        return SafeArea(
            child: Stack(children: [
          Positioned(
              top: 84,
              right: 16,
              width: math.min(420, size.width - 32),
              child: ConstrainedBox(
                  constraints: BoxConstraints(
                      maxHeight: math.min(560, size.height - 76)),
                  child: TaskPanel(engine: engine))),
        ]));
      });

  @override
  Widget build(BuildContext context) => AnimatedBuilder(
      animation: engine,
      builder: (context, _) {
        final count = engine.activeDownloadCount;
        final title = engine.activeDownloadTitle;
        return Semantics(
            button: true,
            label: tr('下载任务'),
            child: Tooltip(
                excludeFromSemantics: true,
                message: title.isEmpty
                    ? tr('下载任务')
                    : '${tr('下载任务')} · ${nativeText(title)}',
                child: Material(
                    color: count > 0
                        ? panelSurface.withOpacity(.92)
                        : Colors.transparent,
                    borderRadius: BorderRadius.circular(9),
                    child: InkWell(
                      key: const Key('downloadTasksButton'),
                      onTap: () => _open(context),
                      borderRadius: BorderRadius.circular(9),
                      child: Padding(
                          padding: const EdgeInsets.symmetric(
                              horizontal: 9, vertical: 7),
                          child: Row(mainAxisSize: MainAxisSize.min, children: [
                            LauncherIcon(LauncherIconKind.download,
                                size: 20, color: count > 0 ? accent : ink),
                            if (count > 0) ...[
                              const SizedBox(width: 7),
                              Container(
                                  key: const Key('downloadTasksCount'),
                                  constraints: const BoxConstraints(
                                      minWidth: 18, minHeight: 18),
                                  padding:
                                      const EdgeInsets.symmetric(horizontal: 5),
                                  alignment: Alignment.center,
                                  decoration: BoxDecoration(
                                      color: accent,
                                      borderRadius: BorderRadius.circular(9)),
                                  child: Text('$count',
                                      style: TextStyle(
                                          fontSize: 10,
                                          height: 1.3,
                                          color: AppAppearance.instance.dark
                                              ? const Color(0xff142532)
                                              : Colors.white))),
                            ],
                          ])),
                    ))));
      });
}
