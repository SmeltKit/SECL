// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:ui';
import 'package:flutter/widgets.dart';
import 'l10n/catalog.dart';

/// UI-only localization. Account names, project content, paths, IDs and filenames
/// are never passed through the translation catalog.
class AppLanguage extends ChangeNotifier with WidgetsBindingObserver {
  AppLanguage._();
  static final instance = AppLanguage._();
  static const supported = [
    Locale('zh', 'CN'),
    Locale('zh', 'TW'),
    Locale('en')
  ];
  static const preferences = ['system', 'zh_Hans', 'zh_Hant', 'en'];
  String _preference = 'system';
  List<Locale>? _system;
  String get preference => _preference;
  Locale get locale => _preference == 'system'
      ? resolve(_system ?? PlatformDispatcher.instance.locales)
      : _preference == 'zh_Hans'
          ? supported[0]
          : _preference == 'zh_Hant'
              ? supported[1]
              : supported[2];
  String get code => locale.languageCode == 'en'
      ? 'en'
      : locale.countryCode == 'TW'
          ? 'zh_Hant'
          : 'zh_Hans';
  static Locale resolve(List<Locale> locales) {
    for (final locale in locales) {
      if (locale.languageCode == 'zh') {
        final traditional = locale.scriptCode == 'Hant' ||
            (locale.scriptCode != 'Hans' &&
                ['TW', 'HK', 'MO'].contains(locale.countryCode));
        return traditional ? supported[1] : supported[0];
      }
      if (locale.languageCode == 'en') return supported[2];
    }
    return supported[2];
  }

  void select(String preference) {
    final next = preferences.contains(preference) ? preference : 'system';
    if (_preference == next) return;
    _preference = next;
    notifyListeners();
  }

  @override
  void didChangeLocales(List<Locale>? locales) {
    _system = locales;
    if (_preference == 'system') notifyListeners();
  }
}

String tr(String source, [List<Object?> args = const []]) {
  final values = catalog[source];
  final language = AppLanguage.instance.code;
  final template = values == null || language == 'zh_Hans'
      ? source
      : values[language == 'zh_Hant' ? 0 : 1];
  // Replace placeholders in one pass so parameter text such as "{1}" is literal.
  return template.replaceAllMapped(RegExp(r'\{(\d+)\}'), (m) {
    final index = int.parse(m[1]!);
    return index < args.length ? '${args[index] ?? ''}' : m[0]!;
  });
}

final _nativePattern = RegExp((nativeMessages.toList()
      ..sort((a, b) => b.length.compareTo(a.length)))
    .map(RegExp.escape)
    .join('|'));

/// Translates only messages emitted by our native core, retaining diagnostic
/// codes, server details, and interpolated filenames without changing them.
String nativeText(String source) {
  if (AppLanguage.instance.code == 'zh_Hans') return source;
  if (catalog.containsKey(source)) return tr(source);
  return source.replaceAllMapped(_nativePattern, (m) => tr(m[0]!));
}

String errorMessage(Map<String, dynamic> error) =>
    nativeText(error['message']?.toString() ?? '请求未完成');
