// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'localization.dart';
import 'package:flutter/foundation.dart';
import 'core_bridge.dart';

class EngineFailure implements Exception {
  const EngineFailure(this.details);
  final Map<String, dynamic> details;
  @override
  String toString() => details['message']?.toString() ?? tr("核心操作失败");
}

class ResourceEngine extends ChangeNotifier {
  ResourceEngine(this.core);
  final LauncherCore core;
  Future<void> _tail = Future.value();
  Future<void>? _cancellation;
  bool active = false, _disposed = false, _cancelled = false;
  int _epoch = 0;
  String label = '';
  final List<Map<String, dynamic>> history = [];
  void clearFinished() {
    history.removeWhere(
        (t) => t["status"] != "running" && t["status"] != "queued");
    update();
  }

  /// Only actual installation work belongs in the downloads UI. Queries and
  /// scans still use the same serialized worker, without becoming downloads.
  List<Map<String, dynamic>> get downloadHistory => history;
  int get activeDownloadCount => history
      .where(
          (task) => task['status'] == 'running' || task['status'] == 'queued')
      .length;
  Map<String, dynamic>? get currentDownload {
    for (final task in history) {
      if (task['status'] == 'running') return task;
    }
    return null;
  }

  String get activeDownloadTitle => currentDownload?['title'] as String? ?? '';

  Map<String, dynamic> progress = {};
  void update() {
    if (!_disposed) notifyListeners();
  }

  // Authentication and engine operations use the same native worker.
  Future<T> queueCoreCommand<T>(Future<T> Function() command) {
    final result = _tail.then((_) => command());
    _tail = result.then<void>((_) {}, onError: (Object _, StackTrace __) {});
    return result;
  }

  Future<dynamic> run(Map<String, dynamic> request, {String title = '正在加载'}) {
    final epoch = _epoch;
    final record = _downloadRecord(request, title);
    if (record != null) {
      history.insert(0, record);
      if (history.length > 50) {
        final finished = history.lastIndexWhere((task) =>
            task['status'] != 'running' && task['status'] != 'queued');
        if (finished >= 0) history.removeAt(finished);
      }
      scheduleMicrotask(update);
    }
    return queueCoreCommand(() async {
      if (_disposed || epoch != _epoch) {
        record?["status"] = "cancelled";
        update();
        throw const EngineFailure({'code': 'cancelled', 'message': "操作已取消"});
      }
      record?["status"] = "running";
      active = true;
      _cancelled = false;
      label = title;
      progress = {};
      update();
      try {
        var snapshot =
            await core.call({'action': 'engine', 'request': request});
        while (snapshot['busy'] == true) {
          progress =
              Map<String, dynamic>.from(snapshot['progress'] as Map? ?? {});
          record?['progress'] = {...progress};
          update();
          await Future<void>.delayed(const Duration(milliseconds: 100));
          snapshot = await core.call({'action': 'poll'});
        }
        if (snapshot['progress'] is Map &&
            (snapshot['progress'] as Map).isNotEmpty) {
          record?['progress'] =
              Map<String, dynamic>.from(snapshot['progress'] as Map);
        }
        final result = snapshot['result'];
        final committedImport = request['op'] == 'modpacks.apply' &&
            snapshot['error'] == null &&
            result is Map &&
            result['committed'] == true &&
            result['id'] is String &&
            result['root'] is String &&
            result['modpack'] is Map;
        if (_cancelled && !committedImport) {
          throw const EngineFailure({'code': 'cancelled', 'message': "操作已取消"});
        }
        if (snapshot['error'] is Map) {
          final error = Map<String, dynamic>.from(snapshot['error'] as Map);
          await core.call({'action': 'clear_error'});
          throw EngineFailure(error);
        }
        record?['status'] = 'complete';
        return result;
      } catch (e) {
        record?['status'] = _cancelled ? 'cancelled' : 'failed';
        if (e is EngineFailure) {
          record?['error'] = e.details;
          rethrow;
        }
        record?['error'] = {
          'code': 'core_unavailable',
          'message': '无法读取 C++ 核心返回的结果。'
        };
        throw const EngineFailure(
            {'code': 'core_unavailable', 'message': "无法读取 C++ 核心返回的结果。"});
      } finally {
        if (_cancellation != null) {
          try {
            await _cancellation;
          } catch (_) {
            // The cancel caller handles bridge errors.
          }
          _cancellation = null;
        }
        active = false;
        progress = {};
        update();
      }
    });
  }

  Future<void> cancel() async {
    _cancelled = true;
    _epoch++;
    if (active) {
      await (_cancellation ??=
          core.call({'action': 'cancel'}).then<void>((_) {}));
    }
  }

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }
}

Map<String, dynamic>? _downloadRecord(
    Map<String, dynamic> request, String fallback) {
  Map<String, dynamic> map(dynamic value) =>
      value is Map ? Map<String, dynamic>.from(value) : <String, dynamic>{};
  List<Map<String, dynamic>> rows(dynamic value) => value is List
      ? value
          .whereType<Map>()
          .map((item) => Map<String, dynamic>.from(item))
          .toList()
      : [];
  String text(dynamic value) => value is String ? value.trim() : '';
  String first(Iterable<dynamic> values) => values
      .map(text)
      .firstWhere((value) => value.isNotEmpty, orElse: () => '');
  String fileName(Map<String, dynamic> file) {
    final name =
        first([file['title'], file['name'], file['filename'], file['path']]);
    final version = text(file['version']);
    return version.isNotEmpty && !name.contains(version)
        ? '$name · $version'
        : name;
  }

  final op = text(request['op']);
  final plan = map(request['plan']);
  final input = map(request['request']);
  final target = map(plan['target']);
  var kind = '';
  var title = '';
  var subtitle = '';
  var items = <String>[];
  if (op == 'java.install') {
    kind = 'java';
    final build = map(request['build']);
    title = 'Java ${first([
          build['version'],
          build['java_version'],
          build['major_version']
        ])}'
        .trim();
    subtitle = [
      ({
            'zulu': 'Zulu',
            'temurin': 'Temurin',
            'microsoft': 'Microsoft OpenJDK',
            'oracle-openjdk': 'Oracle OpenJDK'
          }[build['provider']] ??
          first([build['provider'], build['vendor']])),
      text(build['arch'])
    ].where((value) => value.isNotEmpty).join(' · ');
  } else if (op == 'game.install') {
    kind = 'game';
    final version = first(
        [map(input['version_entry'])['id'], input['version'], input['id']]);
    title = 'Minecraft $version'.trim();
    subtitle = [
      text(input['loader']),
      text(input['loader_version']),
      text(input['id'])
    ].where((value) => value.isNotEmpty && value != 'vanilla').join(' · ');
  } else if (op == 'optifine.install' && input['operation'] != 'remove') {
    kind = 'component';
    final build = map(input['build']);
    title = [
      'OptiFine',
      first([build['version'], build['type'], build['patch']])
    ].where((value) => value.isNotEmpty).join(' ');
    subtitle = text(input['id']);
  } else if (op == 'packs.apply') {
    kind = text(plan['kind']);
    title = fileName(map(plan['file']));
  } else if (op == 'mods.apply' || op == 'updates.apply') {
    kind = text(plan['kind']);
    if (kind.isEmpty) kind = 'mod';
    final files = rows(plan['files']);
    if (files.isEmpty) {
      files.addAll(rows(plan['replacements']).map((item) => map(item['file'])));
    }
    items = files.map(fileName).where((name) => name.isNotEmpty).toList();
    final root = files.where((file) =>
        file['root'] == true || file['project_id'] == plan['root_project']);
    title = root.isNotEmpty ? fileName(root.first) : items.firstOrNull ?? '';
  } else if (op == 'modpacks.apply') {
    kind = 'modpack';
    title = first([plan['name'], map(plan['pack'])['name'], plan['id']]);
    subtitle = first(
        [map(plan['pack'])['version_id'], map(plan['pack'])['minecraft']]);
    items = rows(plan['files'])
        .map(fileName)
        .where((name) => name.isNotEmpty)
        .toList();
  } else {
    return null;
  }
  if (title.isEmpty) title = fallback;
  final instance = first([target['name'], target['id']]);
  return {
    'title': title,
    'subtitle': subtitle,
    'kind': kind,
    'target': instance,
    'items': items,
    'status': 'queued',
    'op': op,
    'progress': <String, dynamic>{},
  };
}
