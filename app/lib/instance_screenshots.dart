// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'dart:math' as math;
import 'package:file_selector/file_selector.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:intl/intl.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'resource_engine.dart';

String screenshotSize(num bytes) => bytes < 1024
    ? '${bytes.toInt()} B'
    : bytes < 1048576
        ? '${(bytes / 1024).toStringAsFixed(1)} KiB'
        : '${(bytes / 1048576).toStringAsFixed(1)} MiB';
String screenshotDate(Data row) => DateFormat('yyyy-MM-dd HH:mm').format(
    DateTime.fromMillisecondsSinceEpoch((row['modified'] as num).toInt() * 1000)
        .toLocal());
Data screenshotChoice(Data row) =>
    {'filename': row['filename'], 'snapshot': row['snapshot']};

class InstanceScreenshots extends StatefulWidget {
  const InstanceScreenshots(
      {super.key, required this.engine, required this.instance});
  final ResourceEngine engine;
  final Data instance;
  @override
  State<InstanceScreenshots> createState() => _InstanceScreenshotsState();
}

class _InstanceScreenshotsState extends State<InstanceScreenshots>
    with ManagedState<InstanceScreenshots> {
  @override
  ResourceEngine get engine => widget.engine;
  List<Data> rows = [];
  int offset = 0, total = 0, skipped = 0;
  bool more = false, scanLimited = false;
  static const pageSize = 36;
  @override
  void initState() {
    super.initState();
    load();
  }

  @override
  void didUpdateWidget(InstanceScreenshots oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.instance['game_directory'] !=
        widget.instance['game_directory']) {
      offset = 0;
      rows = [];
      load();
    }
  }

  Future<dynamic> command(String op, [Data extra = const {}]) => engine.run(
      {'op': 'screenshots.$op', 'instance': widget.instance, ...extra},
      title: tr('截图管理'));
  Future<void> read() async {
    final directory = widget.instance['game_directory'];
    final result = Map<String, dynamic>.from(
        await command('list', {'offset': offset, 'limit': pageSize}) as Map);
    if (!mounted || directory != widget.instance['game_directory']) return;
    final next = dataRows(result['items']);
    if (!mounted) return;
    setState(() {
      rows = next;
      total = (result['total'] as num).toInt();
      more = result['has_more'] == true;
      skipped = (result['skipped'] as num? ?? 0).toInt();
      scanLimited = result['scan_limited'] == true;
    });
  }

  Future<void> load() => perform(read);
  Future<void> openDirectory() => perform(() async => command('open'));
  Future<void> remove(Data row) async {
    if (!await confirmOperation(
        context, tr('移除截图'), tr('将「{0}」移到系统废纸篓，可从废纸篓恢复。', [row['filename']]))) {
      return;
    }
    await perform(() async {
      await command(
          'remove', {'file': screenshotChoice(row), 'confirmed': true});
      if (rows.length == 1 && offset > 0) offset -= pageSize;
      await read();
    });
  }

  Future<void> export(Data row) async {
    final name = row['filename'] as String;
    final extension = name.split('.').last.toLowerCase();
    final location =
        await getSaveLocation(suggestedName: name, acceptedTypeGroups: [
      XTypeGroup(label: row['format'] as String, extensions: [extension])
    ]);
    if (location == null || !mounted) return;
    await perform(() async {
      await command(
          'export', {'file': screenshotChoice(row), 'path': location.path});
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text(tr('截图已导出'))));
      }
    });
  }

  Future<void> preview(int index) async {
    await showDialog<void>(
        context: context,
        barrierColor: Colors.black87,
        builder: (_) => ScreenshotPreview(
            engine: engine,
            instance: widget.instance,
            rows: rows,
            initialIndex: index));
  }

  void action(String value, Data row) {
    if (value == 'preview') preview(rows.indexOf(row));
    if (value == 'open') {
      perform(() async {
        await command('open', {'file': screenshotChoice(row)});
      });
    }
    if (value == 'export') export(row);
    if (value == 'remove') remove(row);
  }

  @override
  Widget build(BuildContext context) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Wrap(
            spacing: 10,
            runSpacing: 8,
            crossAxisAlignment: WrapCrossAlignment.center,
            children: [
              OutlinedButton.icon(
                  onPressed: locked ? null : openDirectory,
                  icon: const Icon(Icons.folder_open_outlined, size: 18),
                  label: Text(tr('打开目录'))),
              OutlinedButton.icon(
                  onPressed: locked ? null : load,
                  icon: const Icon(Icons.refresh_rounded, size: 18),
                  label: Text(tr('刷新'))),
              Text(tr('{0} 张截图', [total]),
                  style: TextStyle(color: muted, fontSize: 12)),
            ]),
        const SizedBox(height: 14),
        Expanded(
            child: result(rows.isEmpty
                ? Center(
                    child: Column(mainAxisSize: MainAxisSize.min, children: [
                    Icon(Icons.photo_library_outlined, size: 36, color: muted),
                    const SizedBox(height: 12),
                    Text(tr('尚无可显示的截图')),
                    const SizedBox(height: 6),
                    Text(tr('仅显示此实例 screenshots 目录中的 PNG、JPEG 和 WebP 图片。'),
                        textAlign: TextAlign.center,
                        style: TextStyle(color: muted, height: 1.5))
                  ]))
                : LayoutBuilder(builder: (context, constraints) {
                    final columns =
                        math.max(1, (constraints.maxWidth / 230).floor());
                    return GridView.builder(
                        key: const Key('screenshotGallery'),
                        gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                            crossAxisCount: columns,
                            crossAxisSpacing: 16,
                            mainAxisSpacing: 18,
                            mainAxisExtent: 228),
                        itemCount: rows.length,
                        itemBuilder: (context, index) {
                          final row = rows[index];
                          return Column(
                              crossAxisAlignment: CrossAxisAlignment.stretch,
                              children: [
                                Expanded(
                                    child: Material(
                                        color: Theme.of(context)
                                            .colorScheme
                                            .surface,
                                        clipBehavior: Clip.antiAlias,
                                        borderRadius: BorderRadius.circular(10),
                                        child: InkWell(
                                            key: ValueKey(
                                                'screenshot-${row['filename']}'),
                                            onTap: locked
                                                ? null
                                                : () => preview(index),
                                            child: ScreenshotImage(
                                                row: row, thumbnail: true)))),
                                const SizedBox(height: 8),
                                Row(children: [
                                  Expanded(
                                      child: Tooltip(
                                          message: row['filename'] as String,
                                          child: Text(row['filename'] as String,
                                              maxLines: 1,
                                              overflow: TextOverflow.ellipsis,
                                              style: const TextStyle(
                                                  fontSize: 13,
                                                  fontWeight:
                                                      FontWeight.w600)))),
                                  SizedBox(
                                      width: 30,
                                      height: 30,
                                      child: PopupMenuButton<String>(
                                          key: ValueKey(
                                              'screenshot-menu-${row['filename']}'),
                                          enabled: !locked,
                                          tooltip: tr('更多操作'),
                                          padding: EdgeInsets.zero,
                                          icon: const Icon(
                                              Icons.more_horiz_rounded,
                                              size: 18),
                                          onSelected: (value) =>
                                              action(value, row),
                                          itemBuilder: (_) => [
                                                for (final item in {
                                                  'preview': tr('预览截图'),
                                                  'open': tr('打开原图'),
                                                  'export': tr('另存为'),
                                                  'remove': tr('移到废纸篓')
                                                }.entries)
                                                  PopupMenuItem(
                                                      value: item.key,
                                                      child: Text(item.value))
                                              ]))
                                ]),
                                Text(screenshotDate(row),
                                    style:
                                        TextStyle(fontSize: 11, color: muted)),
                                const SizedBox(height: 4),
                                Text(
                                    '${row['width']} × ${row['height']} · ${screenshotSize(row['size'] as num)}',
                                    style:
                                        TextStyle(fontSize: 11, color: muted))
                              ]);
                        });
                  }))),
        if (skipped > 0 || scanLimited)
          Padding(
              padding: const EdgeInsets.only(top: 8),
              child: Text(
                  scanLimited
                      ? tr('截图较多，本次最多扫描 4096 个目录项。')
                      : tr('已跳过 {0} 项不支持、损坏或无法安全读取的文件。', [skipped]),
                  style: TextStyle(fontSize: 11, color: muted, height: 1.5))),
        const SizedBox(height: 8),
        Row(mainAxisAlignment: MainAxisAlignment.end, children: [
          IconButton(
              key: const Key('screenshotsPreviousPage'),
              tooltip: tr('上一页'),
              onPressed: locked || offset == 0
                  ? null
                  : () {
                      offset -= pageSize;
                      load();
                    },
              icon: const Icon(Icons.chevron_left_rounded)),
          Text('${offset ~/ pageSize + 1}',
              style: TextStyle(fontSize: 12, color: muted)),
          IconButton(
              key: const Key('screenshotsNextPage'),
              tooltip: tr('下一页'),
              onPressed: locked || !more
                  ? null
                  : () {
                      offset += pageSize;
                      load();
                    },
              icon: const Icon(Icons.chevron_right_rounded))
        ])
      ]);
}

class ScreenshotFileImage extends FileImage {
  const ScreenshotFileImage(super.file, this.snapshot);
  final String snapshot;
  @override
  bool operator ==(Object other) =>
      other is ScreenshotFileImage &&
      other.file.path == file.path &&
      other.scale == scale &&
      other.snapshot == snapshot;
  @override
  int get hashCode => Object.hash(file.path, scale, snapshot);
}

class ScreenshotImage extends StatelessWidget {
  const ScreenshotImage({super.key, required this.row, this.thumbnail = false});
  final Data row;
  final bool thumbnail;
  @override
  Widget build(BuildContext context) {
    final width = (row['width'] as num).toInt();
    final height = (row['height'] as num).toInt();
    final edge = thumbnail ? 640 : 2400;
    return Image(
        image: ResizeImage.resizeIfNeeded(
            width >= height ? math.min(width, edge) : null,
            height > width ? math.min(height, edge) : null,
            ScreenshotFileImage(
                File(row['path'] as String), row['snapshot'] as String)),
        key: ValueKey('${row['path']}-${row['snapshot']}'),
        fit: thumbnail ? BoxFit.cover : BoxFit.contain,
        gaplessPlayback: true,
        errorBuilder: (_, __, ___) => Center(
            child: Padding(
                padding: const EdgeInsets.all(12),
                child: Column(mainAxisSize: MainAxisSize.min, children: [
                  Icon(Icons.broken_image_outlined, color: muted),
                  const SizedBox(height: 6),
                  Text(tr('图片无法显示，请刷新列表。'),
                      textAlign: TextAlign.center,
                      style: TextStyle(color: muted, fontSize: 12))
                ]))));
  }
}

class ScreenshotPreview extends StatefulWidget {
  const ScreenshotPreview(
      {super.key,
      required this.engine,
      required this.instance,
      required this.rows,
      required this.initialIndex});
  final ResourceEngine engine;
  final Data instance;
  final List<Data> rows;
  final int initialIndex;
  @override
  State<ScreenshotPreview> createState() => _ScreenshotPreviewState();
}

class _ScreenshotPreviewState extends State<ScreenshotPreview>
    with ManagedState<ScreenshotPreview> {
  @override
  ResourceEngine get engine => widget.engine;
  late int index;
  Data? current;
  @override
  void initState() {
    super.initState();
    index = widget.initialIndex;
    inspect();
  }

  Future<void> inspect() => perform(() async {
        final row = widget.rows[index];
        final checked = Map<String, dynamic>.from(await rpc({
          'op': 'screenshots.inspect',
          'instance': widget.instance,
          'file': screenshotChoice(row)
        }) as Map);
        if (mounted) setState(() => current = checked);
      });
  void move(int delta) {
    final next = index + delta;
    if (locked || next < 0 || next >= widget.rows.length) return;
    setState(() {
      index = next;
      current = null;
    });
    inspect();
  }

  @override
  Widget build(BuildContext context) {
    final row = widget.rows[index];
    return Shortcuts(
        shortcuts: const {
          SingleActivator(LogicalKeyboardKey.arrowLeft): _PreviousScreenshot(),
          SingleActivator(LogicalKeyboardKey.arrowRight): _NextScreenshot(),
          SingleActivator(LogicalKeyboardKey.escape): _CloseScreenshot()
        },
        child: Actions(
            actions: {
              _PreviousScreenshot:
                  CallbackAction<_PreviousScreenshot>(onInvoke: (_) {
                move(-1);
                return null;
              }),
              _NextScreenshot: CallbackAction<_NextScreenshot>(onInvoke: (_) {
                move(1);
                return null;
              }),
              _CloseScreenshot: CallbackAction<_CloseScreenshot>(onInvoke: (_) {
                Navigator.pop(context);
                return null;
              })
            },
            child: Focus(
                autofocus: true,
                child: Dialog(
                    insetPadding: const EdgeInsets.all(24),
                    child: SizedBox(
                        width: 1100,
                        height: MediaQuery.sizeOf(context).height * .82,
                        child: Padding(
                            padding: const EdgeInsets.all(18),
                            child: Column(
                                crossAxisAlignment: CrossAxisAlignment.stretch,
                                children: [
                                  Row(children: [
                                    Expanded(
                                        child: Text(row['filename'] as String,
                                            maxLines: 1,
                                            overflow: TextOverflow.ellipsis,
                                            style: const TextStyle(
                                                fontSize: 16,
                                                fontWeight: FontWeight.w600))),
                                    IconButton(
                                        tooltip: tr('关闭'),
                                        onPressed: () => Navigator.pop(context),
                                        icon: const Icon(Icons.close_rounded))
                                  ]),
                                  const SizedBox(height: 8),
                                  Expanded(
                                      child: result(current == null
                                          ? const SizedBox()
                                          : InteractiveViewer(
                                              key: ValueKey(
                                                  current!['snapshot']),
                                              minScale: 1,
                                              maxScale: 5,
                                              child: Center(
                                                  child: ScreenshotImage(
                                                      row: current!))))),
                                  const SizedBox(height: 10),
                                  Wrap(
                                      spacing: 12,
                                      runSpacing: 4,
                                      crossAxisAlignment:
                                          WrapCrossAlignment.center,
                                      children: [
                                        Text(
                                            '${index + 1} / ${widget.rows.length}',
                                            style: TextStyle(
                                                fontSize: 12, color: muted)),
                                        Text(
                                            '${row['width']} × ${row['height']} · ${screenshotSize(row['size'] as num)}',
                                            style: TextStyle(
                                                fontSize: 12, color: muted)),
                                        IconButton(
                                            key:
                                                const Key('previousScreenshot'),
                                            tooltip: tr('上一张'),
                                            onPressed: locked || index == 0
                                                ? null
                                                : () => move(-1),
                                            icon: const Icon(
                                                Icons.chevron_left_rounded)),
                                        IconButton(
                                            key: const Key('nextScreenshot'),
                                            tooltip: tr('下一张'),
                                            onPressed: locked ||
                                                    index + 1 ==
                                                        widget.rows.length
                                                ? null
                                                : () => move(1),
                                            icon: const Icon(
                                                Icons.chevron_right_rounded)),
                                        TextButton.icon(
                                            onPressed: locked || current == null
                                                ? null
                                                : () => perform(() async {
                                                      await rpc({
                                                        'op':
                                                            'screenshots.open',
                                                        'instance':
                                                            widget.instance,
                                                        'file':
                                                            screenshotChoice(
                                                                current!)
                                                      });
                                                    }),
                                            icon: const Icon(
                                                Icons.open_in_new_rounded,
                                                size: 17),
                                            label: Text(tr('打开原图')))
                                      ])
                                ])))))));
  }
}

class _PreviousScreenshot extends Intent {
  const _PreviousScreenshot();
}

class _NextScreenshot extends Intent {
  const _NextScreenshot();
}

class _CloseScreenshot extends Intent {
  const _CloseScreenshot();
}
