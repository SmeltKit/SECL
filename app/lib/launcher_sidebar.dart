// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'localization.dart';

/// Shared navigation shell. Account actions stay in the identity area;
/// download activity is available from the global top-right task button.
class LauncherSidebar extends StatelessWidget {
  const LauncherSidebar(
      {super.key,
      required this.items,
      required this.profile,
      required this.settings,
      required this.onAbout});
  final List<Widget> items;
  final Widget profile, settings;
  final VoidCallback onAbout;

  @override
  Widget build(BuildContext context) {
    final dark = AppAppearance.instance.dark;
    final compact = MediaQuery.sizeOf(context).height < 680;
    return SizedBox(
        width: 192,
        child: DecoratedBox(
            decoration: BoxDecoration(
                gradient: LinearGradient(
                    begin: Alignment.topLeft,
                    end: Alignment.bottomRight,
                    colors: dark
                        ? [const Color(0xff222332), const Color(0xff1c2532)]
                        : [const Color(0xffeeebf8), const Color(0xfff3f1fa)])),
            child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Padding(
                      padding: EdgeInsets.fromLTRB(
                          22,
                          Platform.isMacOS ? (compact ? 45 : 55) : 30,
                          16,
                          compact ? 20 : 26),
                      child: Tooltip(
                          message: tr('关于 SECL'),
                          child: InkWell(
                              onTap: onAbout,
                              borderRadius: BorderRadius.circular(10),
                              child: Padding(
                                  padding:
                                      const EdgeInsets.symmetric(vertical: 6),
                                  child: Row(children: [
                                    Image.asset('assets/branding/secl-128.png',
                                        key: const Key('sidebarBrandMark'),
                                        width: 34,
                                        height: 36,
                                        fit: BoxFit.contain,
                                        filterQuality: FilterQuality.high,
                                        excludeFromSemantics: true),
                                    const SizedBox(width: 11),
                                    Text('SECL',
                                        style: TextStyle(
                                            fontSize: 23,
                                            fontWeight: FontWeight.w700,
                                            letterSpacing: .2,
                                            color: ink))
                                  ]))))),
                  Expanded(
                      child: SingleChildScrollView(
                          padding: const EdgeInsets.symmetric(horizontal: 10),
                          child: Column(children: items))),
                  Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 10),
                      child: settings),
                  Padding(
                      padding: const EdgeInsets.fromLTRB(18, 6, 18, 18),
                      child: profile),
                ])));
  }
}

class SidebarNavItem extends StatelessWidget {
  const SidebarNavItem(
      {super.key,
      required this.label,
      required this.leading,
      required this.onTap,
      this.selected = false,
      this.trailing});
  final String label;
  final Widget leading;
  final Widget? trailing;
  final VoidCallback onTap;
  final bool selected;
  @override
  Widget build(BuildContext context) => Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: AnimatedContainer(
          duration: const Duration(milliseconds: 150),
          decoration: BoxDecoration(
              color: selected ? accent.withOpacity(.17) : Colors.transparent,
              borderRadius: BorderRadius.circular(13)),
          child: Material(
              color: Colors.transparent,
              child: InkWell(
                  onTap: onTap,
                  borderRadius: BorderRadius.circular(13),
                  child: Padding(
                      padding: EdgeInsets.symmetric(
                          horizontal: 12,
                          vertical: MediaQuery.sizeOf(context).height < 680
                              ? 8.5
                              : 11),
                      child: Row(children: [
                        SizedBox(
                            width: 22,
                            height: 22,
                            child: Center(child: leading)),
                        const SizedBox(width: 12),
                        Expanded(
                            child: Text(label,
                                style: TextStyle(
                                    fontSize: 15,
                                    height: 1.4,
                                    fontWeight: selected
                                        ? FontWeight.w600
                                        : FontWeight.w500,
                                    color: selected ? accent : ink))),
                        if (trailing != null) ...[
                          const SizedBox(width: 3),
                          trailing!
                        ]
                      ]))))));
}
