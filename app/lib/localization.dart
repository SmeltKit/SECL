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
    Locale('en'),
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

final _nativePattern = RegExp(
  (nativeMessages.toList()..sort((a, b) => b.length.compareTo(a.length)))
      .map(RegExp.escape)
      .join('|'),
);

// Native diagnostics ending in a colon append IDs, paths, field names, or other
// literal data. Translate their prefix once, without translating the payload.
final _nativeLiteralPrefixes = nativeMessages
    .where(
      (message) =>
          message.endsWith('：') &&
          !message.startsWith(' ') &&
          !message.startsWith('：'),
    )
    .toList()
  ..sort((a, b) => b.length.compareTo(a.length));

const _nativeLiteralSeparators = [
  ' 缺少前置：',
  ' 前置版本不符合：',
  ' 依赖组合无法同时满足：',
  ' 语言加载器版本不符合：',
  ' 仍依赖所选模组，请一起选择或先禁用依赖者。',
];

final _nativeParameterizedMessages = <RegExp, String>{
  RegExp(r'^内嵌组件的加载选择尚未确定：(.*) 与 (.*) 的网站不兼容声明需由实际加载器确认。$'):
      '内嵌组件的加载选择尚未确定：{0} 与 {1} 的网站不兼容声明需由实际加载器确认。',
  RegExp(r'^(.*)：加载器不匹配（(.*)）。$'): '{0}：加载器不匹配（{1}）。',
  RegExp(r'^(.*) 与 (.*) 不兼容。$'): '{0} 与 {1} 不兼容。',
  RegExp(r'^(.*) 与 (.*) 不能同时启用。$'): '{0} 与 {1} 不能同时启用。',
  RegExp(r'^Quilt 官方兼容性提醒：(.*)。未提供精确文件哈希，需核对具体构建。$'):
      'Quilt 官方兼容性提醒：{0}。未提供精确文件哈希，需核对具体构建。',
};

/// Translates only messages emitted by our native core, retaining diagnostic
/// codes, server details, and interpolated filenames without changing them.
String nativeText(String source) {
  if (AppLanguage.instance.code == 'zh_Hans') return source;
  if (catalog.containsKey(source)) return tr(source);
  for (final entry in _nativeParameterizedMessages.entries) {
    final match = entry.key.firstMatch(source);
    if (match != null) {
      return tr(entry.value, [
        for (var index = 1; index <= match.groupCount; ++index) match[index],
      ]);
    }
  }
  for (final prefix in _nativeLiteralPrefixes) {
    if (source.startsWith(prefix)) {
      return '${tr(prefix)}${source.substring(prefix.length)}';
    }
  }
  for (final separator in _nativeLiteralSeparators) {
    final index = source.lastIndexOf(separator);
    if (index >= 0) {
      return '${source.substring(0, index)}${tr(separator)}${source.substring(index + separator.length)}';
    }
  }
  // An unknown-metadata diagnostic can prefix a complete core problem with a
  // mod display name. Keep the name literal, including Chinese UI-like text.
  var separator = source.indexOf('：');
  while (separator >= 0) {
    final problem = source.substring(separator + 1);
    if (nativeMessages.contains(problem) ||
        _nativeLiteralPrefixes.any(problem.startsWith)) {
      return '${source.substring(0, separator + 1)}${nativeText(problem)}';
    }
    separator = source.indexOf('：', separator + 1);
  }
  // Unrecognized details (for example a JSON parser's exception) can contain
  // quoted user metadata. There is no safe substring translation for that data.
  if (source.contains('：')) return source;
  return source.replaceAllMapped(_nativePattern, (m) => tr(m[0]!));
}

String errorMessage(Map<String, dynamic> error) =>
    nativeText(error['message']?.toString() ?? '请求未完成');
