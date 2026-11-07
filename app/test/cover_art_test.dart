// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/cover_art.dart';
import 'package:secl/localization.dart';

void main() {
  test(
      'version families preserve release boundaries and unknown versions use the default',
      () {
    expect(versionCoverAsset('1.20.1-forge-47.2.0'),
        'assets/version-covers/minecraft-1.20.webp');
    expect(versionCoverAsset('26.1.2'),
        'assets/version-covers/minecraft-26.1.webp');
    for (final version in ['1.200', '1.22', '26.4', '24w14a', 'My modpack']) {
      expect(versionCoverAsset(version), defaultCoverAsset);
    }
    expect(homeBackgroundAsset('1.16', '1.20.1'),
        'assets/version-covers/minecraft-1.16.webp');
  });

  testWidgets(
      'background picker previews, cancels and applies in a compact window',
      (t) async {
    AppLanguage.instance.select('zh_Hans');
    t.view.physicalSize = const Size(880, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    String? choice;
    await t.pumpWidget(MaterialApp(
        home: Builder(
            builder: (context) => Scaffold(
                body: TextButton(
                    onPressed: () async {
                      choice = await showBackgroundPicker(context,
                          current: 'auto', version: '1.20.1');
                    },
                    child: const Text('Open'))))));
    await t.tap(find.text('Open'));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('background-default')));
    await t.pumpAndSettle();
    await t.tap(find.text('取消'));
    await t.pumpAndSettle();
    expect(choice, isNull);
    await t.tap(find.text('Open'));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const Key('background-default')));
    await t.tap(find.byKey(const Key('applyBackground')));
    await t.pumpAndSettle();
    expect(choice, 'default');
    expect(t.takeException(), isNull);
    for (final family in versionCoverFamilies) {
      final bytes = await rootBundle.load(versionCoverAsset(family));
      expect(bytes.lengthInBytes, greaterThan(100000), reason: family);
    }
  });
}
