// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
namespace solar::engine {
std::string normalize_arch(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if (s == "amd64" || s == "x64" || s == "x86-64")
        return "x86_64";
    if (s == "arm64")
        return "aarch64";
    if (s == "i386" || s == "i686" || s == "i586")
        return "x86";
    return s;
}
Json parse_java(const std::string &out) {
    Json j;
    auto field = [&](const std::string &key) {
        std::smatch m;
        return std::regex_search(out, m, std::regex(key + R"(\s*=\s*([^\r\n]+))")) ? m[1].str()
                                                                                   : "";
    };
    auto version = field("java\\.version");
    if (version.empty()) {
        std::smatch m;
        if (std::regex_search(out, m, std::regex("version \\\"([^\\\"]+)\\\"")))
            version = m[1];
    }
    if (version.empty())
        return j;
    int major = 0;
    try {
        major = std::stoi(version.rfind("1.", 0) == 0 ? version.substr(2) : version);
    } catch (...) {
        return j;
    }
    return {{"version", version},
            {"major", major},
            {"arch", normalize_arch(field("os\\.arch"))},
            {"vendor", field("java\\.vendor")},
            {"runtime_version", field("java\\.runtime\\.version")},
            {"vm_name", field("java\\.vm\\.name")},
            {"vendor_version", field("java\\.vendor\\.version")}};
}
Json probe_java(const fs::path &p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec))
        return nullptr;
    auto properties = run_capture(p, {"-XshowSettings:properties", "-version"});
    auto j = parse_java(properties);
    if (!j.is_null())
        j["path"] = java_runtime_executable(p, properties).u8string();
    return j;
}
Json scan_java(const std::vector<fs::path> &roots, bool deep, const Task &task, int max_depth,
               int budget) {
    Json out = Json::array();
    std::set<fs::path> seen, candidates;
    int directories = 0;
    auto add = [&](const fs::path &p) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) {
            auto real = fs::canonical(p, ec);
            if (!ec)
                candidates.insert(real);
        }
    };
    for (auto &root : roots) {
        task.check();
        std::error_code ec;
        if (fs::is_regular_file(root, ec)) {
            add(root);
            continue;
        }
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied,
                                            ec),
            end;
        if (ec)
            continue;
        while (it != end && !ec) {
            task.check();
            auto p = it->path();
            if (it->is_directory(ec)) {
                ++directories;
                if (directories > budget)
                    break;
                if (it.depth() > max_depth ||
                    (!deep && (p.filename() == "node_modules" || p.filename() == ".git" ||
                               p.filename() == ".Trash")))
                    it.disable_recursion_pending();
                task.report(p.u8string(), directories, budget);
            } else if (p.filename() == (platform().os == "windows" ? "java.exe" : "java"))
                add(p);
            it.increment(ec);
        }
        if (directories > budget)
            break;
    }
    for (auto &p : candidates) {
        task.check();
        auto j = probe_java(p);
        if (!j.is_null() && seen.insert(fs::u8path(j.at("path").get<std::string>())).second)
            out.push_back(j);
    }
    return {{"runtimes", out},
            {"directories", directories},
            {"limited", directories > budget},
            {"cancelled", false}};
}
Json java_legal(const fs::path &exe) {
    auto home = exe.parent_path().parent_path();
    Json files = Json::array();
    for (const auto &name : {"LICENSE", "LICENSE.txt", "LICENSE.md", "ASSEMBLY_EXCEPTION",
                             "THIRD_PARTY_README", "DISCLAIMER"})
        if (fs::is_regular_file(home / name))
            files.push_back((home / name).u8string());
    return {
        {"home", home.u8string()},
        {"legal_directory", fs::is_directory(home / "legal") ? (home / "legal").u8string() : ""},
        {"licenses", files}};
}
Json system_memory() {
    std::int64_t total = 0, available = -1;
    std::string source;
    bool estimated = false;
#ifdef _WIN32
    MEMORYSTATUSEX s{};
    s.dwLength = sizeof(s);
    if (GlobalMemoryStatusEx(&s)) {
        total = s.ullTotalPhys / 1048576;
        available = s.ullAvailPhys / 1048576;
        source = "Windows GlobalMemoryStatusEx";
    }
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    size_t length = sizeof(bytes);
    vm_size_t page = 0;
    vm_statistics64_data_t stats{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    auto host = mach_host_self();
    if (sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) == 0 &&
        host_page_size(host, &page) == KERN_SUCCESS &&
        host_statistics64(host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count) ==
            KERN_SUCCESS) {
        total = std::int64_t(bytes / 1048576);
        available =
            std::int64_t((uint64_t(stats.free_count) + stats.inactive_count) * page / 1048576);
        source = "macOS free + inactive pages";
        estimated = true;
    }
    mach_port_deallocate(mach_task_self(), host);
#else
    std::ifstream f("/proc/meminfo");
    std::string name, unit;
    std::int64_t n;
    while (f >> name >> n >> unit) {
        if (name == "MemTotal:")
            total = n / 1024;
        if (name == "MemAvailable:")
            available = n / 1024;
    }
    source = "Linux MemAvailable";
#endif
    return {{"total_mib", total},
            {"available_mib", std::min(total, available)},
            {"source", source},
            {"estimated", estimated}};
}
Json recommend_memory(const Json &s, const Json &c) {
    auto total = s.value("total_mib", 0LL), available = s.value("available_mib", -1LL);
    if (total <= 0 || available < 0 || available > total)
        throw Error("memory", "invalid_snapshot", "无法读取有效的系统可用内存，请手动指定。");
    int target = 3072;
    std::smatch match;
    auto game = c.value("game_version", "");
    if (std::regex_search(game, match, std::regex(R"(^1\.(\d+)(?:\.|$))")) &&
        std::stoi(match[1]) < 18)
        target = 2048;
    int mods = c.value("enabled_mods", 0);
    if (mods > 0)
        target = mods <= 50 ? 4096 : mods <= 150 ? 6144 : 8192;
    int suggested = c.value("pack_suggested_mib", 0);
    if (suggested > 0)
        target = suggested;
    auto reserve = std::clamp(total / 8, 1024LL, 2048LL);
    auto budget = std::min(16384LL, std::min(total / 2, std::max(0LL, available - reserve)));
    if (normalize_arch(c.value("java_arch", "")) == "x86")
        budget = std::min(budget, 768LL);
    int maximum = int(std::min<std::int64_t>(target, budget) / 256 * 256);
    if (maximum < 512)
        throw Error("memory", "insufficient_memory", "当前可用内存不足以给出 512 MiB 的建议。");
    if (suggested > maximum)
        throw Error("memory", "below_pack_requirement", "当前内存预算低于整合包建议。");
    return {{"maximum_mib", maximum},
            {"target_mib", target},
            {"budget_mib", budget / 256 * 256},
            {"reserve_mib", reserve},
            {"estimated", s.value("estimated", false)},
            {"warning", maximum < target ? "当前可用内存限制了分配上限。" : ""}};
}
void validate_launch_options(const Json &o) {
    auto max = o.value("max_memory_mib", 2048), min = o.value("min_memory_mib", 0),
         w = o.value("width", 854), h = o.value("height", 480);
    if (max < 512 || max > 32768 || min < 0 || min > 32768 || (min > 0 && min < 128) ||
        (!o.value("auto_memory", true) && min > max))
        throw Error("options", "invalid_memory", "内存参数超出范围。");
    if (w < 320 || w > 16384 || h < 240 || h > 16384)
        throw Error("options", "invalid_resolution", "窗口尺寸超出范围。");
    auto jvm = o.value("jvm_arguments", ""), game = o.value("game_arguments", "");
    if (jvm.size() > 32768 || game.size() > 32768 || jvm.find('\0') != jvm.npos ||
        game.find('\0') != game.npos)
        throw Error("options", "invalid_arguments", "附加参数过长或包含无效字符。");
    std::set<std::string> managed{"-jar", "-cp",      "-classpath", "--class-path",
                                  "-m",   "--module", "--"};
    std::set<std::string> takes_value{
        "--add-opens",           "--add-exports",   "--add-reads",
        "--patch-module",        "--module-path",   "-p",
        "--upgrade-module-path", "--limit-modules", "--enable-native-access"};
    bool value = false;
    for (auto &a : split_arguments(jvm)) {
        if (value) {
            if (a.empty() || a[0] == '-')
                throw Error("options", "argument_value_missing", "JVM 参数缺少选项值。");
            value = false;
            continue;
        }
        auto key = a.substr(0, a.find('='));
        if (a.rfind("-Xmx", 0) == 0 || a.rfind("-Xms", 0) == 0 ||
            a.rfind("-XX:MaxHeapSize=", 0) == 0 || a.rfind("-XX:InitialHeapSize=", 0) == 0 ||
            a.rfind("-Djava.class.path=", 0) == 0 || managed.count(key) || a.empty() ||
            a[0] != '-' || a[0] == '@')
            throw Error("options", "managed_argument",
                        "附加 JVM 参数不能覆盖启动器管理的内存、主类或类路径。");
        value = takes_value.count(a) > 0;
    }
    if (value)
        throw Error("options", "argument_value_missing", "JVM 参数缺少选项值。");
    std::set<std::string> game_managed{
        "--width",       "--height",      "--fullscreen", "--gameDir",
        "--assetsDir",   "--assetIndex",  "--username",   "--uuid",
        "--accessToken", "--session",     "--userType",   "--userProperties",
        "--version",     "--versionType", "--clientId",   "--xuid"};
    for (auto &a : split_arguments(game))
        if (game_managed.count(a.substr(0, a.find('='))))
            throw Error("options", "managed_argument",
                        "附加游戏参数不能覆盖已验证的身份和实例设置。");
}
Json loader_components(const Json &m) {
    Json out = Json::object();
    auto game = m.value("_baseId", m.value("inheritsFrom", m.value("id", "")));
    for (auto &l : m.value("libraries", Json::array())) {
        auto n = l.value("name", "");
        std::string key;
        if (n.rfind("net.fabricmc:fabric-loader:", 0) == 0)
            key = "fabric";
        if (n.rfind("org.quiltmc:quilt-loader:", 0) == 0)
            key = "quilt";
        if (n.rfind("net.neoforged:neoforge:", 0) == 0 || n.rfind("net.neoforged:forge:", 0) == 0)
            key = "neoforge";
        if (n.rfind("net.minecraftforge:forge:", 0) == 0 ||
            n.rfind("net.minecraftforge:fmlloader:", 0) == 0)
            key = "forge";
        if (!key.empty()) {
            auto pos = n.find(':', n.find(':') + 1);
            auto v = n.substr(pos + 1);
            v = v.substr(0, v.find(':'));
            if (v.rfind(game + "-", 0) == 0)
                v.erase(0, game.size() + 1);
            out[key] = v;
        }
    }
    auto args = m.value("arguments", Json::object()).value("game", Json::array());
    for (size_t k = 0; k + 1 < args.size(); ++k) {
        if (args[k] == "--fml.neoForgeVersion")
            out["neoforge"] = args[k + 1];
        if (args[k] == "--fml.forgeVersion")
            out["forge"] = args[k + 1];
    }
    return out;
}
Json load_instance_settings(const fs::path &p) {
    auto current = p / ".solar-instance.json";
    auto result = read_json(fs::exists(current) ? current : p / ".m2cl-instance.json");
    auto launch = result.value("launch", Json::object());
    for (auto mapping :
         std::vector<std::pair<std::string, std::string>>{{"autoMemory", "auto_memory"},
                                                          {"maxMemoryMiB", "max_memory_mib"},
                                                          {"minMemoryMiB", "min_memory_mib"},
                                                          {"customResolution", "custom_resolution"},
                                                          {"jvmArguments", "jvm_arguments"},
                                                          {"gameArguments", "game_arguments"}})
        if (launch.contains(mapping.first) && !launch.contains(mapping.second))
            launch[mapping.second] = launch.at(mapping.first);
    result["launch"] = launch;
    return result;
}
void save_instance_settings(const fs::path &p, const Json &value) {
    if (!value.is_object())
        throw Error("settings", "invalid_instance_settings", "实例设置必须是 JSON 对象。");
    validate_launch_options(value.value("launch", Json::object()));
    if (value.contains("offlinePlayer") || value.contains("offlineSkin"))
        throw Error("settings", "offline_unsupported", "SECL 不提供离线身份设置。");
    atomic_write(p / ".solar-instance.json", value.dump(2));
}
Json scan_instances(const std::vector<fs::path> &roots, const Json &overrides) {
    Json instances = Json::array(), warnings = Json::array();
    std::set<fs::path> seen;
    for (auto &root : roots) {
        try {
            for (auto &v : scan_versions(root)) {
                auto id = v.at("id").get<std::string>();
                if (!v.value("available", false)) {
                    warnings.push_back(v.value("reason", "版本配置无法读取。"));
                    continue;
                }
                auto path = fs::canonical(root / "versions" / fs::u8path(id));
                if (!seen.insert(path).second)
                    continue;
                auto m = load_version(root, id);
                auto loaders = loader_components(m);
                std::string loader = loaders.empty()       ? "vanilla"
                                     : loaders.size() == 1 ? loaders.begin().key()
                                                           : "conflict";
                bool isolated = fs::exists(path / "mods") || fs::exists(path / "saves") ||
                                fs::exists(path / "options.txt");
                if (!isolated && !fs::exists(root / "mods") && loader != "vanilla")
                    isolated = true;
                if (overrides.contains(path.u8string()))
                    isolated = overrides[path.u8string()].get<bool>();
                instances.push_back(
                    {{"id", id},
                     {"path", path.u8string()},
                     {"root", fs::canonical(root).u8string()},
                     {"version", m.value("_baseId", m.value("id", id))},
                     {"loader", loader},
                     {"loader_version",
                      loaders.size() == 1 ? loaders.begin().value().get<std::string>() : ""},
                     {"loader_components", loaders},
                     {"loader_problem",
                      loaders.size() > 1 ? "同时检测到多个主加载器，请使用独立实例。" : ""},
                     {"isolated", isolated},
                     {"game_directory",
                      m.value("_solarGameDirectory", (isolated ? path : root).u8string())},
                     {"java_major",
                      m.value("javaVersion", Json::object()).value("majorVersion", 0)},
                     {"optifine",
                      m.value("_solarOptifine", m.value("_m2clOptifine", Json::object()))}});
            }
        } catch (const std::exception &e) {
            warnings.push_back(e.what());
        }
    }
    return {{"instances", instances}, {"warnings", warnings}};
}
std::string clean_log(const std::string &s, const std::vector<std::string> &secrets) {
    auto out = redact(s, secrets);
    out = std::regex_replace(
        out,
        std::regex(R"((--accessToken|--session|Authorization: Bearer)([ =]+)[^ \r\n]+)",
                   std::regex::icase),
        "$1$2[REDACTED]");
    out = std::regex_replace(
        out,
        std::regex(R"LOG(("(?:access_token|refresh_token|Token)"\s*:\s*")[^"]+)LOG",
                   std::regex::icase),
        "$1[REDACTED]");
    return out;
}
} // namespace solar::engine
