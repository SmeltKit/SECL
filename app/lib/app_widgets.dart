import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'localization.dart';
import 'package:flutter/material.dart';

Color get ink => AppAppearance.instance.dark
    ? const Color(0xffe5ebf1)
    : const Color(0xff243347);
Color get muted => AppAppearance.instance.dark
    ? const Color(0xffa5b3c3)
    : const Color(0xff697789);
Color get accent => AppAppearance.instance.tone == 'forest'
    ? (AppAppearance.instance.dark
        ? const Color(0xff83d1b0)
        : const Color(0xff26765c))
    : AppAppearance.instance.tone == 'violet'
        ? (AppAppearance.instance.dark
            ? const Color(0xffc4adf0)
            : const Color(0xff72549d))
        : (AppAppearance.instance.dark
            ? const Color(0xff9fc8e4)
            : const Color(0xff365f7c));
Color get line => AppAppearance.instance.dark
    ? const Color(0xff3a4858)
    : const Color(0xffe2e7ed);

class FooterBar extends StatelessWidget {
  const FooterBar(
      {super.key, required this.child, this.horizontalPadding = 28});
  final Widget child;
  final double horizontalPadding;
  @override
  Widget build(BuildContext context) => Container(
      height: 88,
      alignment: Alignment.center,
      padding: EdgeInsets.symmetric(horizontal: horizontalPadding),
      decoration: BoxDecoration(
          color: panelSurface, border: Border(top: BorderSide(color: line))),
      child: child);
}

class SurfaceCard extends StatelessWidget {
  const SurfaceCard(
      {super.key,
      required this.child,
      this.padding = const EdgeInsets.all(16),
      this.color});
  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color? color;
  @override
  Widget build(BuildContext context) => Container(
      padding: padding,
      decoration: BoxDecoration(
          color: color ?? panelSurface,
          border: Border.all(color: line),
          borderRadius: BorderRadius.circular(10)),
      child: child);
}

class ResourceImage extends StatelessWidget {
  const ResourceImage(this.url,
      {super.key, this.size = 56, this.icon = Icons.inventory_2_outlined});
  final String? url;
  final double size;
  final IconData icon;
  @override
  Widget build(BuildContext context) => ClipRRect(
      borderRadius: BorderRadius.circular(9),
      child: SizedBox(
          width: size,
          height: size,
          child: url != null && url!.startsWith('https://')
              ? Image.network(url!,
                  fit: BoxFit.cover,
                  cacheWidth: (size * 2).round(),
                  errorBuilder: (_, __, ___) => fallback())
              : fallback()));
  Widget fallback() => ColoredBox(
      color: seclColor(0xFFEBF0F5),
      child: Icon(icon, size: size * .45, color: accent));
}

class ProblemPanel extends StatelessWidget {
  const ProblemPanel(this.error, {super.key, this.retry});
  final Map<String, dynamic> error;
  final VoidCallback? retry;
  @override
  Widget build(BuildContext context) => Center(
      child: SingleChildScrollView(
          padding: const EdgeInsets.all(20),
          child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 520),
              child: SurfaceCard(
                  color: seclColor(0xFFFFF6F1),
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                    Icon(Icons.cloud_off_outlined,
                        color: seclColor(0xFFAD6342), size: 30),
                    const SizedBox(height: 12),
                    Text(errorMessage(error),
                        textAlign: TextAlign.center,
                        style: const TextStyle(fontSize: 14, height: 1.5)),
                    const SizedBox(height: 8),
                    SelectableText(
                        '${error['code'] ?? ''}${(error['http_status'] as num? ?? 0) > 0 ? ' · HTTP ${error['http_status']}' : ''}',
                        style: TextStyle(color: muted, fontSize: 12)),
                    if ((error['url'] ?? '').toString().isNotEmpty)
                      Padding(
                          padding: const EdgeInsets.only(top: 8),
                          child: SelectableText(error['url'].toString(),
                              style: TextStyle(color: muted, fontSize: 11))),
                    if (retry != null)
                      Padding(
                          padding: const EdgeInsets.only(top: 14),
                          child: OutlinedButton.icon(
                              onPressed: retry,
                              icon: const Icon(Icons.refresh_rounded, size: 17),
                              label: Text(tr("重试"))))
                  ])))));
}

class BusyPanel extends StatelessWidget {
  const BusyPanel({super.key, this.label});
  final String? label;
  @override
  Widget build(BuildContext context) => Center(
          child: Column(mainAxisSize: MainAxisSize.min, children: [
        const SizedBox(
            width: 26,
            height: 26,
            child: CircularProgressIndicator(strokeWidth: 2.5)),
        if (label != null) ...[
          const SizedBox(height: 16),
          Text(label!, style: TextStyle(color: muted, fontSize: 13))
        ]
      ]));
}

String byteLabel(num n) => n >= 1048576
    ? '${(n / 1048576).toStringAsFixed(1)} MiB'
    : '${(n / 1024).toStringAsFixed(0)} KiB';
