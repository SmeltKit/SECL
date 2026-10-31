// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/core.hpp"
#include "solar/engine.hpp"
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <zlib.h>
namespace solar {
static bool safe_id(const std::string &s) {
    return !s.empty() && s.size() < 200 && s != "." && s != ".." &&
           s.find_first_of("/\\:\0", 0, 4) == std::string::npos;
}
static fs::path inside(const fs::path &root, const std::string &relative) {
    fs::path p = fs::u8path(relative);
    if (p.empty() || p.is_absolute() || relative.find('\\') != std::string::npos ||
        relative.find(':') != std::string::npos || relative.find('\0') != std::string::npos)
        throw Error("launch", "unsafe_path", "版本配置包含不安全的相对路径。");
    for (const auto &v : p)
        if (v == ".." || v == ".")
            throw Error("launch", "unsafe_path", "版本配置包含不安全的相对路径。");
    auto result = root / p;
    std::error_code ec;
    auto base = fs::weakly_canonical(root, ec);
    if (ec)
        throw Error("launch", "invalid_root", "游戏目录不可访问。");
    auto resolved = fs::weakly_canonical(result, ec);
    if (ec)
        throw Error("launch", "invalid_path", "文件路径不可访问。");
    auto mismatch = std::mismatch(base.begin(), base.end(), resolved.begin(), resolved.end());
    if (mismatch.first != base.end())
        throw Error("launch", "path_escape", "版本配置路径指向游戏目录之外。");
    return result;
}
bool rules_allow(const Json &rules, const Platform &p) {
    if (!rules.is_array())
        throw Error("launch", "invalid_rules", "版本规则格式无效。");
    bool allowed = rules.empty();
    for (auto &r : rules) {
        bool match = true;
        auto os = r.value("os", Json::object());
        if (os.contains("name"))
            match &= os.value("name", "") == p.os;
        if (os.contains("arch")) {
            auto arch = os.value("arch", "");
            if (arch == "amd64" || arch == "x64")
                arch = "x86_64";
            if (arch == "arm64")
                arch = "aarch64";
            match &= arch == p.arch;
        }
        if (os.contains("version")) {
            try {
                match &= std::regex_search(p.version, std::regex(os.value("version", "")));
            } catch (...) {
                throw Error("launch", "invalid_rule_regex", "系统版本规则无效。");
            }
        }
        auto features = r.value("features", Json::object());
        for (auto &f : features.items())
            match &= !f.value().get<bool>();
        if (match)
            allowed = r.value("action", "") == "allow";
    }
    return allowed;
}
Json load_version(const fs::path &root, const std::string &id, int depth) {
    if (!safe_id(id) || depth > 8)
        throw Error("versions", "invalid_inheritance", "版本名称无效或继承层级过深。");
    auto file = inside(root, "versions/" + id + "/" + id + ".json");
    auto m = Json::parse(read_file(file), nullptr, false);
    if (!m.is_object())
        throw Error("versions", "invalid_json", "无法解析版本配置：" + id);
    if (m.contains("inheritsFrom")) {
        auto parent_id = m.value("inheritsFrom", "");
        auto parent = load_version(root, parent_id, depth + 1);
        parent["_baseId"] = parent.value("_baseId", parent.value("id", parent_id));
        for (auto &v : m.items()) {
            if (v.key() == "arguments") {
                auto arguments = parent.value("arguments", Json::object());
                for (auto &kind : v.value().items()) {
                    auto a = arguments.value(kind.key(), Json::array());
                    for (auto &arg : kind.value())
                        a.push_back(arg);
                    arguments[kind.key()] = a;
                }
                parent["arguments"] = arguments;
            } else if (v.key() == "libraries") {
                auto libraries = v.value();
                std::set<std::string> replace;
                auto key = [](std::string n) {
                    auto p = n.find(':');
                    return p == n.npos ? n : n.substr(0, n.find(':', p + 1));
                };
                for (auto &lib : libraries)
                    replace.insert(key(lib.value("name", "")));
                for (auto &lib : parent.value("libraries", Json::array()))
                    if (!replace.count(key(lib.value("name", ""))))
                        libraries.push_back(lib);
                parent["libraries"] = libraries;
            } else if (v.key() != "inheritsFrom")
                parent[v.key()] = v.value();
        }
        if (!m.contains("jar"))
            parent["jar"] = parent_id;
        m = parent;
    }
    m.erase("inheritsFrom");
    return m;
}
Json scan_versions(const fs::path &input) {
    std::error_code ec;
    auto root = fs::canonical(input, ec);
    if (ec || !fs::is_directory(root))
        throw Error("versions", "directory_missing",
                    "游戏目录不存在，请选择已安装游戏的 .minecraft 目录。");
    Json result = Json::array();
    auto dir = root / "versions";
    if (!fs::is_directory(dir))
        throw Error("versions", "versions_missing",
                    "该目录没有 versions 文件夹，不会自动下载游戏。");
    for (auto &entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_directory(ec))
            continue;
        auto id = entry.path().filename().u8string();
        if (!safe_id(id))
            continue;
        try {
            auto m = load_version(root, id);
            result.push_back(
                {{"id", id},
                 {"type", m.value("type", "unknown")},
                 {"required_java", m.value("javaVersion", Json::object()).value("majorVersion", 0)},
                 {"available", true}});
        } catch (const Error &e) {
            result.push_back(
                {{"id", id}, {"type", "invalid"}, {"available", false}, {"reason", e.what()}});
        }
    }
    std::sort(result.begin(), result.end(), [](const Json &a, const Json &b) {
        return a.at("id").get<std::string>() > b.at("id").get<std::string>();
    });
    return result;
}
std::vector<std::string> split_arguments(const std::string &s) {
    std::vector<std::string> r;
    std::string token;
    char quote = 0;
    bool active = false;
    for (char c : s) {
        if (quote) {
            if (c == quote)
                quote = 0;
            else
                token += c;
            active = true;
        } else if (c == '\'' || c == '"') {
            quote = c;
            active = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (active) {
                r.push_back(token);
                token.clear();
                active = false;
            }
        } else {
            token += c;
            active = true;
        }
    }
    if (quote)
        throw Error("launch", "unclosed_quote", "旧版启动参数的引号未闭合。");
    if (active)
        r.push_back(token);
    return r;
}
static std::string maven_path(const std::string &name) {
    auto parts = split_arguments(std::regex_replace(name, std::regex(":"), " "));
    if (parts.size() < 3 || parts.size() > 4)
        throw Error("launch", "invalid_library", "本地库标识无效。");
    auto group = parts[0];
    std::replace(group.begin(), group.end(), '.', '/');
    return group + "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2] +
           (parts.size() == 4 ? "-" + parts[3] : "") + ".jar";
}
Command launch_command(const fs::path &root, const std::string &id, const fs::path &java_override,
                       const Identity &identity, const Platform &host) {
    if (!identity.owned || identity.expires <= now() + 60 || identity.token.empty() ||
        !std::regex_match(identity.uuid, std::regex("[0-9a-fA-F]{32}")) ||
        !std::regex_match(identity.name, std::regex("[A-Za-z0-9_]{1,16}")))
        throw Error("launch", "login_required", "请先完成微软登录、游戏所有权与角色验证。");
    auto m = load_version(root, id);
    auto instance_path = inside(root, "versions/" + id);
    auto config = engine::load_instance_settings(instance_path);
    auto choice = config.value("java", Json::object());
    auto java = choice.value("mode", "auto") == "fixed" ? fs::u8path(choice.value("path", ""))
                                                        : java_override;
    auto selected = engine::probe_java(java);
    if (selected.is_null())
        throw Error("launch", "java_unavailable", "所选 Java 无法运行。");
    auto required = m.value("javaVersion", Json::object()).value("majorVersion", 8);
    if (selected.value("major", 0) < required &&
        !choice.value("allow_below_recommended", choice.value("allowBelowRecommended", false)))
        throw Error("launch", "java_too_old", "所选 Java 低于默认版本，请主动确认强制使用后重试。");
    for (auto pair :
         std::vector<std::pair<std::string, std::string>>{{"runtimeVersion", "runtime_version"},
                                                          {"vendor", "vendor"},
                                                          {"vendorVersion", "vendor_version"},
                                                          {"arch", "arch"}}) {
        auto expected = choice.value(pair.second, choice.value(pair.first, ""));
        if (!expected.empty() && expected != selected.value(pair.second, ""))
            throw Error("launch", "java_pin_changed", "固定 Java 的构建、发行版或架构已变化。");
    }
    auto loaders = engine::loader_components(m);
    if (loaders.size() > 1)
        throw Error("launch", "multiple_loaders", "实例含多个主加载器。");
    auto directory =
        fs::u8path(m.value("_solarGameDirectory", m.value("_m2clGameDirectory", root.u8string())));
    if (!m.contains("_solarGameDirectory") && !m.contains("_m2clGameDirectory") &&
        (fs::exists(instance_path / "mods") || fs::exists(instance_path / "saves") ||
         fs::exists(instance_path / "options.txt")))
        directory = instance_path;
    if (!fs::is_directory(directory))
        throw Error("launch", "game_directory_missing", "实例游戏目录不存在。");
    auto mods = engine::local_mods(directory, loaders.empty() ? "vanilla" : loaders.begin().key());
    auto component = m.value("_solarOptifine", m.value("_m2clOptifine", Json::object()));
    Json instance{
        {"version", m.value("_baseId", m.value("id", id))},
        {"game_directory", directory.u8string()},
        {"loader", loaders.empty() ? "vanilla" : loaders.begin().key()},
        {"loader_version", loaders.empty() ? "" : loaders.begin().value().get<std::string>()},
        {"java_major", selected.value("major", 0)},
        {"optifine", component}};
    auto check = engine::check_mods(instance, mods, true, false);
    if (!check["errors"].empty())
        throw Error("launch", "mod_conflict", check["errors"].dump());
    if (m.contains("_solarLoaderFiles") || m.contains("_m2clLoaderFiles"))
        engine::verify_loader_outputs(m, root);
    if (!component.empty()) {
        auto problem = engine::optifine_compatibility(component, instance);
        if (!problem.empty())
            throw Error("launch", "optifine_conflict", problem);
        auto file = component.value("modFile", "");
        if (!file.empty() && engine::hash_file(engine::checked_path(directory, "mods/" + file),
                                               "sha256") != component.value("sha256", ""))
            throw Error("launch", "optifine_file_changed", "OptiFine 文件已变化。");
        for (auto &f : component.value("files", Json::array()))
            if (engine::hash_file(engine::checked_path(root, f.at("path")), "sha256") !=
                f.at("sha256"))
                throw Error("launch", "optifine_library_changed", "OptiFine 运行库已变化。");
    }
    auto options = config.value("launch", Json::object());
    engine::validate_launch_options(options);
    int memory = options.value("max_memory_mib", 2048);
    if (options.value("auto_memory", true)) {
        int enabled = 0;
        for (auto &mod : mods)
            if (mod.value("enabled", true))
                ++enabled;
        auto context = Json{{"game_version", instance["version"]},
                            {"enabled_mods", enabled},
                            {"java_arch", selected.value("arch", "")}};
        memory = engine::recommend_memory(engine::system_memory(), context).at("maximum_mib");
    }
    if (options.value("min_memory_mib", 0) > memory)
        throw Error("launch", "initial_memory_too_large", "初始内存高于本次最大内存。");
    auto p = host;
    p.arch = selected.value("arch", host.arch);
    if (p.arch == "amd64" || p.arch == "x64")
        p.arch = "x86_64";
    if (p.arch == "arm64")
        p.arch = "aarch64";
    std::vector<std::string> cp, natives;
    auto need = [&](const std::string &relative) {
        auto path = inside(root, relative);
        if (!fs::is_regular_file(path))
            throw Error("launch", "missing_file",
                        "缺少本地文件：" + relative + "。请先使用其他启动器完成安装。");
        return path;
    };
    for (auto &lib : m.value("libraries", Json::array())) {
        if (!rules_allow(lib.value("rules", Json::array()), p))
            continue;
        auto name = lib.value("name", "");
        auto marker = name.find(":natives-");
        if (marker != name.npos) {
            auto classifier = name.substr(marker + 1);
            std::string arch = "x86_64";
            if (classifier.find("arm64") != classifier.npos ||
                classifier.find("aarch64") != classifier.npos)
                arch = "aarch64";
            else if (classifier.size() > 4 && classifier.substr(classifier.size() - 4) == "-x86")
                arch = "x86";
            if (arch != p.arch)
                continue;
        }
        auto downloads = lib.value("downloads", Json::object());
        auto artifact = downloads.value("artifact", Json::object());
        auto rel = artifact.value("path", maven_path(name));
        cp.push_back(need("libraries/" + rel).u8string());
        auto map = lib.value("natives", Json::object());
        if (map.contains(p.os)) {
            if (p.arch == "aarch64")
                throw Error("launch", "native_arch_unsupported",
                            "此旧版本没有 ARM64 原生库，请选择兼容的 x64 Java。");
            auto classifier = map.at(p.os).get<std::string>();
            auto pos = classifier.find("${arch}");
            if (pos != classifier.npos)
                classifier.replace(pos, 7, p.arch == "x86" ? "32" : "64");
            auto d = downloads.value("classifiers", Json::object());
            if (!d.contains(classifier))
                throw Error("launch", "native_missing", "版本配置缺少本机原生库。");
            natives.push_back(need("libraries/" + d.at(classifier).value("path", "")).u8string());
        }
    }
    auto jarid = m.value("jar", id);
    if (!safe_id(jarid))
        throw Error("launch", "invalid_jar", "游戏 JAR 标识无效。");
    cp.push_back(need("versions/" + jarid + "/" + jarid + ".jar").u8string());
    auto index = m.value("assetIndex", Json::object()).value("id", m.value("assets", ""));
    if (!safe_id(index))
        throw Error("launch", "missing_asset_index", "版本没有有效的资源索引。");
    auto asset_file = need("assets/indexes/" + index + ".json");
    auto assets = Json::parse(read_file(asset_file, 64 * 1024 * 1024));
    auto objects = assets.value("objects", Json::object());
    for (auto &entry : objects.items()) {
        auto hash = entry.value().value("hash", "");
        if (!std::regex_match(hash, std::regex("[0-9a-f]{40}")))
            throw Error("launch", "invalid_asset_hash", "资源索引包含无效哈希。");
        need("assets/objects/" + hash.substr(0, 2) + "/" + hash);
    }
    auto native_dir = fs::u8path(user_data_root()) / "natives" / id;
    fs::create_directories(native_dir);
    for (auto &archive : natives)
        extract_natives(fs::u8path(archive), native_dir);
    std::string classpath;
    for (auto &path : cp) {
        if (!classpath.empty())
            classpath += p.os == "windows" ? ';' : ':';
        classpath += path;
    }
    std::map<std::string, std::string> values{
        {"auth_player_name", identity.name},
        {"auth_uuid", identity.uuid},
        {"auth_access_token", identity.token},
        {"auth_session", "token:" + identity.token + ":" + identity.uuid},
        {"user_type", "msa"},
        {"user_properties", "{}"},
        {"clientid", identity.client_id},
        {"auth_xuid", identity.xuid},
        {"version_name", id},
        {"version_type", m.value("type", "release")},
        {"game_directory", directory.u8string()},
        {"assets_root", (root / "assets").u8string()},
        {"assets_index_name", index},
        {"game_assets", (root / "assets/virtual" / index).u8string()},
        {"natives_directory", native_dir.u8string()},
        {"library_directory", (root / "libraries").u8string()},
        {"classpath_separator", p.os == "windows" ? ";" : ":"},
        {"classpath", classpath},
        {"launcher_name", "SECL"},
        {"launcher_version", "0.1.0"}};
    auto expand = [&](std::string text) {
        std::regex variable(R"(\$\{([^}]+)\})");
        std::smatch match;
        std::string result;
        while (std::regex_search(text, match, variable)) {
            auto key = match[1].str();
            if (!values.count(key))
                throw Error("launch", "unknown_placeholder", "不支持此启动参数变量：" + key);
            result += match.prefix().str() + values.at(key);
            text = match.suffix().str();
        }
        return result + text;
    };
    auto append = [&](std::vector<std::string> &to, const Json &args) {
        for (auto &arg : args) {
            if (arg.is_string())
                to.push_back(expand(arg.get<std::string>()));
            else if (rules_allow(arg.value("rules", Json::array()), p)) {
                auto v = arg.at("value");
                if (v.is_string())
                    to.push_back(expand(v.get<std::string>()));
                else
                    for (auto &s : v)
                        to.push_back(expand(s.get<std::string>()));
            }
        }
    };
    Command result{
        java, directory, {"-Xmx" + std::to_string(memory) + "M", "-Dfile.encoding=UTF-8"}};
    if (options.value("min_memory_mib", 0) > 0)
        result.arguments.push_back("-Xms" +
                                   std::to_string(options.at("min_memory_mib").get<int>()) + "M");
    for (auto &a : split_arguments(options.value("jvm_arguments", "")))
        result.arguments.push_back(a);
    auto args = m.value("arguments", Json::object());
    if (args.contains("jvm"))
        append(result.arguments, args["jvm"]);
    else {
        if (p.os == "osx")
            result.arguments.push_back("-XstartOnFirstThread");
        result.arguments.push_back("-Djava.library.path=" + native_dir.u8string());
        result.arguments.push_back("-cp");
        result.arguments.push_back(classpath);
    }
    auto logging = m.value("logging", Json::object()).value("client", Json::object());
    if (!logging.empty()) {
        auto path =
            need("assets/log_configs/" + logging.value("file", Json::object()).value("id", ""));
        auto arg = logging.value("argument", "");
        auto position = arg.find("${path}");
        if (position == arg.npos)
            throw Error("launch", "invalid_logging", "日志配置参数无效。");
        arg.replace(position, 7, path.u8string());
        result.arguments.push_back(arg);
    }
    auto main = m.value("mainClass", "");
    if (!std::regex_match(main, std::regex("[A-Za-z_$][A-Za-z0-9_$.]*")))
        throw Error("launch", "invalid_main_class", "游戏主类无效。");
    result.arguments.push_back(main);
    if (args.contains("game"))
        append(result.arguments, args["game"]);
    else
        for (auto &a : split_arguments(m.value("minecraftArguments", "")))
            result.arguments.push_back(expand(a));
    if (options.value("custom_resolution", false)) {
        result.arguments.insert(result.arguments.end(),
                                {"--width", std::to_string(options.value("width", 854)), "--height",
                                 std::to_string(options.value("height", 480))});
    }
    if (options.value("fullscreen", false))
        result.arguments.push_back("--fullscreen");
    for (auto &a : split_arguments(options.value("game_arguments", "")))
        result.arguments.push_back(a);
    return result;
}
void extract_natives(const fs::path &archive, const fs::path &directory) {
    auto bytes = read_file(archive, 128 * 1024 * 1024);
    auto u16 = [&](size_t p) -> std::uint16_t {
        if (p + 2 > bytes.size())
            throw Error("launch", "zip_truncated", "原生库压缩包已截断。");
        return (unsigned char)bytes[p] | std::uint16_t((unsigned char)bytes[p + 1]) << 8;
    };
    auto u32 = [&](size_t p) -> std::uint32_t { return u16(p) | std::uint32_t(u16(p + 2)) << 16; };
    size_t end = std::string::npos;
    if (bytes.size() >= 22) {
        auto begin = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
        for (size_t p = bytes.size() - 22;; --p) {
            if (u32(p) == 0x06054b50 && p + 22 + u16(p + 20) == bytes.size()) {
                end = p;
                break;
            }
            if (p == begin)
                break;
        }
    }
    if (end == std::string::npos || u16(end + 4) || u16(end + 6) || u16(end + 10) == 65535)
        throw Error("launch", "zip_unsupported", "不支持此原生库 ZIP 格式。");
    size_t cursor = u32(end + 16), total = 0;
    for (unsigned n = 0; n < u16(end + 10); ++n) {
        if (u32(cursor) != 0x02014b50)
            throw Error("launch", "zip_invalid", "原生库 ZIP 目录损坏。");
        auto flags = u16(cursor + 8), method = u16(cursor + 10);
        auto crc = u32(cursor + 16), compressed = u32(cursor + 20), size = u32(cursor + 24),
             offset = u32(cursor + 42);
        auto len = u16(cursor + 28);
        if (cursor + 46 + len > bytes.size())
            throw Error("launch", "zip_invalid", "ZIP 文件名损坏。");
        auto name = bytes.substr(cursor + 46, len);
        auto mode = (u32(cursor + 38) >> 16) & 0170000;
        cursor += 46 + len + u16(cursor + 30) + u16(cursor + 32);
        if (flags & 1 || mode == 0120000 || compressed > 128 * 1024 * 1024 ||
            size > 128 * 1024 * 1024)
            throw Error("launch", "zip_unsafe", "不安全的原生库 ZIP 条目。");
        if (name.empty() || name.back() == '/' || name.rfind("META-INF/", 0) == 0)
            continue;
        auto target = inside(directory, name);
        total += size;
        if (total > 256 * 1024 * 1024 || u32(offset) != 0x04034b50)
            throw Error("launch", "zip_limit", "原生库 ZIP 超过限制或损坏。");
        size_t start = size_t(offset) + 30 + u16(offset + 26) + u16(offset + 28);
        if (start + compressed > bytes.size())
            throw Error("launch", "zip_truncated", "原生库数据已截断。");
        std::string output(size, '\0');
        if (method == 0)
            output = bytes.substr(start, compressed);
        else if (method == 8) {
            z_stream z{};
            z.next_in = (Bytef *)bytes.data() + start;
            z.avail_in = compressed;
            z.next_out = (Bytef *)output.data();
            z.avail_out = size;
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                throw Error("launch", "zip_inflate", "无法初始化解压。");
            auto r = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (r != Z_STREAM_END || z.total_out != size)
                throw Error("launch", "zip_inflate", "原生库解压失败。");
        } else
            throw Error("launch", "zip_compression", "不支持此 ZIP 压缩算法。");
        if (output.size() != size ||
            crc32(0, (const Bytef *)output.data(), uInt(output.size())) != crc)
            throw Error("launch", "zip_crc", "原生库 ZIP 校验失败。");
        fs::create_directories(target.parent_path());
        std::ofstream f(target, std::ios::binary | std::ios::trunc);
        if (!f.write(output.data(), std::streamsize(output.size())))
            throw Error("launch", "native_write_failed", "无法写入原生库缓存。");
    }
}
} // namespace solar
