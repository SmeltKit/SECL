import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/project_description.dart';
import 'package:secl/management_widgets.dart';
import 'package:secl/account_manager.dart';

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
    }, action: (_) async {}, onConfigure: () {}))));
    await t.pumpAndSettle();
    expect(find.text('TestPlayer'), findsOneWidget);
    expect(t.takeException(), isNull);
  });
}
