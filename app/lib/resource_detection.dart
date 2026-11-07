// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'appearance.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';

const _providerNames = {'modrinth': 'Modrinth', 'curseforge': 'CurseForge'};

/// Keeps file identity, website identity and instance checks independent.
class ResourceDetection {
  ResourceDetection(Data row) {
    final value = row['detection'];
    final detection = value is Map ? value : const {};
    final problem = row['problem']?.toString() ?? '';
    local = detection['local']?.toString() ??
        (row['valid_archive'] == false
            ? 'invalid'
            : row['known'] == true
                ? 'recognized'
                : 'unknown');
    source = detection['source']?.toString() ??
        ((row['project_id']?.toString() ?? '').isNotEmpty
            ? 'matched'
            : 'not_checked');
    providers = {
      if (_providerNames[row['source']] case final provider?) provider,
      if (row['sources'] case final List sources)
        for (final id in sources)
          if (_providerNames[id] case final provider?) provider,
    };
    issues = dataRows(detection['issues']);
    errors = (row['errors'] as List? ?? []).map((e) => e.toString()).toList();
    warnings =
        (row['warnings'] as List? ?? []).map((e) => e.toString()).toList();
    if (problem.isNotEmpty) warnings.add(problem);
    final hasErrors = errors.isNotEmpty ||
        issues.any((issue) => issue['severity'] == 'error');
    final hasWarnings = warnings.isNotEmpty ||
        issues.any((issue) => issue['severity'] == 'warning');
    compatibility = row['enabled'] == false
        ? 'disabled'
        : hasErrors || detection['compatibility'] == 'conflict'
            ? 'conflict'
            : hasWarnings || local != 'recognized'
                ? 'needs_review'
                : detection['compatibility']?.toString() ?? 'needs_review';
  }

  late final String local, source, compatibility;
  late final Set<String> providers;
  late final List<Data> issues;
  late final List<String> errors, warnings;

  bool get needsAttention =>
      compatibility == 'conflict' ||
      compatibility == 'needs_review' ||
      local != 'recognized' ||
      source == 'failed' ||
      issues.isNotEmpty ||
      errors.isNotEmpty ||
      warnings.isNotEmpty;

  String get label => switch (compatibility) {
        'conflict' => tr('存在冲突'),
        'disabled' => tr('已禁用'),
        'no_known_conflict' => tr('未发现已知冲突'),
        _ => tr('需核实'),
      };
  String get localLabel => switch (local) {
        'recognized' => tr('本地已识别'),
        'invalid' => tr('本地文件无效'),
        _ => tr('本地元数据未知'),
      };
  String get sourceLabel => switch (source) {
        'matched' => providers.isEmpty ? tr('来源已匹配') : providers.join(' / '),
        'unmatched' => tr('来源未匹配'),
        'failed' => tr('来源查询失败'),
        _ => tr('来源未查询'),
      };
  String get details => [
        label,
        '$localLabel · $sourceLabel',
        if (compatibility == 'no_known_conflict') tr('静态检查未发现已知冲突，仍需在游戏内验证。'),
        for (final issue in issues)
          [
            if ((issue['code']?.toString() ?? '').isNotEmpty) issue['code'],
            nativeText(issue['message']?.toString() ?? ''),
            if ((issue['files'] as List? ?? []).isNotEmpty)
              (issue['files'] as List).join(', ')
          ].join(' · '),
        ...{...errors, ...warnings}.map(nativeText)
      ].join('\n');
}

class ResourceDetectionStatus extends StatelessWidget {
  const ResourceDetectionStatus(this.row, {super.key});
  final Data row;

  @override
  Widget build(BuildContext context) {
    final detection = ResourceDetection(row);
    final color = switch (detection.compatibility) {
      'conflict' => seclColor(0xFFAD4343),
      'disabled' => muted,
      'no_known_conflict' => seclColor(0xFF476175),
      _ => seclColor(0xFFB0782F),
    };
    return Tooltip(
        message: detection.details,
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(detection.label,
              softWrap: false,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(color: color, fontSize: 12)),
          const SizedBox(height: 4),
          Wrap(spacing: 6, runSpacing: 2, children: [
            for (final label in [detection.localLabel, detection.sourceLabel])
              Text(label,
                  softWrap: false,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(color: muted, fontSize: 10, height: 1.4))
          ])
        ]));
  }
}

class ResourceSourceFailure extends StatelessWidget {
  const ResourceSourceFailure(this.errors, {super.key, this.retry});
  final List<Data> errors;
  final VoidCallback? retry;

  @override
  Widget build(BuildContext context) => SurfaceCard(
      color: seclColor(0xFFFFF6EA),
      child: Row(crossAxisAlignment: CrossAxisAlignment.start, children: [
        Icon(Icons.cloud_off_outlined, color: seclColor(0xFFB0782F), size: 20),
        const SizedBox(width: 10),
        Expanded(
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Text(tr('来源查询失败，本地资源仍可查看。'), style: const TextStyle(fontSize: 12)),
          for (final error in errors) ...[
            const SizedBox(height: 4),
            Text(
                [
                  if (_providerNames[error['source']] case final provider?)
                    provider,
                  errorMessage(error),
                ].join(' · '),
                style: TextStyle(color: muted, fontSize: 12)),
            SelectableText(
                '${error['code'] ?? ''}${(error['http_status'] as num? ?? 0) > 0 ? ' · HTTP ${error['http_status']}' : ''}',
                style: TextStyle(color: muted, fontSize: 11)),
            if ((error['url']?.toString() ?? '').isNotEmpty)
              SelectableText(error['url'].toString(),
                  style: TextStyle(color: muted, fontSize: 10))
          ]
        ])),
        const SizedBox(width: 8),
        TextButton(onPressed: retry, child: Text(tr('重试来源查询')))
      ]));
}
