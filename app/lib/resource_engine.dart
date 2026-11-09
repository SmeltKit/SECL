// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:convert';
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
  int _epoch = 0, _nextId = 0, replayRevision = 0;
  String label = '';
  final List<Map<String, dynamic>> history = [];
  final _requests = <int, Map<String, dynamic>>{};
  final _pending = <int>{};
  Map<String, dynamic>? _current;
  Future<void> Function(Map<String, dynamic>, dynamic)? onReplayed;

  void clearFinished() {
    history.removeWhere((t) {
      final remove =
          ['complete', 'failed', 'cancelled'].contains(t['status']) &&
              !_pending.contains(t['id']);
      if (remove) _requests.remove(t['id']);
      return remove;
    });
    update();
  }

  /// History is session-local. Partial artifact bytes live in the native cache,
  /// so a user can reissue the same download after restarting the application.
  List<Map<String, dynamic>> get downloadHistory => history;
  int get activeDownloadCount => history
      .where((t) => t['status'] == 'running' || t['status'] == 'queued')
      .length;
  Map<String, dynamic>? get currentDownload =>
      _current ?? history.where((t) => t['status'] == 'running').firstOrNull;
  String get activeDownloadTitle => currentDownload?['title'] as String? ?? '';
  Map<String, dynamic> progress = {};
  void update() {
    if (!_disposed) notifyListeners();
  }

  bool canResume(Map<String, dynamic> task) =>
      !_disposed &&
      _requests.containsKey(task['id']) &&
      !_pending.contains(task['id']) &&
      ['paused', 'cancelled', 'failed'].contains(task['status']);
  bool canPause(Map<String, dynamic> task) =>
      !_disposed &&
      identical(task, _current) &&
      task['status'] == 'running' &&
      task['stop_requested'] == null;
  bool canCancel(Map<String, dynamic> task) =>
      !_disposed &&
      _requests.containsKey(task['id']) &&
      ['queued', 'running', 'paused'].contains(task['status']) &&
      task['stop_requested'] == null;

  Future<void> _stop(Map<String, dynamic> task, String mode) async {
    if (mode == 'pause' ? !canPause(task) : !canCancel(task)) return;
    task['stop_requested'] = mode;
    if (task['status'] == 'queued' || task['status'] == 'paused') {
      task['status'] = 'cancelled';
    }
    update();
    if (identical(task, _current)) {
      Future<void>? pending;
      try {
        pending = _cancellation ??=
            core.call({'action': 'cancel'}).then<void>((_) {});
        await pending;
      } catch (_) {
        if (identical(_cancellation, pending)) _cancellation = null;
        task.remove('stop_requested');
        update();
        rethrow;
      }
    }
  }

  Future<void> pauseDownload(Map<String, dynamic> task) => _stop(task, 'pause');
  Future<void> cancelDownload(Map<String, dynamic> task) =>
      _stop(task, 'cancel');

  Future<dynamic> resumeDownload(Map<String, dynamic> task) async {
    if (!canResume(task)) return null;
    final request = _requests[task['id']]!;
    task['status'] = 'queued';
    task.remove('stop_requested');
    task.remove('error');
    task.remove('refresh_warning');
    update();
    final result =
        await _enqueue(request, task['label'] as String? ?? '下载', task, _epoch);
    replayRevision++;
    update();
    try {
      await onReplayed?.call(request, result);
    } catch (_) {
      // A refresh failing after a committed install must never invite another
      // replacement of files which have already been installed successfully.
      task['refresh_warning'] = true;
      update();
    }
    return result;
  }

  Future<T> queueCoreCommand<T>(Future<T> Function() command) {
    final result = _tail.then((_) => command());
    _tail = result.then<void>((_) {}, onError: (Object _, StackTrace __) {});
    return result;
  }

  Future<dynamic> run(Map<String, dynamic> request, {String title = '正在加载'}) {
    final record = _downloadRecord(request, title);
    if (record != null) {
      final id = ++_nextId;
      // Freeze the reviewed request. UI selections may change while it waits.
      request =
          Map<String, dynamic>.from(jsonDecode(jsonEncode(request)) as Map);
      _requests[id] = request;
      record['id'] = id;
      record['label'] = title;
      history.insert(0, record);
      if (history.length > 50) {
        final old = history.lastIndexWhere((t) =>
            ['complete', 'failed', 'cancelled'].contains(t['status']) &&
            !_pending.contains(t['id']));
        if (old >= 0) _requests.remove(history.removeAt(old)['id']);
      }
      scheduleMicrotask(update);
    }
    return _enqueue(request, title, record, _epoch);
  }

  bool _committed(Map<String, dynamic> request, dynamic result) =>
      request['op'] == 'modpacks.apply' &&
      result is Map &&
      result['committed'] == true;

  Future<dynamic> _enqueue(Map<String, dynamic> request, String title,
      Map<String, dynamic>? record, int epoch) {
    final id = record?['id'];
    if (id is int) _pending.add(id);
    return queueCoreCommand(() async {
      var started = false;
      try {
        if (_disposed ||
            epoch != _epoch ||
            record?['stop_requested'] == 'cancel') {
          throw const EngineFailure({'code': 'cancelled', 'message': '操作已取消'});
        }
        record?['status'] = 'running';
        active = true;
        started = true;
        _current = record;
        _cancelled = false;
        label = title;
        progress = {};
        update();
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
        final error = snapshot['error'] is Map
            ? Map<String, dynamic>.from(snapshot['error'] as Map)
            : null;
        // Native polling also observes the game. Its exit error belongs to the
        // launch UI and must not mislabel a concurrent successful download.
        final gameExit = error?['stage'] == 'launch' &&
            (error?['code'] as String? ?? '').startsWith('game_exit_');
        final committed = (error == null || gameExit) &&
            (snapshot['operation_committed'] == true ||
                _committed(request, result));
        final stop = record?['stop_requested'];
        if (!committed && (_cancelled || stop == 'pause' || stop == 'cancel')) {
          throw EngineFailure(stop == 'pause'
              ? {'code': 'paused', 'message': '下载已暂停，可从下载任务中继续。'}
              : {'code': 'cancelled', 'message': '操作已取消'});
        }
        if (error != null && !gameExit) {
          await core.call({'action': 'clear_error'});
          throw EngineFailure(error);
        }
        record?['status'] = 'complete';
        record?.remove('stop_requested');
        return result;
      } catch (e) {
        final code = e is EngineFailure ? e.details['code'] : '';
        record?['status'] = code == 'paused'
            ? 'paused'
            : code == 'cancelled' || _cancelled
                ? 'cancelled'
                : 'failed';
        if (e is EngineFailure) {
          if (code != 'paused' && code != 'cancelled') {
            record?['error'] = e.details;
          }
          rethrow;
        }
        record?['error'] = {
          'code': 'core_unavailable',
          'message': '无法读取 C++ 核心返回的结果。'
        };
        throw const EngineFailure(
            {'code': 'core_unavailable', 'message': '无法读取 C++ 核心返回的结果。'});
      } finally {
        if (started && _cancellation != null) {
          try {
            await _cancellation;
          } catch (_) {/* reported by the stop caller */}
          _cancellation = null;
        }
        if (id is int) _pending.remove(id);
        record?.remove('stop_requested');
        if (started) {
          active = false;
          _current = null;
          progress = {};
        }
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
