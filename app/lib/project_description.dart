// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_widget_from_html_core/flutter_widget_from_html_core.dart';
import 'package:html/parser.dart' as html;
import 'package:markdown/markdown.dart' as markdown;
import 'app_widgets.dart';
import 'localization.dart';

String? projectUrl(String value, Uri base) {
  final uri = base.resolve(value.trim());
  final resolved = uri.scheme == 'http' ? uri.replace(scheme: 'https') : uri;
  if (resolved.scheme != 'https' ||
      resolved.host.isEmpty ||
      resolved.userInfo.isNotEmpty) return null;
  final host = resolved.host.toLowerCase();
  final ip = InternetAddress.tryParse(host);
  if (host == 'localhost' ||
      host.endsWith('.localhost') ||
      host.endsWith('.local') ||
      ip != null &&
          (ip.isLoopback ||
              ip.isLinkLocal ||
              ip.address.startsWith('10.') ||
              ip.address.startsWith('192.168.') ||
              RegExp(r'^172\.(1[6-9]|2\d|3[01])\.').hasMatch(ip.address))) {
    return null;
  }
  return resolved.toString();
}

String projectHtml(String body, Uri base) {
  final document = html.parse(markdown.markdownToHtml(body,
      extensionSet: markdown.ExtensionSet.gitHubFlavored, encodeHtml: false));
  for (final element in document.querySelectorAll(
      'script,style,iframe,object,embed,form,input,button,meta,base,link')) {
    element.remove();
  }
  for (final element in document.querySelectorAll('*')) {
    for (final key in element.attributes.keys.toList()) {
      final name = key.toString().toLowerCase();
      if (name.startsWith('on') || name == 'srcset') {
        element.attributes.remove(key);
      }
    }
    if (RegExp(r'url\s*\(|expression\s*\(|@import', caseSensitive: false)
        .hasMatch(element.attributes['style'] ?? '')) {
      element.attributes.remove('style');
    }
    for (final key in ['href', 'src']) {
      if (element.attributes.containsKey(key)) {
        final value = element.attributes[key]!;
        if (key == 'href' && value.startsWith('#')) continue;
        final safe = projectUrl(value, base);
        if (safe == null) {
          element.attributes.remove(key);
        } else {
          element.attributes[key] = safe;
        }
      }
    }
  }
  return document.body?.innerHtml ?? '';
}

class ProjectDescription extends StatefulWidget {
  const ProjectDescription(
      {super.key, required this.body, required this.base, required this.open});
  final String body;
  final Uri base;
  final Future<void> Function(String) open;
  @override
  State<ProjectDescription> createState() => _ProjectDescriptionState();
}

class _ProjectDescriptionState extends State<ProjectDescription> {
  int generation = 0;
  @override
  Widget build(BuildContext context) {
    final content = projectHtml(widget.body, widget.base);
    return Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
      Align(
          alignment: Alignment.centerRight,
          child: TextButton.icon(
              onPressed: () => setState(() => generation++),
              icon: const Icon(Icons.refresh_rounded, size: 16),
              label: Text(tr('重试介绍图片')))),
      Expanded(
          child: SingleChildScrollView(
              child: SurfaceCard(
                  child: SelectionArea(
                      child: HtmlWidget(
                          content.isEmpty
                              ? '<p>${tr('项目未提供详细介绍。')}</p>'
                              : content,
                          key: ValueKey(generation),
                          baseUrl: widget.base,
                          textStyle: TextStyle(
                              fontSize: 14,
                              height: 1.65,
                              color: Theme.of(context).colorScheme.onSurface),
                          customStylesBuilder: (element) {
                            switch (element.localName) {
                              case 'blockquote':
                                return {
                                  'border-left': '4px solid #8194a3',
                                  'padding': '8px 16px',
                                  'margin': '14px 0'
                                };
                              case 'pre':
                                return {
                                  'padding': '14px',
                                  'white-space': 'pre-wrap',
                                  'border': '1px solid #8194a3'
                                };
                              case 'code':
                                return {'font-family': 'monospace'};
                              case 'table':
                                return {
                                  'border-collapse': 'collapse',
                                  'width': '100%'
                                };
                              case 'th':
                              case 'td':
                                return {
                                  'border': '1px solid #8194a3',
                                  'padding': '8px'
                                };
                              case 'img':
                                return {'max-width': '100%', 'height': 'auto'};
                              case 'hr':
                                return {'border': '1px solid #8194a3'};
                            }
                            return null;
                          },
                          onTapUrl: (url) async {
                            final safe = projectUrl(url, widget.base);
                            if (safe == null) return false;
                            await widget.open(safe);
                            return true;
                          },
                          onErrorBuilder: (context, element, error) => Padding(
                              padding: const EdgeInsets.all(10),
                              child: Text(tr('图片加载失败，可重试或打开项目主页。'),
                                  style:
                                      TextStyle(fontSize: 12, color: muted))),
                          onLoadingBuilder: (context, element, progress) =>
                              const Padding(
                                  padding: EdgeInsets.all(12),
                                  child: SizedBox(
                                      width: 18,
                                      height: 18,
                                      child: CircularProgressIndicator(
                                          strokeWidth: 2))))))))
    ]);
  }
}
