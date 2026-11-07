import 'appearance.dart';
// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
import 'package:flutter/material.dart';
import 'app_widgets.dart';
import 'localization.dart';
import 'management_widgets.dart';
import 'skin_avatar.dart';

class AccountManager extends StatefulWidget {
  const AccountManager(
      {super.key,
      required this.snapshot,
      required this.action,
      this.pending = false,
      this.authenticating = false});
  final Data snapshot;
  final Future<void> Function(Data) action;
  final bool pending, authenticating;
  @override
  State<AccountManager> createState() => _AccountManagerState();
}

class _AccountManagerState extends State<AccountManager> {
  bool remember = false;
  Future<void> login([Data? account]) async {
    await widget.action({
      'action': 'login',
      'locale': AppLanguage.instance.code,
      'remember': remember,
      if (account != null) 'expected_account': account['id']
    });
  }

  Future<void> selected(String action, Data account) async {
    if (action == 'login') {
      await login(account);
      return;
    }
    if (action == 'remove' &&
        !await confirmOperation(
            context, tr('移除微软账号'), tr('将移除此设备保存的账号记录和系统凭据，不会删除微软账号。'))) return;
    await widget.action({
      'action': action == 'select'
          ? 'accounts.select'
          : action == 'remove'
              ? 'accounts.remove'
              : 'accounts.signout',
      'id': account['id']
    });
  }

  @override
  Widget build(BuildContext context) {
    final accounts = dataRows(widget.snapshot['accounts']);
    final current = widget.snapshot['account'] as Map?;
    return ManagementPage(
        title: tr('账号管理'),
        subtitle: tr('微软账号与游戏资格分别验证，凭据保存在系统凭据库。'),
        actions: [
          FilledButton.icon(
              onPressed: widget.pending ? null : login,
              icon: const Icon(Icons.add_rounded, size: 18),
              label: Text(tr('添加微软账号')))
        ],
        body: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
          CheckboxListTile(
              contentPadding: EdgeInsets.zero,
              controlAffinity: ListTileControlAffinity.leading,
              value: remember,
              onChanged: widget.pending
                  ? null
                  : (v) => setState(() => remember = v ?? false),
              title: Text(tr('记住本次登录（系统凭据库）')),
              subtitle: Text(tr('启用后请求离线访问权限，用于刷新微软登录；不提供离线账号。'),
                  style: const TextStyle(fontSize: 12, height: 1.5))),
          const SizedBox(height: 16),
          if (widget.authenticating)
            SurfaceCard(
                child: Row(children: [
              const SizedBox(
                  width: 22,
                  height: 22,
                  child: CircularProgressIndicator(strokeWidth: 2)),
              const SizedBox(width: 12),
              Expanded(child: Text(tr('正在验证微软账号，请在浏览器中完成授权。'))),
              TextButton(
                  onPressed: () => widget.action({'action': 'cancel'}),
                  child: Text(tr('取消')))
            ])),
          if (widget.pending && !widget.authenticating) Text(tr('等待当前任务完成')),
          if (widget.snapshot['error'] is Map)
            Expanded(
                child: ProblemPanel(
                    Map<String, dynamic>.from(widget.snapshot['error'] as Map),
                    retry: () => login())),
          if (widget.snapshot['account_warning'] is Map)
            Padding(
                padding: const EdgeInsets.symmetric(vertical: 12),
                child: SurfaceCard(
                    color: seclColor(0xFFFFF6EA),
                    child: Text(
                        errorMessage(Map<String, dynamic>.from(
                            widget.snapshot['account_warning'] as Map)),
                        style: const TextStyle(fontSize: 12, height: 1.6)))),
          if (widget.snapshot['error'] is! Map)
            Expanded(
                child: accounts.isEmpty
                    ? Center(child: Text(tr('尚未添加微软账号。')))
                    : SingleChildScrollView(
                        child: ManagementTable(headers: [
                        Text(tr('角色')),
                        Text(tr('账号类型')),
                        Text(tr('登录状态')),
                        Text(tr('操作'))
                      ], widths: const {
                        0: FlexColumnWidth(3),
                        1: FlexColumnWidth(1.3),
                        2: FlexColumnWidth(2),
                        3: FixedColumnWidth(72)
                      }, rows: [
                        for (final account in accounts)
                          [
                            Row(children: [
                              SkinAvatar(
                                  skinUrl: account['skin_url'] as String?,
                                  size: 42),
                              const SizedBox(width: 14),
                              Expanded(
                                  child: Tooltip(
                                      message: account['uuid'] as String,
                                      child: Column(
                                          crossAxisAlignment:
                                              CrossAxisAlignment.start,
                                          children: [
                                            Text(account['name'] as String,
                                                style: const TextStyle(
                                                    fontSize: 14,
                                                    fontWeight:
                                                        FontWeight.w600)),
                                            const SizedBox(height: 7),
                                            Text(
                                                current?['uuid'] ==
                                                        account['uuid']
                                                    ? tr('当前角色')
                                                    : account['remembered'] ==
                                                            true
                                                        ? tr('已记住登录')
                                                        : tr('仅当前会话'),
                                                style: TextStyle(
                                                    color: muted, fontSize: 12))
                                          ])))
                            ]),
                            const Text('Microsoft'),
                            Text(
                                current?['uuid'] == account['uuid'] &&
                                        current?['owned'] == true
                                    ? tr('游戏资格已验证')
                                    : account['needs_login'] == false
                                        ? tr('已登录')
                                        : account['remembered'] == true
                                            ? tr('可刷新登录')
                                            : tr('需要重新登录'),
                                style: const TextStyle(fontSize: 12)),
                            PopupMenuButton<String>(
                                enabled: !widget.pending,
                                tooltip: tr('更多操作'),
                                icon: const Icon(Icons.more_horiz_rounded),
                                onSelected: (action) =>
                                    selected(action, account),
                                itemBuilder: (_) => [
                                      PopupMenuItem(
                                          value: 'select',
                                          child: Text(tr('使用此账号'))),
                                      PopupMenuItem(
                                          value: 'login',
                                          child: Text(tr('重新登录'))),
                                      PopupMenuItem(
                                          value: 'signout',
                                          child: Text(tr('退出此账号'))),
                                      PopupMenuItem(
                                          value: 'remove',
                                          child: Text(tr('移除'),
                                              style: const TextStyle(
                                                  color: Colors.red)))
                                    ])
                          ]
                      ])))
        ]));
  }
}
