// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:io';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/l10n/catalog.dart';
import 'package:secl/localization.dart';
import 'package:secl/main.dart';
import 'package:secl/app_widgets.dart';
import 'package:secl/resource_market.dart';
import 'resource_market_test.dart' show CatalogCore, viewport;

class LanguageCore extends CatalogCore {
  String language = 'en';
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    if (request['action'] == 'configure' && request['language'] is String) {
      language = request['language'] as String;
    }
    return {...await super.call(request), 'language': language};
  }
}

class LongCategoryCore extends LanguageCore {
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async {
    final response = await super.call(request);
    if ((request['request'] as Map?)?['op'] == 'resource.categories') {
      response['result'] = [
        {
          'name': 'test-category',
          'label': 'Colored lighting and volumetric effects'
        }
      ];
    }
    return response;
  }
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  test('resolves language scripts, region aliases, and unsupported locales',
      () {
    expect(AppLanguage.resolve([const Locale('zh', 'HK')]),
        const Locale('zh', 'TW'));
    expect(
        AppLanguage.resolve(
            [const Locale.fromSubtags(languageCode: 'zh', scriptCode: 'Hant')]),
        const Locale('zh', 'TW'));
    expect(
        AppLanguage.resolve([
          const Locale.fromSubtags(
              languageCode: 'zh', scriptCode: 'Hans', countryCode: 'TW')
        ]),
        const Locale('zh', 'CN'));
    expect(AppLanguage.resolve([const Locale('zh', 'SG')]),
        const Locale('zh', 'CN'));
    expect(AppLanguage.resolve([const Locale('en', 'GB')]), const Locale('en'));
    expect(AppLanguage.resolve([const Locale('fr')]), const Locale('en'));
    AppLanguage.instance.select('system');
    AppLanguage.instance.didChangeLocales([const Locale('zh', 'TW')]);
    expect(tr('设置'), '設定');
    AppLanguage.instance.select('en');
    AppLanguage.instance.didChangeLocales([const Locale('zh', 'CN')]);
    expect(tr('设置'), 'Settings');
  });
  test('catalog is complete, placeholders match, parameters remain literal',
      () {
    final placeholders = RegExp(r'\{\d+\}');
    Set<String> tokens(String s) =>
        placeholders.allMatches(s).map((m) => m[0]!).toSet();
    for (final entry in catalog.entries) {
      expect(entry.value.length, 2, reason: entry.key);
      for (final value in entry.value) {
        expect(value, isNotEmpty, reason: entry.key);
        expect(tokens(value), tokens(entry.key), reason: entry.key);
      }
      expect(RegExp(r'[\u4e00-\u9fff]').hasMatch(entry.value[1]), isFalse,
          reason: entry.key);
    }
    for (final source in nativeMessages) {
      expect(catalog.containsKey(source), isTrue, reason: source);
    }
    // Catch new UI calls that were added without all three catalog translations.
    for (final file in Directory('lib')
        .listSync()
        .whereType<File>()
        .where((f) => f.path.endsWith('.dart'))) {
      for (final match in RegExp(r'''tr\((?:"([^"\n]+)"|'([^'\n]+)')''')
          .allMatches(file.readAsStringSync())) {
        final source = (match[1] ?? match[2]!).replaceAll(r'\n', '\n');
        expect(catalog.containsKey(source), isTrue,
            reason: '${file.path}: $source');
      }
    }
    AppLanguage.instance.select('en');
    expect(tr('将添加 · {0} 项', ['My {1} mod']), 'To add · My {1} mod');
    expect(nativeText('下载服务返回 HTTP 503'),
        'The download service returned HTTP 503');
    expect(nativeText('加载器文件缺失或损坏：iris.jar'),
        'Loader file missing or damaged: iris.jar');
  });
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets('$language home and all markets fit the minimum window',
        (t) async {
      viewport(t);
      final core = LanguageCore()..language = language;
      await t.pumpWidget(SeclApp(core: core));
      await t.pumpAndSettle();
      expect(find.text(tr('开始')), findsNWidgets(2));
      expect(find.text(tr('启动游戏')), findsOneWidget);
      for (final kind in ['mod', 'shader', 'resourcepack']) {
        await t.tap(find.byKey(Key('nav-$kind')));
        await t.pumpAndSettle();
        expect(find.text(titleFor(kind)), findsOneWidget);
        expect(find.text('Test Resource'), findsOneWidget);
        expect(find.text('Real resource description'), findsOneWidget);
        expect(find.text(tr('实用工具')), findsWidgets);
        await t.tap(find.text(tr('查看详情')));
        await t.pumpAndSettle();
        expect(find.text(tr('返回列表')), findsOneWidget);
        expect(t.takeException(), isNull);
        await t.tap(find.byKey(const Key('backToCatalog')));
        await t.pumpAndSettle();
      }
      expect(t.takeException(), isNull);
      await t.pumpWidget(const SizedBox());
    });
  }
  testWidgets(
      'settings switches immediately, persists, and restores without losing browse state',
      (t) async {
    viewport(t);
    final core = LanguageCore();
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('nav-mod')));
    await t.pumpAndSettle();
    await t.enterText(find.byKey(const Key('resourceSearch')), 'preserved');
    await t.tap(find.byKey(const Key('settingsButton')));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('languageSelector')));
    await t.pumpAndSettle();
    await t.tap(find.text('繁體中文').last);
    await t.pumpAndSettle();
    expect(core.language, 'zh_Hant');
    expect(find.text('介面語言'), findsOneWidget);
    expect(find.text('模組市集'), findsOneWidget);
    await t.tap(find.text('完成'));
    await t.pumpAndSettle();
    expect(
        t
            .widget<TextField>(find.byKey(const Key('resourceSearch')))
            .controller!
            .text,
        'preserved');
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
    AppLanguage.instance.select('en');
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    expect(AppLanguage.instance.preference, 'zh_Hant');
    expect(find.text('未登入'), findsOneWidget);
    await t.pumpWidget(const SizedBox());
  });
  testWidgets(
      'long English category names wrap and grow instead of being truncated',
      (t) async {
    viewport(t);
    await t.pumpWidget(SeclApp(core: LongCategoryCore()));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('nav-shader')));
    await t.pumpAndSettle();
    final label = find.text('Colored lighting and volumetric effects');
    expect(label, findsOneWidget);
    final text = t.widget<Text>(label);
    expect(text.overflow, isNot(TextOverflow.ellipsis));
    expect(text.maxLines, isNull);
    expect(t.getSize(label).height, greaterThan(30));
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('translated errors retain HTTP status, code and request URL',
      (t) async {
    viewport(t);
    AppLanguage.instance.select('en');
    const error = {
      'message': '连接或请求超时，请手动重试。',
      'code': 'curl_28',
      'http_status': 503,
      'url': 'https://api.modrinth.com/v2/search'
    };
    await t.pumpWidget(
        const MaterialApp(home: Scaffold(body: ProblemPanel(error))));
    expect(find.text('The connection or request timed out. Retry manually.'),
        findsOneWidget);
    expect(find.text('curl_28 · HTTP 503'), findsOneWidget);
    expect(find.text(error['url'] as String), findsOneWidget);
  });
}
