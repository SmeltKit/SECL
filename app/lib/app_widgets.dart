// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'localization.dart';
import 'package:flutter/material.dart';

const ink = Color(0xFF243347);
const muted = Color(0xFF697789);
const accent = Color(0xFF365F7C);
const line = Color(0xFFE2E7ED);

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
      decoration: const BoxDecoration(
          color: Colors.white, border: Border(top: BorderSide(color: line))),
      child: child);
}

class SurfaceCard extends StatelessWidget {
  const SurfaceCard(
      {super.key,
      required this.child,
      this.padding = const EdgeInsets.all(16),
      this.color = Colors.white});
  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color color;
  @override
  Widget build(BuildContext context) => Container(
      padding: padding,
      decoration: BoxDecoration(
          color: color,
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
      color: const Color(0xFFEBF0F5),
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
                  color: const Color(0xFFFFF6F1),
                  child: Column(mainAxisSize: MainAxisSize.min, children: [
                    const Icon(Icons.cloud_off_outlined,
                        color: Color(0xFFAD6342), size: 30),
                    const SizedBox(height: 12),
                    Text(errorMessage(error),
                        textAlign: TextAlign.center,
                        style: const TextStyle(fontSize: 14, height: 1.5)),
                    const SizedBox(height: 8),
                    SelectableText(
                        '${error['code'] ?? ''}${(error['http_status'] as num? ?? 0) > 0 ? ' · HTTP ${error['http_status']}' : ''}',
                        style: const TextStyle(color: muted, fontSize: 12)),
                    if ((error['url'] ?? '').toString().isNotEmpty)
                      Padding(
                          padding: const EdgeInsets.only(top: 8),
                          child: SelectableText(error['url'].toString(),
                              style:
                                  const TextStyle(color: muted, fontSize: 11))),
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
          Text(label!, style: const TextStyle(color: muted, fontSize: 13))
        ]
      ]));
}

String byteLabel(num n) => n >= 1048576
    ? '${(n / 1048576).toStringAsFixed(1)} MiB'
    : '${(n / 1024).toStringAsFixed(0)} KiB';
