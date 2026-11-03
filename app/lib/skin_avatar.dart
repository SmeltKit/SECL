// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';

bool isOfficialSkinUrl(String value) =>
    RegExp(r'^https://textures\.minecraft\.net/texture/[0-9a-fA-F]{32,128}$')
        .hasMatch(value);

/// Fetches the public texture only. No player name, UUID or bearer token is sent.
Future<Uint8List> loadSkinTexture(Uri url) async {
  if (!isOfficialSkinUrl(url.toString())) {
    throw const FormatException('Invalid skin URL');
  }
  final client = HttpClient()..connectionTimeout = const Duration(seconds: 6);
  try {
    return await (() async {
      final request = await client.getUrl(url);
      request.followRedirects = false;
      final response = await request.close();
      if (response.statusCode != HttpStatus.ok ||
          response.contentLength > 1048576) {
        throw const FormatException('Skin unavailable');
      }
      final buffer = BytesBuilder(copy: false);
      await for (final chunk in response) {
        if (buffer.length + chunk.length > 1048576) {
          throw const FormatException('Skin too large');
        }
        buffer.add(chunk);
      }
      return buffer.takeBytes();
    })()
        .timeout(const Duration(seconds: 10));
  } finally {
    client.close(force: true);
  }
}

typedef SkinLoader = Future<Uint8List> Function(Uri);

class SkinAvatar extends StatefulWidget {
  const SkinAvatar(
      {super.key,
      required this.skinUrl,
      this.size = 42,
      this.loader = loadSkinTexture});
  final String? skinUrl;
  final double size;
  @visibleForTesting
  final SkinLoader loader;
  @override
  State<SkinAvatar> createState() => _SkinAvatarState();
}

class _SkinAvatarState extends State<SkinAvatar> {
  // Content-addressed URLs are immutable. Keep only a few small texture files.
  static final _cache = <String, Uint8List>{};
  static final _cacheChanges = ValueNotifier<int>(0);
  void cached() {
    if (mounted &&
        image == null &&
        !loading &&
        _cache.containsKey(widget.skinUrl)) unawaited(load());
  }

  ui.Image? image;
  int generation = 0;
  bool loading = false, failed = false;
  @override
  void initState() {
    super.initState();
    _cacheChanges.addListener(cached);
    unawaited(load());
  }

  @override
  void didUpdateWidget(SkinAvatar oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.skinUrl != widget.skinUrl ||
        oldWidget.loader != widget.loader) {
      image?.dispose();
      image = null;
      loading = false;
      failed = false;
      unawaited(load());
    }
  }

  Future<void> load() async {
    final current = ++generation;
    final url = widget.skinUrl;
    if (url == null || !isOfficialSkinUrl(url)) return;
    if (mounted) {
      setState(() {
        loading = true;
        failed = false;
      });
    }
    try {
      final cached = widget.loader == loadSkinTexture ? _cache[url] : null;
      final bytes = cached ?? await widget.loader(Uri.parse(url));
      // Inspect the PNG header before decoding to bound image allocation.
      if (bytes.length < 24 ||
          !listEquals(bytes.sublist(0, 8), [137, 80, 78, 71, 13, 10, 26, 10])) {
        throw const FormatException('Invalid PNG');
      }
      final header = ByteData.sublistView(bytes);
      if (header.getUint32(16) != 64 ||
          ![32, 64].contains(header.getUint32(20))) {
        throw const FormatException('Unsupported skin dimensions');
      }
      final codec = await ui.instantiateImageCodec(bytes);
      ui.Image decoded;
      try {
        decoded = (await codec.getNextFrame()).image;
      } finally {
        codec.dispose();
      }
      if (!mounted || current != generation) {
        decoded.dispose();
        return;
      }
      if (widget.loader == loadSkinTexture) {
        if (_cache.length >= 8) _cache.remove(_cache.keys.first);
        _cache[url] = bytes;
        _cacheChanges.value++;
      }
      setState(() => image = decoded);
    } catch (_) {
      if (mounted && current == generation) setState(() => failed = true);
      // Manual retry only: a temporary CDN failure must not become permanent.
    } finally {
      if (mounted && current == generation) setState(() => loading = false);
    }
  }

  @override
  void dispose() {
    generation++;
    _cacheChanges.removeListener(cached);
    image?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => SizedBox(
      key: const Key('accountAvatar'),
      width: widget.size,
      height: widget.size,
      child: image != null
          ? CustomPaint(painter: SkinHeadPainter(image!))
          : Tooltip(
              message: failed ? tr('皮肤加载失败，点击重试') : tr('皮肤头像'),
              child: GestureDetector(
                  onTap: loading || widget.skinUrl == null
                      ? null
                      : () => unawaited(load()),
                  child: Container(
                      decoration: BoxDecoration(
                          color: Theme.of(context).colorScheme.surfaceVariant,
                          borderRadius: BorderRadius.circular(10)),
                      child: loading
                          ? const Padding(
                              padding: EdgeInsets.all(12),
                              child: CircularProgressIndicator(strokeWidth: 2))
                          : Icon(
                              failed
                                  ? Icons.refresh_rounded
                                  : Icons.person_outline_rounded,
                              color: accent,
                              size: widget.size * .6)))));
}

/// Front orthographic view of the 8-unit head inside its 9-unit outer layer.
/// Classic and slim skins share UVs; neither layer is rounded or smoothed.
class SkinHeadPainter extends CustomPainter {
  const SkinHeadPainter(this.texture);
  final ui.Image texture;
  @override
  void paint(Canvas canvas, Size size) {
    final paint = Paint()
      ..filterQuality = FilterQuality.none
      ..isAntiAlias = false;
    final side = size.shortestSide;
    final outer = Rect.fromCenter(
        center: size.center(Offset.zero), width: side, height: side);
    // The outer cube extends 0.5 model units beyond each side of the head.
    // Reserve that space inside the avatar so the hair is never clipped.
    final face = outer.deflate(side / 18);
    canvas.drawImageRect(texture, const Rect.fromLTWH(8, 8, 8, 8), face, paint);
    canvas.drawImageRect(
        texture, const Rect.fromLTWH(40, 8, 8, 8), outer, paint);
  }

  @override
  bool shouldRepaint(SkinHeadPainter oldDelegate) =>
      texture != oldDelegate.texture;
}
