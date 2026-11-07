// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/appearance.dart';
import 'package:secl/main.dart';
import 'package:secl/localization.dart';
import 'widget_test.dart' show TestCore;

void viewport(WidgetTester t, {Size size = const Size(880, 640)}) {
  t.view.physicalSize = size;
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
}

ImageProvider backgroundImage(WidgetTester t, String key) {
  final provider = t.widget<Image>(find.byKey(Key(key))).image;
  return provider is ResizeImage ? provider.imageProvider : provider;
}

TestCore readyCore({String language = 'zh_Hans', String appearance = 'light'}) {
  final core = TestCore();
  core.state = {
    ...core.state,
    'language': language,
    'appearance': appearance,
    'home_background': 'auto',
    'versions': [
      {
        'id': 'Everyday survival with a very long instance name',
        'version': '1.20.1',
        'available': true,
      },
      {'id': 'Nether adventure', 'version': '1.16.5', 'available': true}
    ],
    'javas': [
      {
        'path': '/not/a/real/java',
        'version': '17.0.21',
        'vendor': 'Example distribution with a long display name',
        'arch': 'aarch64',
      }
    ],
    'account': {'name': 'ExamplePlayer', 'owned': true},
  };
  return core;
}

void main() {
  setUp(() {
    AppLanguage.instance.select('zh_Hans');
    AppAppearance.instance.select('light', 'blue');
  });
  tearDown(() {
    AppLanguage.instance.select('zh_Hans');
    AppAppearance.instance.select('system', 'blue');
  });

  testWidgets('home scenery fills the main pane and account remains accessible',
      (t) async {
    viewport(t);
    await t.pumpWidget(SeclApp(core: TestCore()));
    await t.pumpAndSettle();
    final background = t.getRect(find.byKey(const Key('homeBackground')));
    expect(background.left, inInclusiveRange(180, 250));
    expect(background.top, 0);
    expect(background.right, 880);
    expect(background.bottom, 640);
    expect(find.byKey(const Key('loginButton')), findsOneWidget);
    expect(t.getBottomRight(find.byKey(const Key('launchButton'))).dx,
        greaterThan(820));
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('compact English dark home keeps long instance and Java usable',
      (t) async {
    viewport(t);
    await t.pumpWidget(
        SeclApp(core: readyCore(language: 'en', appearance: 'dark')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('instanceSelector')), findsOneWidget);
    expect(find.byKey(const Key('javaSelector')), findsOneWidget);
    expect(
        t.widget<FilledButton>(find.byKey(const Key('launchButton'))).onPressed,
        isNotNull);
    expect(t.getBottomRight(find.byKey(const Key('launchButton'))).dy,
        greaterThan(580));
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('changing instance changes the scene and the requested launch',
      (t) async {
    viewport(t, size: const Size(1080, 720));
    final core = readyCore();
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    Image scenery() => t.widget<Image>(find.byKey(const Key('homeBackground')));
    expect((scenery().image as AssetImage).assetName,
        'assets/version-covers/minecraft-1.20.webp');
    await t.tap(find.byKey(const Key('instanceSelector')));
    await t.pumpAndSettle();
    await t.tap(find.text('Nether adventure').last);
    await t.pumpAndSettle();
    expect((scenery().image as AssetImage).assetName,
        'assets/version-covers/minecraft-1.16.webp');
    await t.tap(find.byKey(const Key('launchButton')));
    await t.pumpAndSettle();
    expect(core.requests.where((r) => r['action'] == 'launch').last['version'],
        'Nether adventure');
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('secondary headings follow the selected home instance',
      (t) async {
    viewport(t, size: const Size(1080, 720));
    final core = readyCore();
    await t.pumpWidget(SeclApp(core: core));
    await t.pumpAndSettle();
    String headingAsset() =>
        (backgroundImage(t, 'pageMastheadBackground') as AssetImage).assetName;

    await t.tap(find.byKey(const Key('nav-mod')));
    await t.pumpAndSettle();
    expect(headingAsset(), 'assets/version-covers/minecraft-1.20.webp');
    await t.tap(find.byKey(const Key('nav-home')));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('instanceSelector')));
    await t.pumpAndSettle();
    await t.tap(find.text('Nether adventure').last);
    await t.pumpAndSettle();
    for (final route in [
      'mod',
      'shader',
      'resourcepack',
      'library',
      'java',
      'download'
    ]) {
      await t.tap(find.byKey(Key('nav-$route')));
      await t.pumpAndSettle();
      expect(headingAsset(), 'assets/version-covers/minecraft-1.16.webp',
          reason: route);
    }
    await t.tap(find.byKey(const Key('settingsButton')));
    await t.pumpAndSettle();
    expect(headingAsset(), 'assets/version-covers/minecraft-1.16.webp');
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets(
      'manual home background is shared and no selection hides heading art',
      (t) async {
    viewport(t, size: const Size(1080, 720));
    for (final preference in ['1.18', 'default']) {
      final core = readyCore();
      core.state = {...core.state, 'home_background': preference};
      await t.pumpWidget(SeclApp(core: core));
      await t.pumpAndSettle();
      final home = backgroundImage(t, 'homeBackground');
      await t.tap(find.byKey(const Key('settingsButton')));
      await t.pumpAndSettle();
      expect(backgroundImage(t, 'pageMastheadBackground'), home);
      expect(t.takeException(), isNull);
      await t.pumpWidget(const SizedBox());
    }
    await t.pumpWidget(SeclApp(core: TestCore()));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('settingsButton')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('pageMastheadBackground')), findsNothing);
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });

  testWidgets('many games keep the small carousel clear of launch and Java',
      (t) async {
    viewport(t);
    final core = readyCore(language: 'en', appearance: 'dark');
    core.state = {
      ...core.state,
      'versions': [
        for (var i = 0; i < 9; i++)
          {
            'id': 'instance-$i',
            'name': 'A long game instance name $i',
            'version': i.isEven ? '1.20.1' : '1.16.5',
            'available': true,
          }
      ],
    };
    for (final size in [const Size(880, 640), const Size(1080, 720)]) {
      t.view.physicalSize = size;
      await t.pumpWidget(SeclApp(core: core));
      await t.pumpAndSettle();
      final carousel = t.getRect(find.byKey(const Key('homeCarouselArea')));
      final java = t.getRect(find.byKey(const Key('javaSelector')));
      final launch = t.getRect(find.byKey(const Key('launchButton')));
      expect(carousel.width, lessThanOrEqualTo(216));
      expect(size.width - carousel.right, inInclusiveRange(24, 32));
      expect(carousel.bottom, lessThan(java.top));
      expect(carousel.overlaps(launch), isFalse);
      expect(t.takeException(), isNull);
      await t.pumpWidget(const SizedBox());
    }
  });

  testWidgets('authentication error is readable without displacing launch',
      (t) async {
    viewport(t);
    await t.pumpWidget(SeclApp(core: TestCore()));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('loginButton')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('errorPanel')), findsOneWidget);
    expect(find.textContaining('client_id_missing'), findsOneWidget);
    final launch = t.getRect(find.byKey(const Key('launchButton')));
    expect(launch.right, greaterThan(820));
    expect(launch.bottom, lessThanOrEqualTo(640));
    expect(t.takeException(), isNull);
    await t.pumpWidget(const SizedBox());
  });
}
