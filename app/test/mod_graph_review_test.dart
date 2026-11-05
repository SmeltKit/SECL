// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/localization.dart';
import 'package:secl/resource_market.dart';

Data incompatible(String project, [String? version]) => {
      'project_id': project.isEmpty ? null : project,
      'version_id': version,
      'dependency_type': 'incompatible'
    };
Data file(String project, String version, String title,
        {bool root = false,
        bool optional = false,
        bool disabled = false,
        List<Data> dependencies = const [],
        Data? replaces}) =>
    {
      'project_id': project,
      'version_id': '$project-$version',
      'version': version,
      'title': title,
      'filename': '$project-$version.jar',
      'root': root,
      'optional': optional,
      'install_disabled': disabled,
      'dependencies': dependencies,
      if (replaces != null) 'replaces': replaces
    };
Data installed(String project, String version, String title,
        {bool enabled = true, List<Data> dependencies = const []}) =>
    {
      ...file(project, version, title, dependencies: dependencies),
      'source': 'existing',
      'enabled': enabled,
      'filename': '$project-$version.jar${enabled ? '' : '.disabled'}',
      'embedded': false
    };
Data plan(List<Data> files,
        {List<Data> existing = const [],
        List<Data> reuse = const [],
        List<Data> edges = const [],
        Data? optifine}) =>
    {
      'root_project': 'main',
      'root_projects': ['main'],
      'target': {
        'id': 'Graph review',
        'version': '1.20.1',
        'loader': 'fabric',
        if (optifine != null) 'optifine': optifine
      },
      'files': files,
      'reuse': reuse,
      'edges': edges,
      'marketplace_existing': existing,
      'graph_conflicts': [],
      'conflicts': []
    };
Future<void> showReview(WidgetTester tester, Data value,
    {ValueChanged<Data?>? onChoice}) async {
  tester.view.physicalSize = const Size(860, 700);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  await tester.pumpWidget(MaterialApp(
      home: Scaffold(
          body: Builder(
              builder: (context) => TextButton(
                  onPressed: () async {
                    final choice = await showDialog<Data>(
                        context: context,
                        builder: (_) =>
                            InstallPlanDialog(plan: value, kind: 'mod'));
                    onChoice?.call(choice);
                  },
                  child: const Text('Show'))))));
  await tester.tap(find.text('Show'));
  await tester.pumpAndSettle();
}

bool canApply(WidgetTester tester) =>
    tester
        .widget<FilledButton>(find.byKey(const Key('applyInstall')))
        .onPressed !=
    null;
Future<void> tapKey(WidgetTester tester, String key) async {
  final finder = find.byKey(Key(key));
  await tester.ensureVisible(finder);
  await tester.tap(finder);
  await tester.pumpAndSettle();
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));
  testWidgets(
      'optional selection includes required transitives and clears their conflicts',
      (tester) async {
    final value = plan([
      file('main', '1', 'Main', root: true),
      file('optional', '2', 'Optional', optional: true),
      file('transitive', '3', 'Transit',
          optional: true, dependencies: [incompatible('main')])
    ], edges: [
      {'from': 'main', 'to': 'optional', 'optional': true},
      {'from': 'optional', 'to': 'transitive', 'optional': false}
    ]);
    await showReview(tester, value);
    expect(canApply(tester), isTrue);
    expect(
        find.byKey(const Key('graphConflict-transitive-main')), findsNothing);
    await tapKey(tester, 'planComponent-optional');
    expect(
        tester
            .widget<Checkbox>(find.byKey(const Key('planComponent-transitive')))
            .value,
        isTrue);
    expect(find.text('Transit · 3 与 Main · 1 不兼容'), findsOneWidget);
    expect(canApply(tester), isFalse);
    await tapKey(tester, 'planComponent-optional');
    expect(
        tester
            .widget<Checkbox>(find.byKey(const Key('planComponent-transitive')))
            .value,
        isFalse);
    expect(
        find.byKey(const Key('graphConflict-transitive-main')), findsNothing);
    expect(canApply(tester), isTrue);
    expect(tester.takeException(), isNull);
  });
  testWidgets(
      'reverse existing edge offers live reversible disable consent only for selected branch',
      (tester) async {
    Data? choice;
    final old = installed('old', '4', 'Installed',
        dependencies: [incompatible('optional')]);
    final value = plan([
      file('main', '1', 'Main', root: true),
      file('optional', '2', 'Optional', optional: true)
    ], existing: [
      old
    ], edges: [
      {'from': 'main', 'to': 'optional', 'optional': true}
    ]);
    await showReview(tester, value, onChoice: (result) => choice = result);
    expect(find.byKey(const Key('disableGraph-old-4.jar')), findsNothing);
    await tapKey(tester, 'planComponent-optional');
    expect(find.text('Installed · 4 与 Optional · 2 不兼容'), findsOneWidget);
    expect(canApply(tester), isFalse);
    await tapKey(tester, 'disableGraph-old-4.jar');
    expect(canApply(tester), isTrue);
    expect(
        tester
            .widget<CheckboxListTile>(
                find.byKey(const Key('disableGraph-old-4.jar')))
            .value,
        isTrue);
    await tapKey(tester, 'planComponent-optional');
    expect(find.byKey(const Key('disableGraph-old-4.jar')), findsNothing);
    expect(canApply(tester), isTrue);
    await tester.tap(find.byKey(const Key('applyInstall')));
    await tester.pumpAndSettle();
    expect(choice?['disable'], isEmpty,
        reason:
            'removing the conflict must not silently disable an unrelated existing file');
    expect(tester.takeException(), isNull);
  });
  testWidgets(
      'deselecting an optional replacement restores the old versions outgoing declaration',
      (tester) async {
    final old = installed('replace', '1', 'Old copy',
        dependencies: [incompatible('main')]);
    final value = plan([
      file('main', '1', 'Main', root: true),
      file('replace', '2', 'Replacement', optional: true, replaces: old)
    ], existing: [
      old
    ], edges: [
      {'from': 'main', 'to': 'replace', 'optional': true}
    ]);
    await showReview(tester, value);
    expect(find.byKey(const Key('disableGraph-replace-1.jar')), findsOneWidget);
    expect(canApply(tester), isFalse);
    await tapKey(tester, 'planComponent-replace');
    expect(find.byKey(const Key('disableGraph-replace-1.jar')), findsNothing);
    expect(canApply(tester), isTrue);
    await tapKey(tester, 'planComponent-replace');
    expect(find.byKey(const Key('disableGraph-replace-1.jar')), findsOneWidget);
    expect(canApply(tester), isFalse);
    expect(tester.takeException(), isNull);
  });
  testWidgets(
      'managed OptiFine conflict can be resolved by deselecting optional Iris',
      (tester) async {
    final value = plan([
      file('main', '1', 'Main', root: true),
      file('iris', '2', 'Iris', optional: true)
    ], optifine: {
      'modFile': 'OptiFine.jar',
      'version': 'HD U I6',
      'build': 'legacy build'
    }, edges: [
      {'from': 'main', 'to': 'iris', 'optional': true}
    ]);
    value['local_graph_files'] = [
      {
        'filename': 'OptiFine.jar',
        'enabled': true,
        'ids': ['optifine']
      }
    ];
    await showReview(tester, value);
    expect(canApply(tester), isTrue);
    await tapKey(tester, 'planComponent-iris');
    expect(
        find.byKey(const Key('graphConflict-iris-optifine')), findsOneWidget);
    expect(find.byKey(const Key('disableGraph-OptiFine.jar')), findsNothing);
    expect(find.textContaining('版本组件中移除 OptiFine'), findsOneWidget);
    expect(find.textContaining('OptiFine · HD U I6'), findsOneWidget);
    expect(canApply(tester), isFalse);
    await tapKey(tester, 'planComponent-iris');
    expect(find.byKey(const Key('graphConflict-iris-optifine')), findsNothing);
    expect(canApply(tester), isTrue);
    expect(tester.takeException(), isNull);
  });
  testWidgets(
      'disabled installations stay outside graph while enabling an optional reuse activates it',
      (tester) async {
    final reused = installed('reuse', '2', 'Reused',
        enabled: false, dependencies: [incompatible('main')]);
    final value = plan([
      file('main', '1', 'Main',
          root: true, dependencies: [incompatible('disabled')]),
      file('disabled', '2', 'Disabled update', disabled: true)
    ], existing: [
      reused
    ], reuse: [
      {
        ...reused,
        'file': file('reuse', '2', 'Reused',
            optional: true, dependencies: [incompatible('main')])
      }
    ], edges: [
      {'from': 'main', 'to': 'reuse', 'optional': true},
      {'from': 'main', 'to': 'disabled', 'optional': false}
    ]);
    await showReview(tester, value);
    expect(canApply(tester), isTrue);
    await tapKey(tester, 'enableComponent-reuse');
    expect(
        tester
            .widget<Checkbox>(find.byKey(const Key('planComponent-reuse')))
            .value,
        isTrue);
    expect(find.byKey(const Key('disableGraph-reuse-2.jar.disabled')),
        findsOneWidget);
    expect(canApply(tester), isFalse);
    await tapKey(tester, 'planComponent-reuse');
    expect(find.byKey(const Key('disableGraph-reuse-2.jar.disabled')),
        findsNothing);
    expect(
        tester
            .widget<Checkbox>(find.byKey(const Key('enableComponent-reuse')))
            .value,
        isFalse);
    expect(canApply(tester), isTrue);
    expect(tester.takeException(), isNull);
  });
  for (final incoming in [false, true]) {
    testWidgets(
        '${incoming ? 'incoming' : 'outgoing'} embedded website exclusions are nonblocking and follow optional selection',
        (tester) async {
      final one = {
        ...installed('library', '1', 'Library one',
            dependencies: incoming ? [] : [incompatible('optional')]),
        'filename': 'first.jar',
        'embedded': true,
        'possible': true,
        'version': 'parent-version',
        'version_number': 'website-1.0'
      };
      final two = {
        ...installed('library', '2', 'Library two',
            dependencies: incoming ? [] : [incompatible('optional')]),
        'filename': 'second.jar',
        'embedded': true,
        'possible': true,
        'version_number': 'website-2.0'
      };
      final value = plan([
        file('main', '1', 'Main', root: true),
        file('optional', '2', 'Optional',
            optional: true,
            dependencies: incoming ? [incompatible('', 'library-1')] : [])
      ], existing: [
        one,
        two
      ], edges: [
        {'from': 'main', 'to': 'optional', 'optional': true}
      ]);
      await showReview(tester, value);
      expect(find.text(tr('内嵌组件选择待确认')), findsNothing);
      await tapKey(tester, 'planComponent-optional');
      expect(find.text(tr('内嵌组件选择待确认')), findsOneWidget);
      final warning = find.byKey(Key(incoming
          ? 'graphWarning-optional-optional-2-library-library-1'
          : 'graphWarning-library-library-1-optional-optional-2'));
      expect(warning, findsOneWidget);
      final text = tester
          .widget<Text>(
              find.descendant(of: warning, matching: find.byType(Text)))
          .data!;
      expect(text, contains('Library one · website-1.0'));
      expect(text, contains('Optional · 2'));
      expect(text, isNot(contains('parent-version')));
      if (!incoming) {
        expect(find.byKey(const Key('graphWarning-library-library-2-optional-optional-2')), findsOneWidget);
      }
      expect(find.byKey(const Key('disableGraph-first.jar')), findsNothing);
      expect(canApply(tester), isTrue);
      await tapKey(tester, 'planComponent-optional');
      expect(warning, findsNothing);
      expect(canApply(tester), isTrue);
      expect(tester.takeException(), isNull);
    });
  }
  for (final language in ['zh_Hans', 'zh_Hant', 'en']) {
    testWidgets(
        '$language candidate conflicts show both titles and versions without overflowing',
        (tester) async {
      AppLanguage.instance.select(language);
      await showReview(
          tester,
          plan([
            file(
                'main', '1.20.1-release', 'First component with a longer title',
                root: true,
                dependencies: [incompatible('', 'other-2.6.0-beta.3')]),
            file(
                'other', '2.6.0-beta.3', 'Second component with a longer title')
          ], edges: [
            {'from': 'main', 'to': 'other', 'optional': false}
          ]));
      expect(find.byKey(const Key('graphConflict-main-other')), findsOneWidget);
      final labels = tester
          .widgetList<Text>(find.descendant(
              of: find.byKey(const Key('graphConflict-main-other')),
              matching: find.byType(Text)))
          .map((text) => text.data ?? '')
          .join(' ');
      expect(labels,
          contains('First component with a longer title · 1.20.1-release'));
      expect(labels,
          contains('Second component with a longer title · 2.6.0-beta.3'));
      expect(canApply(tester), isFalse);
      expect(tester.takeException(), isNull);
    });
  }
}
