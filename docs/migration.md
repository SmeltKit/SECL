# M²CL → SECL 的核心迁移

2026-10-31。迁移的是业务能力，不复制旧 Qt 窗口、控件、QObject 信号槽或 Qt
翻译资源。新项目的全部原生目标只使用 C++17 与明确列出的第三方依赖。
旧 M²CL 工作目录未修改。

| 原模块 | SECL 实现 | 界面状态 / 验证边界 |
| --- | --- | --- |
| microsoftauth / networkerror | `auth.cpp`、`api.cpp` | PKCE、回调、Xbox、XSTS、资格、角色；合约测试通过，真实账号待新应用授权 |
| accounts / accountmanager / credentialvault | `accounts.cpp`、`credentialvault.cpp`、原生会话管理 | Microsoft 元数据、严格验证、系统凭据库适配；当前 UI 仅单会话登录，不开放记住账号 |
| catalog / settings | `runtime.cpp`、`game.cpp` | 本地版本识别、继承、隔离目录、实例 Java 与参数；读取旧实例设置 |
| java / javaarchive / javaprovider | `runtime.cpp`、`javaprovider.cpp`、`archive.cpp` | 扫描、实际版本探测、发行版/构建查询、校验解压安装、许可路径；下载界面未开放 |
| memory / launchoptions / gameplan | `runtime.cpp`、`game.cpp`、`gameinstall.cpp` | 可用内存建议、手动内存、分辨率和参数检查；实例参数参与实际命令生成 |
| downloadsource / gameinstaller | `downloads.cpp`、`gameinstall.cpp` | 官方/镜像、时区优先级、有限回退、哈希、资源索引、官方运行时；完整联网安装待集成实测 |
| forgeinstaller | `loaderinstall.cpp` | Fabric / Quilt 配置、Forge / NeoForge 官方处理器、暂存、输出校验；保留原支持格式边界 |
| optifine | `optifine.cpp` | 内部类常量识别、原版/Forge 精确组合、补丁、移除及还原；完整安装待集成实测 |
| resources / resourceclient | `resources.cpp`、Flutter 资源市场 | 三类独立入口、中文分类、项目图标、版本/加载器筛选、正式/测试/预览文件分类、底部分页；Modrinth 已实测，未接入 CurseForge |
| mods / modinstaller | `mods.cpp`、`modinstall.cpp`、共用安装确认窗口 | 递归依赖、已有组件复用、显式重下、依赖勾选、冲突确认、暂存与回滚；Comforts 与必需前置真实下载通过 |
| packs / compatibility | `packs.cpp`、共用安装确认窗口 | 光影/材质包下载、哈希/归档验证、重复复用、光影支持提示；MakeUp 与 Default Dark Mode 真实下载通过；保留 Quilt 规则缓存与退避 |
| gamelog / SecretRedactor | `accounts.cpp`、`runtime.cpp` | 跨分块令牌脱敏、XML 日志解码和通用脱敏；当前精简 UI 未提供日志阅读器 |

**明确排除**：离线账号创建、离线 UUID、离线皮肤、authlib-injector 皮肤注入。
原 `skins.cpp` 的业务都是离线皮肤，因此未搬入；没有自带 Steve/Alex 素材。
Qt Widgets UI、Qt 国际化与富文本渲染属于外壳，未作为核心依赖搬入。

原 Qt 账号管理器拆为无 Qt 的存储、认证、系统凭据和任务状态；Flutter 不接收
原始令牌。刷新函数仍须完整重新检查游戏资格；当前精简 UI 每次启动重新登录，
不会自动从旧 M²CL 读取凭据，也不会在磁盘保存访问令牌。

## 数据边界

- 实例读取 `.solar-instance.json`，缺失时可读取 `.m2cl-instance.json`，映射旧参数名。
- 旧 `mods/.m2cl-mods.json` 的 `files` 数组会转成内部索引；只有哈希一致才复用身份。
- 新索引写入 `mods/.solar-mods.json`；旧项目数据不会被自动搬家。
- 版本、文件路径、档案解压、来源哈希与安装计划在写入前验证。
- 加载器是版本组件；模组不能冒充 Java、游戏或主加载器。
- Quilt 对 Fabric 的支持仍结合文件元数据和官方提醒，不声称维护了官方未提供的精确哈希黑名单。
- 对不支持的老 Forge 安装器格式仍明确报错，不伪装成功。

构建通过不等于每个发行版、加载器和历史版本组合都已经联网安装成功。
请按 `testing.md` 中的后续矩阵进行真实集成验证后再开放扩展 UI。
