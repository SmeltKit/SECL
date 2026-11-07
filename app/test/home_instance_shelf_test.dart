// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:secl/home_instance_shelf.dart';
import 'package:secl/localization.dart';

List<Map<String, dynamic>> instances() => [
      {
        'id': 'a',
        'name': 'Alpha world',
        'version': '1.20.1',
        'available': true,
        'last_launch': 0,
      },
      {
        'id': 'b',
        'name': 'Beta world',
        'version': '1.16.5',
        'available': true,
        'last_launch': 0,
      },
      {
        'id': 'c',
        'name': 'Never played',
        'version': '1.21.1',
        'available': true,
        'last_launch': 0,
      },
    ];

List<Map<String, dynamic>> historyInstances() => [
      {'id': 'g', 'name': 'Gamma', 'last_launch': 0, 'available': true},
      {
        'id': 'c',
        'name': 'Third recent',
        'last_launch': 200,
        'available': true
      },
      {'id': 'f', 'name': 'beta', 'last_launch': 0, 'available': true},
      {'id': 'a', 'name': 'Newest', 'last_launch': 400, 'available': true},
      {
        'id': 'd',
        'name': 'Oldest recent',
        'last_launch': 100,
        'available': true
      },
      {'id': 'e', 'name': 'Alpha', 'last_launch': 0, 'available': true},
      {
        'id': 'b',
        'name': 'Second recent',
        'last_launch': 300,
        'available': true
      },
      {'id': 'd', 'name': 'Duplicate', 'last_launch': 100, 'available': true},
      {
        'id': '',
        'name': 'Invalid entry',
        'last_launch': 900,
        'available': true
      },
    ];

Widget shelfHarness(
  List<Map<String, dynamic>> data,
  List<String> selections, {
  bool enabled = true,
  String initial = 'a',
}) {
  var selected = initial;
  return MaterialApp(
    home: Scaffold(
      body: Row(
        children: [
          SizedBox(
            width: 260,
            height: 480,
            child: StatefulBuilder(
              builder: (context, setState) => HomeInstanceShelf(
                instances: data,
                selected: selected,
                enabled: enabled,
                onSelected: (id) {
                  selections.add(id);
                  setState(() => selected = id);
                },
                onOpenLibrary: () {},
              ),
            ),
          ),
          const Expanded(
            child: ColoredBox(key: Key('outsideShelf'), color: Colors.white),
          ),
        ],
      ),
    ),
  );
}

Finder card(String id) => find.byKey(Key('shelf-$id'));

Rect paintedCard(WidgetTester tester, String id) {
  final box = tester.renderObject<RenderBox>(card(id));
  return Rect.fromPoints(
    box.localToGlobal(Offset.zero),
    box.localToGlobal(box.size.bottomRight(Offset.zero)),
  );
}

Future<void> wheel(
  WidgetTester tester,
  Offset position,
  double dy, {
  double dx = 0,
}) async {
  await tester.sendEventToBinding(
    PointerScrollEvent(
      kind: PointerDeviceKind.mouse,
      position: position,
      scrollDelta: Offset(dx, dy),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 16));
}

void expectCentered(WidgetTester tester, String id) {
  final list = tester.getRect(find.byKey(const Key('instanceShelfList')));
  final selected = paintedCard(tester, id);
  expect(selected.center.dy, closeTo(list.center.dy, 1));
  expect(selected.center.dx, closeTo(list.center.dx, 1));
}

void main() {
  setUp(() => AppLanguage.instance.select('zh_Hans'));
  tearDown(() => AppLanguage.instance.select('zh_Hans'));

  testWidgets('all games share a sorted five-card carousel with no tabs', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(historyInstances(), selections, initial: 'd'),
    );
    await tester.pumpAndSettle();

    expect(find.byKey(const Key('shelfRecentTab')), findsNothing);
    expect(find.byKey(const Key('shelfAllTab')), findsNothing);
    expect(find.text('Duplicate'), findsNothing);
    expect(find.text('Invalid entry'), findsNothing);
    expect(card('a'), findsNothing);
    expect(card('g'), findsNothing);
    for (final id in ['b', 'c', 'd', 'e', 'f']) {
      expect(card(id), findsOneWidget);
    }
    final centers = [
      for (final id in ['b', 'c', 'd', 'e', 'f']) tester.getCenter(card(id)).dy,
    ];
    for (var i = 1; i < centers.length; i++) {
      expect(centers[i], greaterThan(centers[i - 1]));
    }
    expectCentered(tester, 'd');
    expect(
      paintedCard(tester, 'd').width,
      greaterThan(paintedCard(tester, 'c').width),
    );
    expect(
      paintedCard(tester, 'c').width,
      greaterThan(paintedCard(tester, 'b').width),
    );

    await tester.tap(find.byKey(const Key('shelfNext')));
    await tester.pumpAndSettle();
    expect(selections, ['e']);
    expectCentered(tester, 'e');
    expect(card('b'), findsNothing);
    expect(card('g'), findsOneWidget);
    expect(
      tester.getCenter(card('f')).dy,
      lessThan(tester.getCenter(card('g')).dy),
    );
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('arrows move selection and disappear at each boundary', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(instances(), selections));
    await tester.pumpAndSettle();
    expectCentered(tester, 'a');
    expect(find.byKey(const Key('shelfPrevious')), findsNothing);
    expect(find.byKey(const Key('shelfNext')), findsOneWidget);

    final before = paintedCard(tester, 'b').center.dy;
    await tester.tap(find.byKey(const Key('shelfNext')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 70));
    final moving = paintedCard(tester, 'b').center.dy;
    final center =
        tester.getCenter(find.byKey(const Key('instanceShelfList'))).dy;
    expect(moving, lessThan(before - 1));
    expect(moving, greaterThan(center + 1));
    await tester.pumpAndSettle();
    expectCentered(tester, 'b');
    expect(find.byKey(const Key('shelfPrevious')), findsOneWidget);
    expect(find.byKey(const Key('shelfNext')), findsOneWidget);
    await tester.tap(find.byKey(const Key('shelfNext')));
    await tester.pumpAndSettle();
    expectCentered(tester, 'c');
    expect(find.byKey(const Key('shelfNext')), findsNothing);
    await tester.tap(find.byKey(const Key('shelfPrevious')));
    await tester.pumpAndSettle();
    expectCentered(tester, 'b');
    expect(selections, ['b', 'c', 'b']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('tapping a neighbor recenters the selection after animation', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(instances(), selections));
    await tester.pumpAndSettle();
    expect(
      tester.getCenter(card('b')).dy,
      greaterThan(tester.getCenter(card('a')).dy),
    );
    final before = paintedCard(tester, 'b').center.dy;
    await tester.tap(card('b'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 70));
    final moving = paintedCard(tester, 'b').center.dy;
    final center =
        tester.getCenter(find.byKey(const Key('instanceShelfList'))).dy;
    expect(moving, lessThan(before - 1));
    expect(moving, greaterThan(center + 1));
    await tester.pumpAndSettle();
    expect(selections, ['b']);
    expectCentered(tester, 'b');
    expect(
      tester.getCenter(card('a')).dy,
      lessThan(tester.getCenter(card('b')).dy),
    );
    expect(
      tester.getCenter(card('c')).dy,
      greaterThan(tester.getCenter(card('b')).dy),
    );
    await tester.tap(card('b'));
    await tester.pumpAndSettle();
    expect(selections, ['b'], reason: 'The selected card is already active.');
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('small wheel events move immediately throughout a burst', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    final initial = paintedCard(tester, 'b').center.dy;
    var previous = initial;
    var firstMovement = 0.0;
    for (var event = 0; event < 7; event++) {
      if (event > 0) await tester.pump(const Duration(milliseconds: 40));
      await wheel(tester, area, 14);
      final current = paintedCard(tester, 'b').center.dy;
      expect(
        current,
        lessThan(previous - 1),
        reason: 'Every small event must visibly move the covers.',
      );
      expect(
        selections,
        isEmpty,
        reason: 'A continuous wheel gesture has not settled yet.',
      );
      if (event == 0) firstMovement = initial - current;
      previous = current;
    }
    expect(initial - previous, greaterThan(firstMovement * 5));
    final beforeSnap = paintedCard(tester, 'c').center.dy;
    expect(beforeSnap, greaterThan(area.dy + 1));

    await tester.pump(const Duration(milliseconds: 100));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.pump(const Duration(milliseconds: 60));
    final duringSnap = paintedCard(tester, 'c').center.dy;
    expect(duringSnap, lessThan(beforeSnap - 1));
    expect(duringSnap, greaterThan(area.dy + .1));
    expect(
      selections,
      isEmpty,
      reason: 'Selection is committed when the snap finishes.',
    );
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'c');
    expect(selections, ['c']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
      'wheel motion remains visible when system animations are disabled', (
    tester,
  ) async {
    tester.binding.platformDispatcher.accessibilityFeaturesTestValue =
        const FakeAccessibilityFeatures(disableAnimations: true);
    addTearDown(
      tester.binding.platformDispatcher.clearAccessibilityFeaturesTestValue,
    );
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(instances(), selections));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    final before = paintedCard(tester, 'b').center.dy;

    await wheel(tester, area, 112);
    final firstFrame = paintedCard(tester, 'b').center.dy;
    expect(firstFrame, lessThan(before - 1));
    expect(firstFrame, greaterThan(area.dy + 1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 16));
    final secondFrame = paintedCard(tester, 'b').center.dy;
    expect(secondFrame, lessThan(firstFrame - 1));
    expect(secondFrame, greaterThan(area.dy + 1));
    expect(selections, isEmpty);

    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'b');
    expect(selections, ['b']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('a short wheel gesture snaps back without changing selection', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 32);
    final displaced = paintedCard(tester, 'b').center.dy;
    expect(displaced, lessThan(area.dy - 1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 70));
    final returning = paintedCard(tester, 'b').center.dy;
    expect(returning, greaterThan(displaced + 1));
    expect(returning, lessThan(area.dy - .1));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'b');
    expect(selections, isEmpty);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('wheel reversal immediately moves the covers the other way', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 40);
    await tester.pump(const Duration(milliseconds: 110));
    final forward = paintedCard(tester, 'b').center.dy;
    expect(forward, lessThan(area.dy - 1));
    await wheel(tester, area, -12);
    final reversed = paintedCard(tester, 'b').center.dy;
    expect(reversed, greaterThan(forward + 1));
    expect(reversed, lessThan(area.dy - 1));
    await wheel(tester, area, -100);
    expect(paintedCard(tester, 'b').center.dy, greaterThan(area.dy + 1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'a');
    expect(selections, ['a']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('wheel travel clamps at both ends without wrapping', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(instances(), selections));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, -1000);
    expectCentered(tester, 'a');
    expect(selections, isEmpty);
    await wheel(tester, area, 1000);
    await tester.pump(const Duration(milliseconds: 110));
    expectCentered(tester, 'c');
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'c');
    expect(selections, ['c']);
    await wheel(tester, area, 1000);
    expectCentered(tester, 'c');
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expect(selections, ['c']);
    await wheel(tester, area, -1000);
    await tester.pump(const Duration(milliseconds: 110));
    expectCentered(tester, 'a');
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'a');
    expect(selections, ['c', 'a']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('horizontal and outside wheel input leaves covers still', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    final before = paintedCard(tester, 'b');
    await wheel(tester, area, 100, dx: 120);
    await wheel(tester, area, 0, dx: 100);
    await wheel(tester, area, 100, dx: 100);
    await wheel(
      tester,
      tester.getCenter(find.byKey(const Key('outsideShelf'))),
      100,
    );
    expect(paintedCard(tester, 'b'), before);
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 600));
    expect(paintedCard(tester, 'b'), before);
    expect(selections, isEmpty);
    await wheel(tester, area, 12);
    expect(paintedCard(tester, 'b').center.dy, lessThan(before.center.dy - 1));
    expect(selections, isEmpty);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('disposing during wheel input cancels its pending settlement', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 80);
    expect(paintedCard(tester, 'b').center.dy, lessThan(area.dy - 1));
    expect(selections, isEmpty);
    await tester.pumpWidget(const SizedBox());
    await tester.pump(const Duration(seconds: 1));
    expect(selections, isEmpty);
    expect(tester.takeException(), isNull);
  });

  testWidgets('new input interrupts a snap from its painted position', (
    tester,
  ) async {
    final selections = <String>[];
    final data = instances();
    await tester.pumpWidget(shelfHarness(data, selections, initial: 'b'));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 80);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 70));
    final snapping = paintedCard(tester, 'b').center.dy;
    expect(selections, isEmpty);

    await wheel(tester, area, -14);
    final interrupted = paintedCard(tester, 'b').center.dy;
    expect(interrupted, greaterThan(snapping + 1));
    expect(
      interrupted - snapping,
      lessThan(40),
      reason: 'Interrupting a snap must not jump back to its old origin.',
    );
    await tester.pumpWidget(shelfHarness(data, selections, initial: 'b'));
    expect(
      paintedCard(tester, 'b').center.dy,
      closeTo(interrupted, .1),
      reason: 'A same-order refresh preserves an active gesture.',
    );
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'c');
    expect(selections, ['c']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('an arrow during wheel motion steps from the painted center', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(historyInstances(), selections));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 224);
    await tester.pump(const Duration(milliseconds: 64));
    expect(paintedCard(tester, 'c').center.dy, closeTo(area.dy, 10));
    expect(selections, isEmpty);

    await tester.tap(find.byKey(const Key('shelfNext')));
    expect(selections, ['d'],
        reason: 'The live center is the third row during this wheel motion.');
    await tester.pumpAndSettle();
    expectCentered(tester, 'd');
    expect(selections, ['d']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('tapping the selected card cancels a wheel before its first tick',
      (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(shelfHarness(instances(), selections));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await tester.sendEventToBinding(PointerScrollEvent(
      kind: PointerDeviceKind.mouse,
      position: area,
      scrollDelta: const Offset(0, 112),
    ));
    await tester.tap(card('a'));
    expectCentered(tester, 'a');
    expect(selections, isEmpty);

    await tester.pumpAndSettle();
    await tester.pump(const Duration(milliseconds: 600));
    expectCentered(tester, 'a');
    expect(selections, isEmpty);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('an inertia stop settles without waiting for the idle timer', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 80);
    await tester.pump(const Duration(milliseconds: 100));
    final beforeStop = paintedCard(tester, 'c').center.dy;
    await tester.sendEventToBinding(
      PointerScrollInertiaCancelEvent(
        kind: PointerDeviceKind.mouse,
        position: area,
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 70));
    final settling = paintedCard(tester, 'c').center.dy;
    expect(settling, lessThan(beforeStop - 1));
    expect(settling, greaterThan(area.dy + .1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 200));
    expectCentered(tester, 'c');
    expect(selections, ['c']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('trackpad pan updates move continuously and end with a snap', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, initial: 'b'),
    );
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await tester.sendEventToBinding(
      PointerPanZoomStartEvent(pointer: 7, position: area),
    );
    await tester.sendEventToBinding(
      PointerPanZoomUpdateEvent(
        pointer: 7,
        position: area,
        pan: const Offset(0, -14),
        panDelta: const Offset(0, -14),
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));
    final first = paintedCard(tester, 'b').center.dy;
    expect(first, lessThan(area.dy - 1));
    expect(selections, isEmpty);
    await tester.sendEventToBinding(
      PointerPanZoomUpdateEvent(
        pointer: 7,
        position: area,
        pan: const Offset(0, -80),
        panDelta: const Offset(0, -66),
      ),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 16));
    expect(paintedCard(tester, 'b').center.dy, lessThan(first - 1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 100));
    final beforeEnd = paintedCard(tester, 'c').center.dy;
    await tester.sendEventToBinding(
      PointerPanZoomEndEvent(pointer: 7, position: area),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 70));
    expect(paintedCard(tester, 'c').center.dy, lessThan(beforeEnd - 1));
    expect(selections, isEmpty);
    await tester.pump(const Duration(milliseconds: 200));
    expectCentered(tester, 'c');
    expect(selections, ['c']);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('disabling during wheel input cancels its pending selection', (
    tester,
  ) async {
    final selections = <String>[];
    final data = instances();
    await tester.pumpWidget(shelfHarness(data, selections, initial: 'b'));
    await tester.pumpAndSettle();
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 80);
    expect(paintedCard(tester, 'b').center.dy, lessThan(area.dy - 1));
    await tester.pumpWidget(
      shelfHarness(data, selections, initial: 'b', enabled: false),
    );
    await tester.pump(const Duration(milliseconds: 600));
    expectCentered(tester, 'b');
    expect(selections, isEmpty);
    await tester.pumpWidget(shelfHarness(data, selections, initial: 'b'));
    await tester.pump(const Duration(milliseconds: 600));
    expectCentered(tester, 'b');
    expect(selections, isEmpty);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('keyboard and arrows skip visible unavailable games', (
    tester,
  ) async {
    final selections = <String>[];
    final data = instances();
    data[1]['available'] = false;
    await tester.pumpWidget(shelfHarness(data, selections));
    await tester.pumpAndSettle();
    expect(card('b'), findsOneWidget);
    await tester.tap(card('b'), warnIfMissed: false);
    expect(selections, isEmpty);
    await tester.tap(card('a'));
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowUp);
    expect(selections, isEmpty);
    final before = paintedCard(tester, 'c').center.dy;
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 70));
    final moving = paintedCard(tester, 'c').center.dy;
    final center =
        tester.getCenter(find.byKey(const Key('instanceShelfList'))).dy;
    expect(moving, lessThan(before - 1));
    expect(moving, greaterThan(center + 1));
    await tester.pumpAndSettle();
    expect(selections, ['c']);
    expectCentered(tester, 'c');
    expect(find.byKey(const Key('shelfNext')), findsNothing);
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    expect(selections, ['c']);
    await tester.tap(find.byKey(const Key('shelfPrevious')));
    await tester.pumpAndSettle();
    expect(selections, ['c', 'a']);
    final area = tester.getCenter(find.byKey(const Key('instanceShelfList')));
    await wheel(tester, area, 80);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expectCentered(tester, 'a');
    expect(
      selections,
      ['c', 'a'],
      reason: 'An unavailable cover cannot become the settled selection.',
    );
    await wheel(tester, area, 170);
    expect(selections, ['c', 'a']);
    await tester.pump(const Duration(milliseconds: 160));
    await tester.pump(const Duration(milliseconds: 350));
    expect(selections, ['c', 'a', 'c']);
    expectCentered(tester, 'c');
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets('disabled carousel ignores cards, arrows, wheel and keyboard', (
    tester,
  ) async {
    final selections = <String>[];
    await tester.pumpWidget(
      shelfHarness(instances(), selections, enabled: false, initial: 'b'),
    );
    await tester.pumpAndSettle();
    await tester.tap(card('a'), warnIfMissed: false);
    await tester.tap(card('b'), warnIfMissed: false);
    for (final key in ['shelfPrevious', 'shelfNext']) {
      final arrow = find.byKey(Key(key));
      if (arrow.evaluate().isNotEmpty) await tester.tap(arrow);
    }
    await wheel(
      tester,
      tester.getCenter(find.byKey(const Key('instanceShelfList'))),
      100,
    );
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
    await tester.sendKeyEvent(LogicalKeyboardKey.arrowUp);
    await tester.pumpAndSettle();
    expect(selections, isEmpty);
    expectCentered(tester, 'b');
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });

  testWidgets(
    'an external selection recenters without reporting a new choice',
    (tester) async {
      final selections = <String>[];
      final data = historyInstances();
      await tester.pumpWidget(shelfHarness(data, selections, initial: 'a'));
      await tester.pumpAndSettle();
      expectCentered(tester, 'a');
      await tester.pumpWidget(shelfHarness(data, selections, initial: 'g'));
      await tester.pumpAndSettle();
      expectCentered(tester, 'g');
      expect(card('a'), findsNothing);
      expect(find.byKey(const Key('shelfNext')), findsNothing);
      expect(selections, isEmpty);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    },
  );

  testWidgets('empty and single-game carousels remain safe and inert', (
    tester,
  ) async {
    final selections = <String>[];
    for (final data in [
      <Map<String, dynamic>>[],
      [instances().first],
    ]) {
      await tester.pumpWidget(shelfHarness(data, selections));
      await tester.pumpAndSettle();
      expect(find.byKey(const Key('shelfPrevious')), findsNothing);
      expect(find.byKey(const Key('shelfNext')), findsNothing);
      if (data.isNotEmpty) {
        expectCentered(tester, 'a');
        await tester.tap(card('a'));
      }
      final area = tester.getCenter(find.byKey(const Key('homeInstanceShelf')));
      await wheel(tester, area, 100);
      await tester.pump(const Duration(milliseconds: 220));
      await wheel(tester, area, -100);
      await tester.sendKeyEvent(LogicalKeyboardKey.arrowDown);
      await tester.sendKeyEvent(LogicalKeyboardKey.arrowUp);
      expect(selections, isEmpty);
      expect(tester.takeException(), isNull);
      await tester.pumpWidget(const SizedBox());
    }
  });
}
