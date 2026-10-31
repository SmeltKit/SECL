// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
namespace solar::engine {
using namespace detail;
static std::map<std::string, std::string> constants(const std::string &data) {
    size_t pos = 0;
    auto number = [&](size_t size) {
        require(size <= data.size() - std::min(pos, data.size()), "class_truncated",
                "OptiFine 类文件截断。");
        std::uint32_t v = 0;
        while (size--)
            v = (v << 8) | static_cast<unsigned char>(data[pos++]);
        return v;
    };
    require(number(4) == 0xcafebabe, "invalid_class", "无效的 Java 类文件。");
    number(2);
    number(2);
    auto count = number(2);
    std::map<unsigned, std::string> utf;
    std::map<unsigned, unsigned> strings;
    for (unsigned i = 1; i < count; ++i) {
        switch (number(1)) {
        case 1: {
            auto n = number(2);
            require(n <= data.size() - pos, "class_truncated", "常量字符串截断。");
            utf[i] = data.substr(pos, n);
            pos += n;
            break;
        }
        case 8:
            strings[i] = number(2);
            break;
        case 3:
        case 4:
        case 9:
        case 10:
        case 11:
        case 12:
        case 17:
        case 18:
            number(4);
            break;
        case 5:
        case 6:
            number(4);
            number(4);
            ++i;
            break;
        case 7:
        case 16:
        case 19:
        case 20:
            number(2);
            break;
        case 15:
            number(1);
            number(2);
            break;
        default:
            throw Error("optifine", "invalid_constant_pool", "无效的 Java 常量池。");
        }
    }
    number(2);
    number(2);
    number(2);
    auto interfaces = number(2);
    for (unsigned i = 0; i < interfaces; ++i)
        number(2);
    auto fields = number(2);
    std::map<std::string, std::string> out;
    for (unsigned i = 0; i < fields; ++i) {
        number(2);
        auto name = utf[number(2)];
        number(2);
        auto attributes = number(2);
        for (unsigned k = 0; k < attributes; ++k) {
            auto attr = utf[number(2)];
            auto len = number(4);
            require(len <= data.size() - pos, "class_truncated", "字段属性截断。");
            if (attr == "ConstantValue" && len == 2)
                out[name] = utf[strings[number(2)]];
            else
                pos += len;
        }
    }
    return out;
}
Json inspect_optifine(const fs::path &jar) {
    auto patcher = jar_entry(jar, "optifine/Patcher.class");
    require(patcher.size() >= 4 && patcher.substr(0, 4) == std::string("\xca\xfe\xba\xbe", 4),
            "missing_patcher", "安装包没有受支持的 OptiFine 补丁器。");
    for (auto entry :
         {"net/optifine/Config.class", "notch/net/optifine/Config.class", "Config.class"}) {
        auto data = jar_entry(jar, entry);
        if (data.empty())
            continue;
        auto fields = constants(data);
        auto game = fields["MC_VERSION"],
             version = fields["OF_EDITION"] + "_" + fields["OF_RELEASE"];
        if (id(game) && id(version) && !fields["OF_RELEASE"].empty())
            return {{"game", game}, {"version", version}};
    }
    throw Error("optifine", "unknown_build", "无法从安装包内部读取 OptiFine 构建。");
}
std::string optifine_compatibility(const Json &b, const Json &i) {
    if (b.value("game", "") != i.value("version", ""))
        return "OptiFine 与 Minecraft 版本不一致。";
    auto loader = i.value("loader", "vanilla");
    if (loader == "vanilla")
        return {};
    if (loader != "forge")
        return "仅支持原版 / Forge 的 OptiFine 安装；不能与 "
               "Fabric、Quilt、NeoForge 或 Iris 混装。";
    auto forge = b.value("forge", "");
    if (!std::regex_match(forge, std::regex("[0-9]+\\.[0-9]+\\.[0-9]+(?:\\.[0-9]+)?")))
        return "发布方未明确标注匹配的 Forge 构建。";
    if (forge != i.value("loader_version", ""))
        return "OptiFine 标注 Forge " + forge + "，与当前构建不同；不会自动更换主加载器。";
    return {};
}
Json optifine_builds(const std::string &game, const Task &t) {
    require(id(game), "invalid_game", "游戏版本无效。");
    auto providers =
        download_candidates("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", t);
    std::vector<std::string> order;
    for (auto &u : providers)
        order.push_back(u.find("bangbang93.com") != u.npos ? "bmclapi" : "official");
    Json errors = Json::array();
    for (auto &source : order)
        try {
            auto one = t;
            one.source = "official";
            auto bytes =
                fetch_data(source == "official"
                               ? "https://optifine.net/downloads"
                               : "https://bmclapi2.bangbang93.com/optifine/" + url_encode(game),
                           one);
            Json out = Json::array();
            std::set<std::string> seen;
            if (source == "official") {
                require(bytes.find("adloadx?f=") != bytes.npos, "invalid_optifine_list",
                        "OptiFine 官网列表格式无效。");
                std::regex row("<tr\\b[^>]*>([\\s\\S]*?)</tr>", std::regex::icase);
                for (auto it = std::sregex_iterator(bytes.begin(), bytes.end(), row);
                     it != std::sregex_iterator(); ++it) {
                    auto text = (*it)[1].str();
                    std::smatch file, version, forge;
                    if (!std::regex_search(text, file,
                                           std::regex("adloadx\\?f=((?:preview_)?"
                                                      "OptiFine_[A-Za-z0-9._]+\\.jar)")))
                        continue;
                    auto name = file[1].str();
                    if (!std::regex_match(name, version,
                                          std::regex("(preview_)?OptiFine_(.+?)_(HD_[A-Z]"
                                                     "_[A-Za-z0-9_]+)\\.jar")) ||
                        version[2] != game || !seen.insert(name).second)
                        continue;
                    std::regex_search(
                        text, forge,
                        std::regex("class=['\"]colForge['\"][^>]*>\\s*Forge\\s+([^<]+)"));
                    out.push_back({{"file", name},
                                   {"game", game},
                                   {"version", version[3].str()},
                                   {"preview", version[1].matched},
                                   {"forge", forge.empty() ? "" : trim(forge[1])},
                                   {"source", source},
                                   {"page", "https://optifine.net/adloadx?f=" + name}});
                }
            } else {
                auto list = Json::parse(bytes);
                require(list.is_array(), "invalid_optifine_list", "镜像列表格式无效。");
                for (auto &b : list) {
                    if (b.value("mcversion", "") != game)
                        continue;
                    auto version = b.value("type", "") + "_" + b.value("patch", "");
                    if (!std::regex_match(version, std::regex("HD_[A-Z]_[A-Za-z0-9_]+")) ||
                        !seen.insert(version).second)
                        continue;
                    bool preview = version.find("_pre") != version.npos;
                    auto name = std::string(preview ? "preview_OptiFine_" : "OptiFine_") + game +
                                "_" + version + ".jar";
                    if (b.value("filename", "") != name)
                        continue;
                    auto forge = b.value("forge", "");
                    if (forge.rfind("Forge ", 0) == 0)
                        forge.erase(0, 6);
                    out.push_back({{"file", name},
                                   {"game", game},
                                   {"version", version},
                                   {"preview", preview},
                                   {"forge", trim(forge)},
                                   {"source", source},
                                   {"page", "https://optifine.net/adloadx?f=" + name}});
                }
            }
            return {{"builds", out}, {"source", source}, {"warnings", errors}};
        } catch (const Error &e) {
            t.check();
            errors.push_back(e.json());
        }
    throw Error("optifine", "list_failed", errors.dump());
}
Json install_optifine(const Json &r, const Task &t) {
    auto root = fs::u8path(r.at("root").get<std::string>());
    auto instance_id = r.at("id").get<std::string>();
    require(id(instance_id), "invalid_id", "实例 ID 无效。");
    auto document = checked_path(root, "versions/" + instance_id + "/" + instance_id + ".json");
    auto original = read_file(document);
    auto metadata = load_version(root, instance_id);
    auto game = metadata.value("_baseId", metadata.value("id", ""));
    auto directory =
        fs::u8path(r.value("directory", metadata.value("_solarGameDirectory", root.u8string())));
    Lock lock(directory / "mods");
    Workspace work(root);
    auto old = metadata.value("_solarOptifine", metadata.value("_m2clOptifine", Json::object()));
    auto base = old.empty() ? metadata : old.value("base", Json::object());
    require(!base.empty(), "missing_optifine_base", "不能自动还原缺少原版记录的 OptiFine 配置。");
    fs::path previous;
    if (!old.value("modFile", "").empty()) {
        previous = checked_path(directory, "mods/" + old.at("modFile").get<std::string>());
        require(fs::is_regular_file(previous) && hash_file(previous, "sha256") == old.at("sha256"),
                "optifine_changed", "原 OptiFine 文件已被外部修改。");
    }
    auto action = r.value("operation", "install");
    if (action == "remove")
        require(!old.empty(), "optifine_not_installed", "此实例没有受管理的 OptiFine。");
    Json next = base;
    fs::path target, staged;
    if (action != "remove") {
        require(action == "install", "invalid_action", "无效的 OptiFine 操作。");
        auto build = r.at("build");
        auto loaders = loader_components(base);
        require(loaders.size() <= 1, "multiple_loaders", "实例含多个主加载器。");
        auto loader = loaders.empty() ? "vanilla" : loaders.begin().key();
        Json instance{
            {"version", game},
            {"loader", loader},
            {"loader_version", loaders.empty() ? "" : loaders.begin().value().get<std::string>()}};
        auto problem = optifine_compatibility(build, instance);
        require(problem.empty(), "incompatible_optifine", problem);
        for (auto &m : local_mods(directory, loader))
            if (m.value("enabled", true)) {
                for (auto &id : m.value("ids", Json::array()))
                    if (id == "iris" || id == "sodium" || id == "oculus" || id == "optifabric")
                        throw Error("optifine", "conflicting_mod",
                                    "请先停用冲突组件：" + id.get<std::string>());
                if (std::find(m["ids"].begin(), m["ids"].end(), "optifine") != m["ids"].end() &&
                    fs::u8path(m.value("filename", "")) != previous.filename())
                    throw Error("optifine", "existing_copy",
                                "检测到手动安装的 OptiFine，请先处理旧副本。");
            }
        auto installer = work.path / "installer.jar";
        if (r.contains("local_file"))
            atomic_write(installer, read_file(fs::u8path(r.at("local_file").get<std::string>()),
                                              64 * 1024 * 1024));
        else {
            Json errors = Json::array();
            bool complete = false;
            auto providers = download_candidates(
                "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json", t);
            for (auto &candidate : providers)
                try {
                    auto one = t;
                    one.source = "official";
                    std::string bytes;
                    if (candidate.find("bangbang93.com") != candidate.npos) {
                        auto version = build.at("version").get<std::string>();
                        std::smatch match;
                        require(std::regex_match(
                                    version, match,
                                    std::regex("(HD_[A-Z])_([A-Za-z0-9]+)(?:_(pre[0-9]+))?")),
                                "invalid_optifine_build", "OptiFine 构建名无效。");
                        auto type = match[1].str() + (match[3].matched ? "_" + match[2].str() : "");
                        auto patch = match[3].matched ? match[3].str() : match[2].str();
                        bytes = fetch_data("https://bmclapi2.bangbang93.com/optifine/" +
                                               url_encode(game) + "/" + url_encode(type) + "/" +
                                               url_encode(patch),
                                           one, 64 * 1024 * 1024);
                    } else {
                        auto page = fetch_data("https://optifine.net/adloadx?f=" +
                                                   url_encode(build.at("file")),
                                               one, 4 * 1024 * 1024);
                        std::smatch link;
                        require(std::regex_search(
                                    page, link, std::regex("href=['\"](downloadx\\?[^'\"]+)['\"]")),
                                "missing_download_link",
                                "OptiFine 官方页面未提供下载链接，请选择本地安装包。");
                        auto relative = std::regex_replace(link[1].str(), std::regex("&amp;"), "&");
                        require(relative.find("f=" + build.at("file").get<std::string>()) !=
                                    relative.npos,
                                "download_identity", "OptiFine 下载链接不匹配。");
                        bytes =
                            fetch_data("https://optifine.net/" + relative, one, 64 * 1024 * 1024);
                    }
                    atomic_write(installer, bytes);
                    auto actual = inspect_optifine(installer);
                    require(actual.at("game") == game &&
                                actual.at("version") == build.at("version"),
                            "optifine_identity", "安装包内部版本不匹配。");
                    complete = true;
                    break;
                } catch (const Error &e) {
                    t.check();
                    errors.push_back(e.json());
                }
            require(complete, "optifine_download_failed", errors.dump());
        }
        auto found = inspect_optifine(installer);
        require(found.at("game") == game && found.at("version") == build.at("version"),
                "optifine_identity", "安装包内部版本与选择的构建不匹配。");
        auto component = build;
        component["base"] = base;
        component["sha256"] = hash_file(installer, "sha256");
        component["sha512"] = hash_file(installer);
        component["mode"] = loader;
        if (loader == "forge") {
            auto name = "OptiFine_" + game + "_" + found.at("version").get<std::string>() + ".jar";
            target = checked_path(directory, "mods/" + name);
            require(!fs::exists(target) || target == previous, "optifine_exists",
                    "目标 OptiFine 文件已存在。");
            staged = installer;
            component["modFile"] = name;
        } else {
            auto java = fs::u8path(r.at("java").get<std::string>());
            require(!probe_java(java).is_null(), "invalid_java", "所选 Java 无法运行。");
            auto patched = work.path / "patched.jar";
            process({java,
                     work.path,
                     {"-Djava.awt.headless=true", "-cp", installer.u8string(), "optifine.Patcher",
                      checked_path(root, "versions/" + instance_id + "/" + instance_id + ".jar")
                          .u8string(),
                      installer.u8string(), patched.u8string()}},
                    t, 120);
            bool valid = false;
            for (auto e :
                 {"net/optifine/Config.class", "notch/net/optifine/Config.class", "Config.class"})
                if (!jar_entry(patched, e).empty())
                    valid = true;
            require(valid, "invalid_patch", "OptiFine 补丁输出无效。");
            auto wrapper = trim(jar_entry(installer, "launchwrapper-of.txt"));
            require(std::regex_match(wrapper, std::regex("[0-9]+\\.[0-9]+")),
                    "unsupported_old_wrapper", "此旧版 OptiFine 启动包装器尚未接入。");
            auto wrapper_bytes = jar_entry(installer, "launchwrapper-of-" + wrapper + ".jar");
            require(!wrapper_bytes.empty(), "missing_wrapper", "OptiFine 启动包装器缺失。");
            auto key = component.at("sha256").get<std::string>();
            auto libs = base.value("libraries", Json::array());
            Json record = Json::array();
            auto add = [&](const std::string &name, const std::string &filename,
                           const std::string &bytes) {
                auto relative = "solar-optifine/" + key + "/" + filename;
                auto path = checked_path(root, "libraries/" + relative);
                atomic_write(path, bytes);
                record.push_back(
                    {{"path", "libraries/" + relative}, {"sha256", hash_bytes(bytes, "sha256")}});
                libs.insert(libs.begin(), Json{{"name", name},
                                               {"downloads",
                                                {{"artifact",
                                                  {{"path", relative},
                                                   {"url", "https://optifine.net/downloads"},
                                                   {"sha1", hash_bytes(bytes, "sha1")},
                                                   {"size", bytes.size()}}}}}});
            };
            add("optifine:launchwrapper-of:" + wrapper, "launchwrapper.jar", wrapper_bytes);
            add("optifine:OptiFine:" + game + "_" + found.at("version").get<std::string>(),
                "OptiFine.jar", read_file(patched, 128 * 1024 * 1024));
            next["libraries"] = libs;
            next["mainClass"] = "net.minecraft.launchwrapper.Launch";
            if (next.contains("minecraftArguments"))
                next["minecraftArguments"] = next.at("minecraftArguments").get<std::string>() +
                                             " --tweakClass optifine.OptiFineTweaker";
            else {
                next["arguments"]["game"].push_back("--tweakClass");
                next["arguments"]["game"].push_back("optifine.OptiFineTweaker");
            }
            component["files"] = record;
        }
        next["_solarOptifine"] = component;
    }
    t.check();
    require(read_file(document) == original, "version_changed", "确认后版本配置发生变化。");
    auto backup = work.path / "old.jar";
    bool moved = false, added = false;
    try {
        if (!previous.empty()) {
            fs::rename(previous, backup);
            moved = true;
        }
        if (!staged.empty()) {
            fs::copy_file(staged, target);
            added = true;
        }
        atomic_write(document, next.dump(2));
    } catch (...) {
        std::error_code ec;
        if (added)
            fs::remove(target, ec);
        if (moved)
            fs::rename(backup, previous, ec);
        throw;
    }
    return {{"metadata", next}, {"operation", action}};
}
} // namespace solar::engine
