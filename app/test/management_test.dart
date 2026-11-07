import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/project_description.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/account_manager.dart';
import 'package:secl/app_widgets.dart';
import 'package:secl/appearance.dart';
import 'package:secl/core_bridge.dart';
import 'package:secl/instance_library.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_engine.dart';

class LibraryLayoutCore implements LauncherCore {
  @override
  Future<Map<String, dynamic>> call(Map<String, dynamic> request) async => {
        'busy': false,
        'result': {
          'instances': [
            {
              'id': 'everyday',
              'name': 'Everyday survival with a long display name',
              'version': '1.20.1',
              'loader': 'fabric',
              'loader_version': '0.16.10',
              'path': '/example/instances/everyday'
            }
          ]
        }
      };
}

void main() {
  test('description preserves nested markup and rejects active content', () {
    final html = projectHtml(
        '# Title\n\n> quoted text\n\n<div><details><summary>More</summary>Retained text</details></div>\n\n<script>alert(1)</script>\n<img src="http://example.com/a.png" onerror="alert(2)">',
        Uri.parse('https://modrinth.com/mod/test'));
    expect(html, contains('Retained text'));
    expect(html, contains('<blockquote>'));
    expect(html, contains('<h1>'));
    expect(html, contains('https://example.com/a.png'));
    expect(html, isNot(contains('onerror')));
    expect(html, isNot(contains('<script>')));
    expect(projectUrl('file:///etc/passwd', Uri.parse('https://example.com')),
        isNull);
    expect(projectUrl('https://127.0.0.1/a', Uri.parse('https://example.com')),
        isNull);
  });
  testWidgets('shared table uses one column geometry for metadata and content',
      (t) async {
    await t.pumpWidget(const MaterialApp(
        home: Scaffold(
            body: ManagementTable(headers: [
      Text('Name'),
      Text('Status'),
      Text('Actions')
    ], widths: {
      2: FixedColumnWidth(80)
    }, rows: [
      [Text('Sodium'), Text('Enabled'), Icon(Icons.more_horiz)]
    ]))));
    expect(t.getTopLeft(find.text('Status')).dx,
        t.getTopLeft(find.text('Enabled')).dx);
    expect(t.takeException(), isNull);
  });
  testWidgets('account page fits a compact window and uses square game avatar',
      (t) async {
    t.view.physicalSize = const Size(680, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    await t.pumpWidget(MaterialApp(
        home: Scaffold(
            body: AccountManager(snapshot: const {
      'accounts': [
        {
          'id': 'microsoft:test',
          'name': 'TestPlayer',
          'uuid': 'test',
          'remembered': false,
          'needs_login': true
        }
      ]
    }, action: (_) async {}))));
    await t.pumpAndSettle();
    expect(find.text('TestPlayer'), findsOneWidget);
    expect(t.takeException(), isNull);
  });
  testWidgets(
      'page heading wraps English actions and preserves the shared footer',
      (t) async {
    t.view.physicalSize = const Size(880, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    addTearDown(() => AppAppearance.instance.select('system', 'blue'));
    for (final dark in [false, true]) {
      AppAppearance.instance.select(dark ? 'dark' : 'light', 'blue');
      for (final width in [488.0, 656.0]) {
        await t.pumpWidget(MaterialApp(
            theme: ThemeData(
                platform: TargetPlatform.macOS,
                useMaterial3: true,
                brightness: dark ? Brightness.dark : Brightness.light),
            home: Scaffold(
                body: Align(
                    alignment: Alignment.topLeft,
                    child: SizedBox(
                        width: width,
                        height: 640,
                        child: ManagementPage(
                            title: 'Java runtime environments',
                            subtitle: 'Each instance selects its own runtime.',
                            actions: [
                              OutlinedButton(
                                  key: const Key('headingImport'),
                                  onPressed: () {},
                                  child: const Text('Import a local runtime')),
                              FilledButton(
                                  key: const Key('headingInstall'),
                                  onPressed: () {},
                                  child: const Text('Install Java')),
                            ],
                            body: const SizedBox(),
                            footer: const Text('Footer')))))));
        await t.pumpAndSettle();
        for (final key in ['headingImport', 'headingInstall']) {
          final bounds = t.getRect(find.byKey(Key(key)));
          expect(bounds.left, greaterThanOrEqualTo(28));
          expect(bounds.right, lessThanOrEqualTo(width - 28));
        }
        expect(t.getSize(find.byType(FooterBar)).height, 88);
        expect(t.takeException(), isNull);
      }
    }
  });

  testWidgets('library cover, compact menu and launch keep their actions',
      (t) async {
    t.view.physicalSize = const Size(880, 640);
    t.view.devicePixelRatio = 1;
    addTearDown(t.view.resetPhysicalSize);
    addTearDown(t.view.resetDevicePixelRatio);
    AppLanguage.instance.select('en');
    addTearDown(() => AppLanguage.instance.select('zh_Hans'));
    final engine = ResourceEngine(LibraryLayoutCore());
    addTearDown(engine.dispose);
    final opened = <String>[];
    var launched = 0;
    await t.pumpWidget(MaterialApp(
        theme: ThemeData(useMaterial3: true),
        home: Scaffold(
            body: SizedBox(
                width: 656,
                child: InstanceLibrary(
                    engine: engine,
                    root: '/example',
                    onOpen: (_, tab) => opened.add(tab),
                    onInstall: () {},
                    onLaunch: (_) => launched++)))));
    await t.pumpAndSettle();
    await t.tap(find.byKey(const ValueKey('library-cover-everyday')));
    expect(opened, ['mod']);
    await t.tap(find.byKey(const ValueKey('library-menu-everyday')));
    await t.pumpAndSettle();
    await t.tap(find.text(tr('管理实例')));
    await t.pumpAndSettle();
    expect(opened, ['mod', 'mod']);
    await t.tap(find.byKey(const ValueKey('library-launch-everyday')));
    expect(launched, 1);
    expect(t.takeException(), isNull);
  });
}
