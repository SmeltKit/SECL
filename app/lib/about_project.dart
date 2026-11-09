// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:math' as math;
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'localization.dart';

Future<void> showProjectAboutDialog(BuildContext context) => showDialog<void>(
    context: context, builder: (_) => const _ProjectAboutDialog());

Future<void> showProjectLicensesDialog(BuildContext context) =>
    showDialog<void>(
        context: context, builder: (_) => const _ProjectLicensesDialog());

class _ProjectAboutDialog extends StatelessWidget {
  const _ProjectAboutDialog();

  @override
  Widget build(BuildContext context) => Dialog(
      key: const Key('projectAboutDialog'),
      backgroundColor: panelSurface,
      surfaceTintColor: Colors.transparent,
      insetPadding: const EdgeInsets.all(24),
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
      child: ConstrainedBox(
          constraints: BoxConstraints(
              maxWidth: 500,
              maxHeight: math.max(120, MediaQuery.sizeOf(context).height - 48)),
          child: SizedBox(
              width: 500,
              child: Column(mainAxisSize: MainAxisSize.min, children: [
                Padding(
                    padding: const EdgeInsets.fromLTRB(24, 24, 16, 20),
                    child: Row(children: [
                      Image.asset('assets/branding/secl-128.png',
                          key: const Key('aboutBrandMark'),
                          width: 56,
                          height: 56,
                          fit: BoxFit.contain,
                          excludeFromSemantics: true),
                      const SizedBox(width: 16),
                      Expanded(
                          child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              mainAxisSize: MainAxisSize.min,
                              children: [
                            Text('SECL',
                                style: TextStyle(
                                    fontSize: 27,
                                    height: 1.2,
                                    fontWeight: FontWeight.w600,
                                    letterSpacing: -.5,
                                    color: ink)),
                            const SizedBox(height: 6),
                            Text(tr('0.5.0 · 测试版'),
                                style: TextStyle(
                                    fontSize: 12, height: 1.5, color: muted)),
                          ])),
                      IconButton(
                          key: const Key('closeAbout'),
                          tooltip: tr('关闭'),
                          onPressed: () => Navigator.pop(context),
                          icon:
                              Icon(Icons.close_rounded, color: muted, size: 20))
                    ])),
                Divider(height: 1, color: line),
                Flexible(
                    child: SingleChildScrollView(
                        padding: const EdgeInsets.fromLTRB(24, 20, 24, 0),
                        child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text('SUPREME ETERNITY CRAFT LAUNCHER',
                                  style: TextStyle(
                                      fontSize: 12.5,
                                      height: 1.6,
                                      fontWeight: FontWeight.w500,
                                      letterSpacing: .1,
                                      color: ink)),
                              const SizedBox(height: 20),
                              _AboutInfo(
                                  icon: Icons.source_outlined,
                                  title: tr('开源许可'),
                                  description: tr('GPL-3.0-only + 第 7 节附加条款')),
                              const SizedBox(height: 16),
                              _AboutInfo(
                                  icon: Icons.account_tree_outlined,
                                  title: tr('项目来源'),
                                  description: tr('基于 M²CL 迁移与重写')),
                              const SizedBox(height: 20),
                              Text('Copyright © 2026 ppm203',
                                  style: TextStyle(
                                      fontSize: 12, height: 1.6, color: muted)),
                              const SizedBox(height: 8),
                              Text(tr('非官方项目，与 Mojang 或 Microsoft 无隶属或背书关系。'),
                                  style: TextStyle(
                                      fontSize: 11.5,
                                      height: 1.6,
                                      color: muted)),
                            ]))),
                Padding(
                    padding: const EdgeInsets.fromLTRB(24, 22, 24, 22),
                    child: Align(
                        alignment: Alignment.centerRight,
                        child: Wrap(
                            spacing: 10,
                            runSpacing: 10,
                            alignment: WrapAlignment.end,
                            children: [
                              OutlinedButton.icon(
                                  key: const Key('viewProjectLicenses'),
                                  onPressed: () =>
                                      showProjectLicensesDialog(context),
                                  icon: const Icon(Icons.description_outlined,
                                      size: 17),
                                  label: Text(tr('查看许可证'))),
                              FilledButton(
                                  onPressed: () => Navigator.pop(context),
                                  child: Text(tr('关闭')))
                            ])))
              ]))));
}

class _AboutInfo extends StatelessWidget {
  const _AboutInfo(
      {required this.icon, required this.title, required this.description});
  final IconData icon;
  final String title, description;

  @override
  Widget build(BuildContext context) =>
      Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Padding(
            padding: const EdgeInsets.only(top: 2),
            child: Icon(icon, size: 19, color: accent)),
        const SizedBox(width: 12),
        Expanded(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(title,
              style: TextStyle(
                  fontSize: 12.5,
                  height: 1.5,
                  fontWeight: FontWeight.w600,
                  color: ink)),
          const SizedBox(height: 3),
          Text(description,
              style: TextStyle(fontSize: 12, height: 1.6, color: muted)),
        ]))
      ]);
}

class _ProjectLicense {
  const _ProjectLicense(this.packages, this.paragraphs);
  final List<String> packages;
  final List<LicenseParagraph> paragraphs;
}

class _ProjectLicensesDialog extends StatefulWidget {
  const _ProjectLicensesDialog();
  @override
  State<_ProjectLicensesDialog> createState() => _ProjectLicensesDialogState();
}

class _ProjectLicensesDialogState extends State<_ProjectLicensesDialog> {
  final _search = TextEditingController();
  final _readerScroll = ScrollController();
  final Map<String, List<_ProjectLicense>> _licenses = {};
  bool _loading = true;
  bool _failed = false;
  String? _selected;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    setState(() {
      _loading = true;
      _failed = false;
    });
    final grouped = <String, List<_ProjectLicense>>{};
    try {
      await for (final entry in LicenseRegistry.licenses) {
        final record = _ProjectLicense(entry.packages.toList(growable: false),
            entry.paragraphs.toList(growable: false));
        final packages =
            record.packages.isEmpty ? [tr('其他组件')] : record.packages;
        for (final package in packages) {
          (grouped[package] ??= []).add(record);
        }
      }
      if (!mounted) return;
      setState(() {
        _licenses
          ..clear()
          ..addAll(grouped);
        _loading = false;
      });
    } catch (_) {
      if (mounted) {
        setState(() {
          _loading = false;
          _failed = true;
        });
      }
    }
  }

  @override
  void dispose() {
    _search.dispose();
    _readerScroll.dispose();
    super.dispose();
  }

  Widget _contents() {
    if (_loading) {
      return const Center(
          child: SizedBox(
              width: 24,
              height: 24,
              child: CircularProgressIndicator(strokeWidth: 2)));
    }
    if (_failed) {
      return Center(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
        Icon(Icons.description_outlined, size: 30, color: muted),
        const SizedBox(height: 12),
        Text(tr('无法加载许可证')),
        const SizedBox(height: 12),
        OutlinedButton.icon(
            onPressed: _load,
            icon: const Icon(Icons.refresh_rounded, size: 17),
            label: Text(tr('重试')))
      ]));
    }
    if (_selected != null) return _reader(_selected!);
    final query = _search.text.trim().toLowerCase();
    final packages = _licenses.keys
        .where((name) => name.toLowerCase().contains(query))
        .toList()
      ..sort((a, b) => a.toLowerCase().compareTo(b.toLowerCase()));
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Padding(
          padding: const EdgeInsets.fromLTRB(22, 18, 22, 12),
          child: TextField(
              key: const Key('projectLicenseSearch'),
              controller: _search,
              onChanged: (_) => setState(() {}),
              decoration: InputDecoration(
                  hintText: tr('搜索组件名称…'),
                  prefixIcon:
                      Icon(Icons.search_rounded, size: 19, color: muted),
                  suffixIcon: query.isEmpty
                      ? null
                      : IconButton(
                          tooltip: tr('清除搜索'),
                          onPressed: () => setState(_search.clear),
                          icon: const Icon(Icons.close_rounded, size: 18))))),
      Padding(
          padding: const EdgeInsets.fromLTRB(24, 0, 24, 10),
          child: Text(tr('{0} 个组件', [packages.length]),
              style: TextStyle(fontSize: 11.5, color: muted))),
      Expanded(
          child: packages.isEmpty
              ? Center(
                  child: Text(tr('没有匹配的组件'), style: TextStyle(color: muted)))
              : ListView.separated(
                  key: const Key('projectLicenseList'),
                  padding: const EdgeInsets.fromLTRB(12, 0, 12, 14),
                  itemCount: packages.length,
                  separatorBuilder: (_, __) => Padding(
                      padding: const EdgeInsets.symmetric(horizontal: 12),
                      child: Divider(height: 1, color: line.withOpacity(.65))),
                  itemBuilder: (_, index) {
                    final package = packages[index];
                    return ListTile(
                        key: ValueKey('project-license-$package'),
                        contentPadding: const EdgeInsets.symmetric(
                            horizontal: 12, vertical: 5),
                        shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(8)),
                        leading:
                            Icon(Icons.code_rounded, size: 21, color: accent),
                        title: Text(package,
                            style: TextStyle(
                                fontSize: 13.5,
                                height: 1.45,
                                fontWeight: FontWeight.w500,
                                color: ink)),
                        trailing: Icon(Icons.chevron_right_rounded,
                            size: 19, color: muted),
                        onTap: () => setState(() => _selected = package));
                  }))
    ]);
  }

  Widget _reader(String package) => Scrollbar(
      controller: _readerScroll,
      child: SingleChildScrollView(
          controller: _readerScroll,
          key: ValueKey('project-license-reader-$package'),
          padding: const EdgeInsets.fromLTRB(24, 20, 24, 28),
          child:
              Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            for (final (index, license) in _licenses[package]!.indexed) ...[
              if (index > 0) ...[
                Padding(
                    padding: const EdgeInsets.symmetric(vertical: 20),
                    child: Divider(height: 1, color: line)),
              ],
              Text(license.packages.join(' · '),
                  style: TextStyle(
                      fontSize: 11.5,
                      height: 1.6,
                      fontWeight: FontWeight.w600,
                      color: muted)),
              const SizedBox(height: 16),
              for (final paragraph in license.paragraphs)
                Padding(
                    padding: EdgeInsets.only(
                        left:
                            paragraph.indent < 0 ? 0 : 16.0 * paragraph.indent,
                        bottom: 12),
                    child: SelectableText(paragraph.text,
                        textAlign: paragraph.indent < 0
                            ? TextAlign.center
                            : TextAlign.start,
                        style: TextStyle(
                            fontSize: 12.5, height: 1.7, color: ink))),
            ]
          ])));

  @override
  Widget build(BuildContext context) {
    final screen = MediaQuery.sizeOf(context);
    return Dialog(
        key: const Key('projectLicensesDialog'),
        backgroundColor: panelSurface,
        surfaceTintColor: Colors.transparent,
        insetPadding: const EdgeInsets.all(24),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(16)),
        child: SizedBox(
            width: math.min(720, screen.width - 48),
            height: math.min(600, screen.height - 48),
            child: Column(children: [
              Padding(
                  padding: const EdgeInsets.fromLTRB(16, 12, 12, 12),
                  child: Row(children: [
                    if (_selected != null)
                      IconButton(
                          key: const Key('backToProjectLicenses'),
                          tooltip: tr('返回'),
                          onPressed: () {
                            if (_readerScroll.hasClients) {
                              _readerScroll.jumpTo(0);
                            }
                            setState(() => _selected = null);
                          },
                          icon: Icon(Icons.arrow_back_rounded,
                              size: 20, color: ink))
                    else
                      Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 10),
                          child: Icon(Icons.description_outlined,
                              color: accent, size: 22)),
                    const SizedBox(width: 8),
                    Expanded(
                        child: Text(_selected ?? tr('许可证与第三方声明'),
                            maxLines: 2,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                                fontSize: 18,
                                height: 1.4,
                                fontWeight: FontWeight.w600,
                                color: ink))),
                    const SizedBox(width: 12),
                    IconButton(
                        key: const Key('closeProjectLicenses'),
                        tooltip: tr('关闭'),
                        onPressed: () => Navigator.pop(context),
                        icon: Icon(Icons.close_rounded, color: muted, size: 20))
                  ])),
              Divider(height: 1, color: line),
              Expanded(child: _contents()),
            ])));
  }
}
