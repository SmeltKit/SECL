// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';
import 'resource_market.dart' show InstallPlanDialog;

Future<bool> reviewAndInstall(
    BuildContext context, ResourceEngine engine, Data plan,
    {String kind = 'mod'}) async {
  if (kind == 'mod') {
    final ids = <String>{
      for (final f in dataRows(plan['files'])) f['project_id'] as String,
      for (final r in dataRows(plan['reuse'])) r['project_id'] as String
    };
    if (ids.isNotEmpty) {
      try {
        final projects = dataRows(
            await engine.run({'op': 'resource.projects', 'ids': ids.toList()}));
        for (final dynamic file in plan['files'] as List? ?? []) {
          final match =
              projects.where((p) => p['id'] == file['project_id']).firstOrNull;
          if (match != null) {
            file['title'] = match['title'];
            file['icon_url'] = match['icon_url'];
          }
        }
        for (final dynamic reuse in plan['reuse'] as List? ?? []) {
          final match =
              projects.where((p) => p['id'] == reuse['project_id']).firstOrNull;
          if (match != null) {
            reuse['file'] = {
              ...Map<String, dynamic>.from(reuse['file'] as Map),
              'title': match['title'],
              'icon_url': match['icon_url']
            };
          }
        }
      } catch (_) {
        /* Optional project images never invalidate a verified plan. */
      }
    }
  }
  if (!context.mounted) return false;
  final choice = await showDialog<Data>(
      context: context,
      barrierDismissible: false,
      builder: (_) => InstallPlanDialog(plan: plan, kind: kind));
  if (choice == null) return false;
  var selected = plan;
  if (kind == 'mod') {
    selected = Map<String, dynamic>.from(await engine.run({
      'op': 'mods.select',
      'plan': plan,
      'selected': choice['selected'],
      'redownload': choice['redownload'],
      'enable': choice['enable']
    }) as Map);
    selected['disable_files'] = choice['disable'];
  } else {
    selected = {...plan, 'replace': choice['replace'] == true};
  }
  await engine.run(
      {'op': kind == 'mod' ? 'mods.apply' : 'packs.apply', 'plan': selected});
  return true;
}
