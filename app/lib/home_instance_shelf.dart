// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'dart:async';
import 'dart:math' as math;
import 'package:flutter/gestures.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'app_widgets.dart';
import 'appearance.dart';
import 'instance_library.dart';
import 'localization.dart';

/// A scene-side cover carousel. Navigation only changes the selected instance.
class HomeInstanceShelf extends StatefulWidget {
  const HomeInstanceShelf({
    super.key,
    required this.instances,
    required this.selected,
    required this.onSelected,
    this.enabled = true,
    this.onOpenLibrary,
  });

  final List<Map<String, dynamic>> instances;
  final String selected;
  final ValueChanged<String> onSelected;
  final bool enabled;
  final VoidCallback? onOpenLibrary;

  @override
  State<HomeInstanceShelf> createState() => _HomeInstanceShelfState();
}

class _HomeInstanceShelfState extends State<HomeInstanceShelf>
    with SingleTickerProviderStateMixin {
  final _focus = FocusNode();
  late final AnimationController _movement;
  late List<Map<String, dynamic>> _rows;
  late Map<String, int> _indices;
  late String _selected;
  double _from = 0;
  double _to = 0;
  Timer? _wheelIdle;
  bool _wheelActive = false;
  double _wheelTarget = 0;
  double _wheelDirection = 0;
  String? _wheelCommit;
  final _cardCache = <String, ({int signature, Widget child})>{};
  bool _hovered = false;
  bool _focused = false;

  String _id(Map<String, dynamic> row) => (row['id'] ?? '').toString();
  String _name(Map<String, dynamic> row) =>
      (row['name'] ?? row['id'] ?? '').toString();
  int _lastLaunch(Map<String, dynamic> row) {
    final value = row['last_launch'];
    return value is num ? value.toInt() : int.tryParse('$value') ?? 0;
  }

  List<Map<String, dynamic>> _readRows() {
    final unique = <String, Map<String, dynamic>>{};
    for (final row in widget.instances) {
      final id = _id(row);
      if (id.trim().isNotEmpty) unique.putIfAbsent(id, () => row);
    }
    return unique.values.toList()
      ..sort((a, b) {
        final recent = _lastLaunch(b).compareTo(_lastLaunch(a));
        if (recent != 0) return recent;
        final name = _name(a).toLowerCase().compareTo(_name(b).toLowerCase());
        return name != 0 ? name : _id(a).compareTo(_id(b));
      });
  }

  void _indexRows() {
    _indices = {
      for (var index = 0; index < _rows.length; index++)
        _id(_rows[index]): index,
    };
  }

  @override
  void initState() {
    super.initState();
    _selected = widget.selected;
    _rows = _readRows();
    _indexRows();
    _from = _to = (_indices[_selected] ?? 0).toDouble();
    _movement = AnimationController(
      vsync: this,
      // Direct scrolling needs spatial continuity even when decorative
      // animations are shortened by the operating system.
      animationBehavior: AnimationBehavior.preserve,
      duration: const Duration(milliseconds: 280),
    )..addStatusListener(_movementStatus);
  }

  void _movementStatus(AnimationStatus status) {
    if (status != AnimationStatus.completed || !mounted) return;
    final pending = _wheelCommit;
    String? changed;
    setState(() {
      _from = _to;
      if (pending != null) {
        _wheelCommit = null;
        _wheelActive = false;
        final index = _indices[pending];
        if (widget.enabled &&
            index != null &&
            _rows[index]['available'] != false) {
          if (_selected != pending) changed = _selected = pending;
        } else {
          _from = _to = (_indices[_selected] ?? 0).toDouble();
        }
      }
    });
    if (changed != null) widget.onSelected(changed!);
  }

  @override
  void didUpdateWidget(HomeInstanceShelf oldWidget) {
    super.didUpdateWidget(oldWidget);
    final nextRows = _readRows();
    final sameOrder = nextRows.length == _rows.length &&
        Iterable<int>.generate(
          nextRows.length,
        ).every((index) => _id(nextRows[index]) == _id(_rows[index]));
    _rows = nextRows;
    _indexRows();
    _cardCache.removeWhere((id, _) => !_indices.containsKey(id));
    final externallySelected =
        oldWidget.selected != widget.selected && _selected != widget.selected;
    if (externallySelected) _selected = widget.selected;
    if (!sameOrder) {
      // Reconcile by identity when a scan or a launch changes the ordering.
      _cancelWheel();
      _movement.stop();
      _from = _to = (_indices[_selected] ?? 0).toDouble();
    } else if (externallySelected) {
      _cancelWheel();
      _animateTo((_indices[_selected] ?? 0).toDouble());
    }
    if (oldWidget.enabled != widget.enabled) {
      _cancelWheel();
      _movement.stop();
      _from = _to = (_indices[_selected] ?? 0).toDouble();
    }
  }

  @override
  void dispose() {
    _wheelIdle?.cancel();
    _movement.dispose();
    _focus.dispose();
    super.dispose();
  }

  double get _position =>
      _from + (_to - _from) * Curves.easeOutCubic.transform(_movement.value);

  void _animateTo(double target,
      {Duration duration = const Duration(milliseconds: 280),
      bool shortcut = true}) {
    final current = _position;
    _movement.stop();
    // Only distant explicit selections enter from a nearby card. Wheel input
    // always continues from its current painted position, including reversals.
    _from = shortcut && (target - current).abs() > 3
        ? target + (current - target).sign * 2
        : current;
    _to = target;
    _movement.duration = duration;
    _movement.forward(from: 0);
  }

  void _cancelWheel() {
    _wheelIdle?.cancel();
    _wheelIdle = null;
    _wheelActive = false;
    _wheelCommit = null;
    _wheelDirection = 0;
  }

  void _select(Map<String, dynamic> row) {
    if (!widget.enabled || row['available'] == false) return;
    final id = _id(row);
    if (!_indices.containsKey(id)) return;
    final target = _indices[id]!.toDouble();
    final changed = id != _selected;
    _cancelWheel();
    if (!changed && (_position - target).abs() < .001) {
      _movement.stop();
      setState(() => _from = _to = target);
      return;
    }
    setState(() {
      _selected = id;
      _animateTo(target);
    });
    if (changed) widget.onSelected(id);
  }

  Map<String, dynamic>? _neighbor(int direction) {
    var index = _wheelActive
        ? _position.round()
        : _indices[_selected] ?? (direction > 0 ? -1 : _rows.length);
    for (index += direction;
        index >= 0 && index < _rows.length;
        index += direction) {
      final row = _rows[index];
      if (row['available'] != false) return row;
    }
    return null;
  }

  void _step(int direction) {
    if (!widget.enabled) return;
    final next = _neighbor(direction);
    if (next != null) {
      _select(next);
    } else if (_wheelActive) {
      _settleWheel();
    }
  }

  void _scrollBy(Offset delta) {
    if (!widget.enabled ||
        _rows.isEmpty ||
        delta.dy == 0 ||
        delta.dy.abs() <= delta.dx.abs() ||
        !_rows.any((row) => row['available'] != false)) return;
    final continuing = _wheelActive &&
        _wheelCommit == null &&
        _wheelDirection == delta.dy.sign;
    final origin = continuing ? _wheelTarget : _position;
    final target = (origin + delta.dy / 112.0)
        .clamp(0.0, (_rows.length - 1).toDouble())
        .toDouble();
    if (!_wheelActive && (target - _position).abs() < .0001) return;
    _wheelIdle?.cancel();
    _focus.requestFocus();
    setState(() {
      _wheelActive = true;
      _wheelCommit = null;
      _wheelDirection = delta.dy.sign;
      _wheelTarget = target;
      // Retarget every packet instead of dropping events during a gate. A
      // short easing also gives ordinary mouse notches visible travel.
      _animateTo(target,
          duration: const Duration(milliseconds: 110), shortcut: false);
    });
    _wheelIdle = Timer(const Duration(milliseconds: 140), _settleWheel);
  }

  void _settleWheel() {
    _wheelIdle?.cancel();
    _wheelIdle = null;
    if (!mounted || !widget.enabled || !_wheelActive || _rows.isEmpty) return;
    int? nearest;
    var distance = double.infinity;
    for (var index = 0; index < _rows.length; index++) {
      if (_rows[index]['available'] == false) continue;
      final candidate = (index - _wheelTarget).abs();
      if (candidate < distance - .0001 ||
          ((candidate - distance).abs() < .0001 &&
              (_wheelDirection > 0
                  ? index > (nearest ?? -1)
                  : index < (nearest ?? _rows.length)))) {
        nearest = index;
        distance = candidate;
      }
    }
    if (nearest == null) {
      _cancelWheel();
      setState(() => _animateTo((_indices[_selected] ?? 0).toDouble()));
      return;
    }
    setState(() {
      _wheelCommit = _id(_rows[nearest!]);
      _animateTo(nearest.toDouble(),
          duration: const Duration(milliseconds: 220), shortcut: false);
    });
  }

  void _wheel(PointerSignalEvent event) {
    if (event is PointerScrollEvent) {
      _scrollBy(event.scrollDelta);
    } else if (event is PointerScrollInertiaCancelEvent && _wheelActive) {
      _settleWheel();
    }
  }

  KeyEventResult _key(FocusNode node, KeyEvent event) {
    if (!widget.enabled ||
        (event is! KeyDownEvent && event is! KeyRepeatEvent)) {
      return KeyEventResult.ignored;
    }
    if (event.logicalKey == LogicalKeyboardKey.arrowDown) {
      _step(1);
      return KeyEventResult.handled;
    }
    if (event.logicalKey == LogicalKeyboardKey.arrowUp) {
      _step(-1);
      return KeyEventResult.handled;
    }
    return KeyEventResult.ignored;
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
        builder: (context, bounds) {
          final width = math.min(
            bounds.maxWidth.isFinite ? bounds.maxWidth : 216.0,
            220.0,
          );
          final height = math.min(
            bounds.maxHeight.isFinite ? bounds.maxHeight : 430.0,
            430.0,
          );
          final cardWidth = math.min(196.0, math.max(0.0, width - 10));
          final cardHeight = cardWidth * 110 / 196;
          final areaHeight = math.max(0.0, height - 52);
          final previous = widget.enabled ? _neighbor(-1) : null;
          final next = widget.enabled ? _neighbor(1) : null;
          return MouseRegion(
            onEnter: (_) => setState(() => _hovered = true),
            onExit: (_) => setState(() => _hovered = false),
            child: SizedBox(
              key: const Key('homeInstanceShelf'),
              width: width,
              height: height,
              child: Focus(
                focusNode: _focus,
                onKeyEvent: _key,
                onFocusChange: (focused) => setState(() => _focused = focused),
                child: Listener(
                  key: const Key('instanceShelfList'),
                  behavior: HitTestBehavior.opaque,
                  onPointerDown: (_) => _focus.requestFocus(),
                  onPointerSignal: _wheel,
                  onPointerPanZoomUpdate: (event) => _scrollBy(-event.panDelta),
                  onPointerPanZoomEnd: (_) {
                    if (_wheelActive) _settleWheel();
                  },
                  child: Column(
                    children: [
                      _arrow(previous, -1),
                      Expanded(
                        child: _rows.isEmpty
                            ? Center(
                                child: Padding(
                                  padding: const EdgeInsets.all(12),
                                  child: Text(
                                    tr('尚未添加游戏实例'),
                                    textAlign: TextAlign.center,
                                    style: TextStyle(
                                      fontSize: 12,
                                      height: 1.5,
                                      color: muted.withOpacity(.65),
                                    ),
                                  ),
                                ),
                              )
                            : ClipRect(
                                child: ShaderMask(
                                  blendMode: BlendMode.dstIn,
                                  shaderCallback: (rect) =>
                                      const LinearGradient(
                                    begin: Alignment.topCenter,
                                    end: Alignment.bottomCenter,
                                    colors: [
                                      Colors.transparent,
                                      Colors.white,
                                      Colors.white,
                                      Colors.transparent,
                                    ],
                                    stops: [0, .10, .90, 1],
                                  ).createShader(rect),
                                  child: AnimatedBuilder(
                                      animation: _movement,
                                      builder: (context, _) => _covers(
                                          cardWidth, cardHeight, areaHeight)),
                                ),
                              ),
                      ),
                      _arrow(next, 1),
                    ],
                  ),
                ),
              ),
            ),
          );
        },
      );

  Widget _arrow(Map<String, dynamic>? row, int direction) => SizedBox(
        width: 34,
        height: 26,
        child: row == null
            ? null
            : IconButton(
                key: Key(direction < 0 ? 'shelfPrevious' : 'shelfNext'),
                tooltip: '${tr('切换实例')}: ${_name(row)}',
                constraints:
                    const BoxConstraints.tightFor(width: 34, height: 26),
                padding: EdgeInsets.zero,
                style: ButtonStyle(
                  foregroundColor: MaterialStateProperty.resolveWith((states) {
                    final highlighted = _hovered ||
                        _focused ||
                        states.contains(MaterialState.hovered) ||
                        states.contains(MaterialState.focused);
                    return (AppAppearance.instance.dark ? Colors.white : ink)
                        .withOpacity(highlighted ? .8 : .35);
                  }),
                  overlayColor: MaterialStateProperty.all(Colors.transparent),
                ),
                onPressed: () {
                  _focus.requestFocus();
                  _step(direction);
                },
                icon: Icon(
                  direction < 0
                      ? Icons.keyboard_arrow_up_rounded
                      : Icons.keyboard_arrow_down_rounded,
                  size: 22,
                ),
              ),
      );

  Widget _covers(double width, double height, double areaHeight) {
    // Only mount the current and incoming neighborhood, regardless of how
    // many instances are in the library. Keys follow IDs across selections.
    final first = math.max(0, _position.floor() - 2);
    final last = math.min(_rows.length - 1, _position.ceil() + 2);
    final room = ((areaHeight - 278) / 100).clamp(0.0, 1.0).toDouble();
    final nearScale = .66 + .04 * room;
    final farScale = .48 + .02 * room;
    final spacing = .78 + .22 * room;
    final nearOffset = (height * (1 + nearScale) / 2 + 20) * spacing;
    final farOffset =
        nearOffset + (height * (nearScale + farScale) / 2 + 18) * spacing;
    return Stack(
      clipBehavior: Clip.none,
      children: [
        for (var index = first; index <= last; index++)
          Positioned.fill(
            key: ValueKey('shelf-position-${_id(_rows[index])}'),
            child: Align(
              alignment: Alignment.center,
              child: AnimatedBuilder(
                animation: _movement,
                // Covers, labels and image providers stay out of the
                // animation builder; each tick only changes geometry.
                child: _cachedCard(_rows[index], width, height),
                builder: (context, child) {
                  final distance = index - _position;
                  final amount = distance.abs();
                  final scale = amount <= 1
                      ? 1 - (1 - nearScale) * amount
                      : nearScale -
                          (nearScale - farScale) * (amount - 1).clamp(0.0, 1.0);
                  final offset = distance.sign *
                      (amount <= 1
                          ? nearOffset * amount
                          : nearOffset +
                              (farOffset - nearOffset) * (amount - 1));
                  final opacity = amount <= 1
                      ? 1 - .26 * amount
                      : amount <= 2
                          ? .74 - .34 * (amount - 1)
                          : .4 * (1 - (amount - 2) / .4).clamp(0.0, 1.0);
                  final inView = amount <= 2.4 &&
                      offset.abs() - height * scale / 2 < areaHeight / 2;
                  final interactive = inView && amount <= 2 && opacity > .06;
                  final emphasis = (1 - amount).clamp(0.0, 1.0).toDouble();
                  return Transform.translate(
                    offset: Offset(0, offset),
                    child: Transform.scale(
                      scale: scale,
                      child: IgnorePointer(
                        ignoring: !interactive,
                        child: ExcludeSemantics(
                          excluding: !interactive,
                          child: Opacity(
                            opacity: inView
                                ? opacity * (widget.enabled ? 1 : .6)
                                : 0,
                            child: DecoratedBox(
                              position: DecorationPosition.foreground,
                              decoration: BoxDecoration(
                                borderRadius: BorderRadius.circular(10),
                                border: Border.all(
                                  width: 1.1,
                                  color: Color.lerp(
                                    accent,
                                    Colors.white,
                                    .55,
                                  )!
                                      .withOpacity(.85 * emphasis),
                                ),
                                boxShadow: [
                                  BoxShadow(
                                    color: Colors.black.withOpacity(
                                      .04 + .1 * emphasis,
                                    ),
                                    blurRadius: 9.0 + 7.0 * emphasis,
                                    offset: const Offset(0, 4),
                                  ),
                                ],
                              ),
                              child: child,
                            ),
                          ),
                        ),
                      ),
                    ),
                  );
                },
              ),
            ),
          ),
      ],
    );
  }

  Widget _cachedCard(Map<String, dynamic> row, double width, double height) {
    final id = _id(row);
    final signature = Object.hash(
        id,
        _name(row),
        row['version'],
        row['loader'],
        row['cover'],
        row['available'],
        _selected == id,
        widget.enabled,
        width,
        height);
    final cached = _cardCache[id];
    if (cached != null && cached.signature == signature) return cached.child;
    // Reuse the cover/image subtree through fractional scrolling frames.
    final child = _card(row, width, height);
    _cardCache[id] = (signature: signature, child: child);
    // Retain only a small neighborhood when traversing a large library.
    if (_cardCache.length > 12) {
      _cardCache
          .removeWhere((key, _) => (_indices[key]! - _position).abs() > 4);
    }
    return child;
  }

  Widget _card(Map<String, dynamic> row, double width, double height) {
    final id = _id(row);
    final active = id == _selected;
    final available = widget.enabled && row['available'] != false;
    final version = (row['version'] ?? '').toString();
    final rawLoader = (row['loader'] ?? '').toString();
    final loader = const {
          'fabric': 'Fabric',
          'forge': 'Forge',
          'neoforge': 'NeoForge',
          'quilt': 'Quilt',
        }[rawLoader.toLowerCase()] ??
        rawLoader;
    final detail = [
      version,
      if (rawLoader.toLowerCase() != 'vanilla') loader,
    ].where((part) => part.isNotEmpty).join(' · ');
    void select() {
      _focus.requestFocus();
      _select(row);
    }

    return Semantics(
      button: true,
      selected: active,
      enabled: available,
      label: '${_name(row)}${detail.isEmpty ? '' : ', $detail'}',
      onTap: available ? select : null,
      excludeSemantics: true,
      child: GestureDetector(
        key: Key('shelf-$id'),
        behavior: HitTestBehavior.opaque,
        excludeFromSemantics: true,
        onTap: available ? select : null,
        child: SizedBox(
          width: width,
          height: height,
          child: RepaintBoundary(
            child: ClipRRect(
              borderRadius: BorderRadius.circular(10),
              child: Stack(
                fit: StackFit.expand,
                children: [
                  InstanceCover(row, height: height),
                  const DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.topCenter,
                        end: Alignment.bottomCenter,
                        colors: [
                          Colors.transparent,
                          Color(0x14000000),
                          Color(0xb8000000),
                        ],
                        stops: [0, .4, 1],
                      ),
                    ),
                  ),
                  if (!available) const ColoredBox(color: Color(0x38000000)),
                  Positioned(
                    left: 11,
                    right: 11,
                    bottom: 9,
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Text(
                          _name(row),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: const TextStyle(
                            fontSize: 14,
                            height: 1.25,
                            fontWeight: FontWeight.w600,
                            color: Colors.white,
                          ),
                        ),
                        if (active && detail.isNotEmpty) ...[
                          const SizedBox(height: 2),
                          Text(
                            detail,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: TextStyle(
                              fontSize: 10,
                              height: 1.2,
                              color: Colors.white.withOpacity(.9),
                            ),
                          ),
                        ],
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}
