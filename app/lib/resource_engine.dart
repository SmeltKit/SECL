// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
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
    final record = <String, dynamic>{
      "title": title,
      "status": "queued",
      "op": request["op"]
    };
    history.insert(0, record);
    if (history.length > 50) history.removeLast();
    return queueCoreCommand(() async {
      if (_disposed || epoch != _epoch) {
        record["status"] = "cancelled";
        update();
        throw const EngineFailure({'code': 'cancelled', 'message': "操作已取消"});
      }
      record["status"] = "running";
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
          update();
          await Future<void>.delayed(const Duration(milliseconds: 100));
          snapshot = await core.call({'action': 'poll'});
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
        record['status'] = 'complete';
        return result;
      } catch (e) {
        record['status'] = _cancelled ? 'cancelled' : 'failed';
        if (e is EngineFailure) {
          record['error'] = e.details;
          rethrow;
        }
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
