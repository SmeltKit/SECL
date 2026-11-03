import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'resource_engine.dart';

typedef Data = Map<String, dynamic>;
List<Data> dataRows(dynamic value) => (value as List? ?? [])
    .map((v) => Map<String, dynamic>.from(v as Map))
    .toList();

mixin ManagedState<T extends StatefulWidget> on State<T> {
  ResourceEngine get engine;
  bool loading = false;
  Data? problem;
  Future<void> Function()? retryWork;
  bool get locked => loading || engine.active;
  Future<void> perform(Future<void> Function() work) async {
    if (loading) return;
    setState(() {
      loading = true;
      problem = null;
      retryWork = work;
    });
    try {
      await work();
    } on EngineFailure catch (e) {
      if (mounted) setState(() => problem = e.details);
    } catch (_) {
      if (mounted) {
        setState(() => problem = {
              'code': 'invalid_result',
              'message': tr('无法读取操作结果，请重试。')
            });
      }
    } finally {
      if (mounted) setState(() => loading = false);
    }
  }

  Future<dynamic> rpc(Data request) => engine.run(request);
  Widget result(Widget child) => problem != null
      ? ProblemPanel(problem!, retry: () => perform(retryWork!))
      : loading
          ? const BusyPanel()
          : child;
}

class ManagementPage extends StatelessWidget {
  const ManagementPage(
      {super.key,
      required this.title,
      required this.body,
      this.subtitle,
      this.actions = const [],
      this.onBack,
      this.footer});
  final String title;
  final String? subtitle;
  final Widget body;
  final List<Widget> actions;
  final VoidCallback? onBack;
  final Widget? footer;
  @override
  Widget build(BuildContext context) =>
      Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
        Padding(
            padding: EdgeInsets.fromLTRB(
                28,
                Theme.of(context).platform == TargetPlatform.macOS ? 44 : 24,
                28,
                20),
            child:
                Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
              if (onBack != null)
                Padding(
                    padding: const EdgeInsets.only(bottom: 14),
                    child: TextButton.icon(
                        onPressed: onBack,
                        icon: const Icon(Icons.arrow_back_rounded, size: 18),
                        label: Text(tr('返回游戏库')))),
              Row(children: [
                Expanded(
                    child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                      Text(title,
                          style: const TextStyle(
                              fontSize: 25, fontWeight: FontWeight.w600)),
                      if (subtitle != null) ...[
                        const SizedBox(height: 8),
                        Text(subtitle!,
                            style: TextStyle(fontSize: 13, color: muted))
                      ]
                    ])),
                ...actions
              ]),
            ])),
        Expanded(
            child: Padding(
                padding: const EdgeInsets.symmetric(horizontal: 28),
                child: body)),
        if (footer != null)
          FooterBar(child: footer!)
        else
          const SizedBox(height: 24)
      ]);
}

class ManagementTable extends StatelessWidget {
  const ManagementTable(
      {super.key,
      required this.headers,
      required this.rows,
      this.widths = const {},
      this.selected = const {}});
  final List<Widget> headers;
  final List<List<Widget>> rows;
  final Map<int, TableColumnWidth> widths;
  final Set<int> selected;
  @override
  Widget build(BuildContext context) => SurfaceCard(
      padding: EdgeInsets.zero,
      child: ClipRRect(
          borderRadius: BorderRadius.circular(9),
          child: Table(
              columnWidths: widths,
              defaultVerticalAlignment: TableCellVerticalAlignment.middle,
              children: [
                TableRow(
                    decoration: BoxDecoration(color: seclColor(0xFFEDF1F5)),
                    children: [
                      for (final cell in headers)
                        Padding(
                            padding: const EdgeInsets.symmetric(
                                horizontal: 12, vertical: 14),
                            child: DefaultTextStyle.merge(
                                style: TextStyle(
                                    fontSize: 12,
                                    fontWeight: FontWeight.w600,
                                    color: ink),
                                child: cell))
                    ]),
                for (var index = 0; index < rows.length; index++)
                  TableRow(
                      decoration: BoxDecoration(
                          color: selected.contains(index)
                              ? seclColor(0xFFEAF2F7)
                              : null,
                          border: Border(top: BorderSide(color: line))),
                      children: [
                        for (final cell in rows[index])
                          Padding(
                              padding: const EdgeInsets.symmetric(
                                  horizontal: 12, vertical: 14),
                              child: DefaultTextStyle.merge(
                                  style: TextStyle(fontSize: 13, color: ink),
                                  child: cell))
                      ])
              ])));
}

class AppCheck extends StatelessWidget {
  const AppCheck(
      {super.key,
      required this.value,
      required this.onChanged,
      this.tristate = false});
  final bool? value;
  final ValueChanged<bool?>? onChanged;
  final bool tristate;
  @override
  Widget build(BuildContext context) => SizedBox(
      width: 24,
      height: 28,
      child: Checkbox(
          value: value,
          tristate: tristate,
          onChanged: onChanged,
          materialTapTargetSize: MaterialTapTargetSize.shrinkWrap,
          visualDensity: VisualDensity.compact,
          shape:
              RoundedRectangleBorder(borderRadius: BorderRadius.circular(5))));
}

class StepNumberField extends StatefulWidget {
  const StepNumberField(
      {super.key,
      required this.value,
      required this.onChanged,
      this.minimum = 0,
      this.maximum = 65536,
      this.step = 256,
      this.unit = '',
      this.placeholder = ''});
  final int? value;
  final ValueChanged<int?> onChanged;
  final int minimum, maximum, step;
  final String unit, placeholder;
  @override
  State<StepNumberField> createState() => _StepNumberFieldState();
}

class _StepNumberFieldState extends State<StepNumberField> {
  late final TextEditingController controller;
  @override
  void initState() {
    super.initState();
    controller = TextEditingController(text: widget.value?.toString() ?? '');
  }

  @override
  void didUpdateWidget(StepNumberField old) {
    super.didUpdateWidget(old);
    if (old.value != widget.value &&
        controller.text != widget.value?.toString()) {
      controller.text = widget.value?.toString() ?? '';
    }
  }

  @override
  void dispose() {
    controller.dispose();
    super.dispose();
  }

  void step(int direction) =>
      widget.onChanged(((widget.value ?? 0) + widget.step * direction)
          .clamp(widget.minimum, widget.maximum));
  @override
  Widget build(BuildContext context) =>
      Row(mainAxisSize: MainAxisSize.min, children: [
        SizedBox(
            width: 46,
            height: 40,
            child: OutlinedButton(
                onPressed: () => step(-1),
                style: OutlinedButton.styleFrom(padding: EdgeInsets.zero),
                child: const Icon(Icons.remove_rounded, size: 19))),
        const SizedBox(width: 6),
        SizedBox(
            width: 144,
            height: 40,
            child: TextField(
                controller: controller,
                textAlign: TextAlign.center,
                keyboardType: TextInputType.number,
                style: const TextStyle(fontSize: 14),
                decoration: InputDecoration(
                    hintText: widget.placeholder,
                    hintStyle: TextStyle(fontSize: 13, color: muted),
                    contentPadding: const EdgeInsets.symmetric(
                        horizontal: 8, vertical: 10)),
                onChanged: (s) {
                  if (s.trim().isEmpty) {
                    widget.onChanged(null);
                    return;
                  }
                  final n = int.tryParse(s);
                  if (n != null && n >= widget.minimum && n <= widget.maximum) {
                    widget.onChanged(n);
                  }
                })),
        const SizedBox(width: 6),
        SizedBox(
            width: 46,
            height: 40,
            child: OutlinedButton(
                onPressed: () => step(1),
                style: OutlinedButton.styleFrom(padding: EdgeInsets.zero),
                child: const Icon(Icons.add_rounded, size: 19))),
        if (widget.unit.isNotEmpty) ...[
          const SizedBox(width: 10),
          Text(widget.unit, style: const TextStyle(fontSize: 14))
        ]
      ]);
}

Future<bool> confirmOperation(
        BuildContext context, String title, String message) async =>
    await showDialog<bool>(
        context: context,
        builder: (context) => AlertDialog(
                title: Text(title),
                content: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 440),
                    child: Text(message, style: const TextStyle(height: 1.6))),
                actions: [
                  TextButton(
                      onPressed: () => Navigator.pop(context, false),
                      child: Text(tr('取消'))),
                  FilledButton(
                      onPressed: () => Navigator.pop(context, true),
                      child: Text(tr('确认')))
                ])) ??
    false;
