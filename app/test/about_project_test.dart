// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/about_project.dart';
import 'package:secl/appearance.dart';
import 'package:secl/localization.dart';

Future<void> openAbout(WidgetTester t,
    {Size viewport = const Size(880, 640)}) async {
  t.view.physicalSize = viewport;
  t.view.devicePixelRatio = 1;
  addTearDown(t.view.resetPhysicalSize);
  addTearDown(t.view.resetDevicePixelRatio);
  await t.pumpWidget(MaterialApp(
      theme: ThemeData(
          useMaterial3: true,
          brightness:
              AppAppearance.instance.dark ? Brightness.dark : Brightness.light),
      home: Builder(
          builder: (context) => Scaffold(
              body: TextButton(
                  onPressed: () => showProjectAboutDialog(context),
                  child: const Text('Open'))))));
  await t.tap(find.text('Open'));
  await t.pumpAndSettle();
}

void main() {
  setUp(() {
    AppLanguage.instance.select('zh_Hans');
    AppAppearance.instance.select('light', 'blue');
    LicenseRegistry.reset();
    LicenseRegistry.addLicense(() async* {
      yield const LicenseEntryWithLineBreaks([
        'Example library',
        'Example alias'
      ], 'Copyright Example Authors\n\nPermission is granted to use this library.');
      yield const LicenseEntryWithLineBreaks(['Example library'],
          'Additional legal notice for the same component.');
      yield const LicenseEntryWithLineBreaks(
          ['Another component'], 'The complete independent component notice.');
    });
  });
  tearDown(() {
    LicenseRegistry.reset();
    AppLanguage.instance.select('zh_Hans');
    AppAppearance.instance.select('system', 'blue');
  });

  testWidgets('about uses project branding and keeps legal information compact',
      (t) async {
    await openAbout(t);
    expect(find.byKey(const Key('aboutBrandMark')), findsOneWidget);
    expect(find.text('SECL'), findsOneWidget);
    expect(find.text('SUPREME ETERNITY CRAFT LAUNCHER'), findsOneWidget);
    expect(find.text('Copyright © 2026 ppm203'), findsOneWidget);
    expect(find.text(tr('基于 M²CL 迁移与重写')), findsOneWidget);
    expect(find.text(tr('GPL-3.0-only + 第 7 节附加条款')), findsOneWidget);
    expect(find.byType(AboutDialog), findsNothing);
    expect(t.takeException(), isNull);
    await t.tap(find.byKey(const Key('closeAbout')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('projectAboutDialog')), findsNothing);
  });

  testWidgets(
      'licenses are searchable and preserve every notice for a component',
      (t) async {
    await openAbout(t);
    await t.tap(find.byKey(const Key('viewProjectLicenses')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('projectLicensesDialog')), findsOneWidget);
    expect(find.byType(LicensePage), findsNothing);
    expect(find.text('Example alias'), findsOneWidget);
    await t.enterText(
        find.byKey(const Key('projectLicenseSearch')), 'Example library');
    await t.pumpAndSettle();
    expect(find.text('Another component'), findsNothing);
    await t.tap(find.byKey(const ValueKey('project-license-Example library')));
    await t.pumpAndSettle();
    expect(find.text('Copyright Example Authors'), findsOneWidget);
    expect(find.text('Permission is granted to use this library.'),
        findsOneWidget);
    expect(find.text('Additional legal notice for the same component.'),
        findsOneWidget);
    expect(find.byType(SelectableText), findsAtLeastNWidgets(3));
    await t.tap(find.byKey(const Key('backToProjectLicenses')));
    await t.pumpAndSettle();
    expect(
        t
            .widget<TextField>(find.byKey(const Key('projectLicenseSearch')))
            .controller!
            .text,
        'Example library');
    await t.enterText(
        find.byKey(const Key('projectLicenseSearch')), 'unmatched');
    await t.pumpAndSettle();
    expect(find.text(tr('没有匹配的组件')), findsOneWidget);
    await t.tap(find.byKey(const Key('closeProjectLicenses')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('projectAboutDialog')), findsOneWidget);
    expect(t.takeException(), isNull);
  });

  testWidgets(
      'compact English dark dialogs retain closing and reading controls',
      (t) async {
    AppLanguage.instance.select('en');
    AppAppearance.instance.select('dark', 'blue');
    await openAbout(t, viewport: const Size(520, 480));
    expect(t.takeException(), isNull);
    await t.tap(find.byKey(const Key('viewProjectLicenses')));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const ValueKey('project-license-Example library')));
    await t.pumpAndSettle();
    expect(find.byKey(const Key('backToProjectLicenses')), findsOneWidget);
    expect(find.byKey(const Key('closeProjectLicenses')), findsOneWidget);
    expect(t.takeException(), isNull);
  });
}
