// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'localization.dart';
import 'dart:async';
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

  Future<dynamic> run(Map<String, dynamic> request, {String title = '正在加载'}) {
    final done = Completer<dynamic>();
    final epoch = _epoch;
    final record = <String, dynamic>{
      "title": title,
      "status": "queued",
      "op": request["op"]
    };
    history.insert(0, record);
    if (history.length > 50) history.removeLast();
    _tail = _tail.then((_) async {
      if (_disposed || epoch != _epoch) {
        record["status"] = "cancelled";
        done.completeError(
            const EngineFailure({'code': 'cancelled', 'message': "操作已取消"}));
        return;
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
        if (_cancelled) {
          throw const EngineFailure({'code': 'cancelled', 'message': "操作已取消"});
        }
        if (snapshot['error'] is Map) {
          final error = Map<String, dynamic>.from(snapshot['error'] as Map);
          await core.call({'action': 'clear_error'});
          throw EngineFailure(error);
        }
        record['status'] = 'complete';
        done.complete(snapshot['result']);
      } catch (e) {
        record['status'] = _cancelled ? 'cancelled' : 'failed';
        if (e is EngineFailure) record['error'] = e.details;
        done.completeError(e is EngineFailure
            ? e
            : const EngineFailure(
                {'code': 'core_unavailable', 'message': "无法读取 C++ 核心返回的结果。"}));
      } finally {
        active = false;
        progress = {};
        update();
      }
    });
    return done.future;
  }

  Future<void> cancel() async {
    _cancelled = true;
    _epoch++;
    await core.call({'action': 'cancel'});
  }

  @override
  void dispose() {
    _disposed = true;
    super.dispose();
  }
}
