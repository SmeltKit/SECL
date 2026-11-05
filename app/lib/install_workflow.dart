// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';
import 'resource_market.dart' show InstallPlanDialog, enrichModPlanProjects;

Future<bool> reviewAndInstall(
    BuildContext context, ResourceEngine engine, Data plan,
    {String kind = 'mod',
    String? applyOperation,
    VoidCallback? onApplying,
    void Function(Data)? onResult}) async {
  if (kind == 'mod') await enrichModPlanProjects(engine, plan);
  if (!context.mounted) return false;
  final choice = await showDialog<Data>(
      context: context,
      barrierDismissible: false,
      builder: (_) => InstallPlanDialog(plan: plan, kind: kind));
  if (choice == null) return false;
  onApplying?.call();
  var selected = plan;
  if (kind == 'mod') {
    selected = Map<String, dynamic>.from(await engine.run({
      'op': 'mods.select',
      'plan': {...plan, 'disable_files': choice['disable']},
      'selected': choice['selected'],
      'redownload': choice['redownload'],
      'enable': choice['enable']
    }) as Map);
    selected['disable_files'] = choice['disable'];
  } else {
    selected = {...plan, 'replace': choice['replace'] == true};
  }
  final response = await engine.run({
    'op': applyOperation ?? (kind == 'mod' ? 'mods.apply' : 'packs.apply'),
    'plan': selected
  }, title: applyOperation == 'updates.apply' ? '正在更新资源' : '正在安装资源');
  if (response is Map) onResult?.call(Map<String, dynamic>.from(response));
  return true;
}
