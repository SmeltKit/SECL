// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
import 'package:ffi/ffi.dart';

typedef _CallNative = Pointer<Utf8> Function(Pointer<Utf8>);
typedef _CallDart = Pointer<Utf8> Function(Pointer<Utf8>);
typedef _FreeNative = Void Function(Pointer<Utf8>);
typedef _FreeDart = void Function(Pointer<Utf8>);

String libraryPath() {
  final custom = Platform.environment['SOLAR_CORE_LIBRARY'];
  if (custom != null && custom.isNotEmpty) return custom;
  final exe = File(Platform.resolvedExecutable).parent;
  if (Platform.isMacOS) {
    return '${exe.parent.path}/Frameworks/libsolar_core.dylib';
  }
  if (Platform.isWindows) return '${exe.path}/solar_core.dll';
  return '${exe.path}/lib/libsolar_core.so';
}

Map<String, dynamic> _nativeCall(Map<String, dynamic> request) {
  final library = DynamicLibrary.open(libraryPath());
  final abi = library
      .lookupFunction<Uint32 Function(), int Function()>('solar_abi_version');
  if (abi() != 1) throw StateError('不兼容的 SECL 核心版本');
  final call = library.lookupFunction<_CallNative, _CallDart>('solar_command');
  final release = library.lookupFunction<_FreeNative, _FreeDart>('solar_free');
  final input = jsonEncode(request).toNativeUtf8();
  Pointer<Utf8> output = nullptr;
  try {
    output = call(input);
    if (output == nullptr) throw StateError('核心没有返回结果');
    return jsonDecode(output.toDartString()) as Map<String, dynamic>;
  } finally {
    malloc.free(input);
    if (output != nullptr) release(output);
  }
}

abstract class LauncherCore {
  Future<Map<String, dynamic>> call(Map<String, dynamic> request);
}

class NativeCore implements LauncherCore {
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) =>
      Isolate.run(() => _nativeCall(request));
}
