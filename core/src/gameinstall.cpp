// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include <future>
namespace solar::engine {
using namespace detail;
static constexpr auto manifest_url =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
Json game_versions(const Task &t) {
    auto j = fetch_json(manifest_url, t);
    require(j.contains("versions") && j["versions"].is_array(), "invalid_manifest",
            "游戏版本列表格式无效。");
    for (auto &v : j["versions"])
        v["channel"] = game_channel(v.value("id", ""), v.value("type", ""));
    return j;
}
static Json version_document(const Json &e, const Task &t) {
    auto bytes = fetch_data(e.at("url"), t);
    require(hash_bytes(bytes, "sha1") == e.at("sha1"), "metadata_checksum",
            "游戏元数据 SHA-1 不匹配。");
    auto m = Json::parse(bytes);
    require(m.value("id", "") == e.at("id"), "metadata_identity", "游戏元数据 ID 不匹配。");
    return m;
}
static void files(const Json &entries, const fs::path &root, const Task &t) {
    size_t done = 0;
    for (size_t start = 0; start < entries.size(); start += 4) {
        std::vector<std::future<void>> jobs;
        for (size_t k = start; k < std::min(start + 4, entries.size()); ++k) {
            auto f = entries[k];
            jobs.push_back(std::async(std::launch::async, [&, f] {
                auto target = checked_path(root, f.at("path"));
                auto worker = t;
                worker.progress = {};
                download(f, target, worker);
                if (f.value("executable", false))
                    fs::permissions(target,
                                    fs::perms::owner_exec | fs::perms::group_exec |
                                        fs::perms::others_exec,
                                    fs::perm_options::add);
            }));
        }
        std::exception_ptr error;
        for (auto &j : jobs)
            try {
                j.get();
                ++done;
                t.report("下载并校验文件", done, entries.size());
            } catch (...) {
                if (!error)
                    error = std::current_exception();
            }
        if (error)
            std::rethrow_exception(error);
    }
}
static Json choose_java(const Json &r, const Json &m, const Task &t) {
    int recommended = major(m);
    auto choice = r.value("java_choice", Json::object());
    auto selected = r.value("java", choice.value("path", ""));
    auto validate = [&](Json j) {
        require(j.is_object(), "invalid_java", "所选 Java 无法运行。");
        require(j.value("major", 0) > 0, "invalid_java", "所选 Java 无法运行。");
        require(j.value("major", 0) >= recommended ||
                    choice.value("allow_below_recommended", false),
                "java_below_recommended", "所选 Java 低于默认版本；请先明确启用强制使用。");
        for (auto key : {"runtime_version", "vendor", "vendor_version", "arch"})
            if (!choice.value(key, "").empty())
                require(choice.at(key) == j.value(key, ""), "java_pin_changed",
                        "固定 Java 的构建、发行版或架构已变化。");
        return j;
    };
    if (!selected.empty())
        return validate(probe_java(fs::u8path(selected)));
    require(choice.value("mode", "auto") != "fixed", "java_missing", "固定 Java 尚未选择。");
    auto known = detect_javas();
    for (auto &j : known)
        if (j.value("major", 0) == recommended &&
            normalize_arch(j.value("arch", "")) == platform().arch)
            return j;
    auto root = fs::u8path(r.at("root").get<std::string>());
    auto component = m.value("javaVersion", Json::object()).value("component", "");
    require(id(component), "java_missing", "未找到匹配 Java，请手动安装。");
    auto p = platform();
    std::string key = p.os == "osx"       ? (p.arch == "aarch64" ? "mac-os-arm64" : "mac-os")
                      : p.os == "windows" ? (p.arch == "aarch64" ? "windows-arm64"
                                             : p.arch == "x86"   ? "windows-x86"
                                                                 : "windows-x64")
                                          : (p.arch == "x86"      ? "linux-i386"
                                             : p.arch == "x86_64" ? "linux"
                                                                  : "");
    require(!key.empty(), "runtime_unavailable",
            "此平台未提供官方 Java 运行时，请手动选择发行版。");
    auto all = fetch_json("https://piston-meta.mojang.com/v1/products/java-runtime/"
                          "2ec0cc96c44e5a76b9c8b7c39df7210883d12871/all.json",
                          t);
    auto choices = all.value(key, Json::object()).value(component, Json::array());
    require(!choices.empty(), "runtime_unavailable", "此平台未提供该 Java 运行时，请手动安装。");
    auto desc = choices[0].at("manifest");
    auto bytes = fetch_data(desc.at("url"), t);
    require(hash_bytes(bytes, "sha1") == desc.at("sha1"), "runtime_checksum",
            "Java 运行时元数据校验失败。");
    auto manifest = Json::parse(bytes).at("files");
    auto runtime = checked_path(root, "runtime/solar/" + component + "/" + key);
    Json pending = Json::array();
    std::string executable;
    for (auto &e : manifest.items()) {
        checked_path(runtime, e.key());
        auto v = e.value();
        if (v.value("type", "") == "file") {
            auto d = v.at("downloads").at("raw");
            d["path"] = e.key();
            d["executable"] = v.value("executable", false);
            pending.push_back(d);
            if (e.key() == "bin/java" || e.key() == "bin/java.exe" ||
                e.key().find("/Home/bin/java") != std::string::npos)
                executable = e.key();
        } else if (v.value("type", "") == "directory")
            fs::create_directories(checked_path(runtime, e.key()));
    }
    files(pending, runtime, t);
    for (int pass = 0; pass < 16; ++pass) {
        bool unresolved = false;
        for (auto &e : manifest.items())
            if (e.value().value("type", "") == "link") {
                auto to = checked_path(runtime, e.key());
                if (fs::exists(to))
                    continue;
                auto relative = (fs::u8path(e.key()).parent_path() /
                                 fs::u8path(e.value().at("target").get<std::string>()))
                                    .lexically_normal()
                                    .generic_u8string();
                auto from = checked_path(runtime, relative);
                if (!manifest.contains(relative) || !fs::is_regular_file(from)) {
                    unresolved = true;
                    continue;
                }
                atomic_write(to, read_file(from, 256 * 1024 * 1024));
                fs::permissions(to, fs::status(from).permissions());
            }
        if (!unresolved)
            break;
        require(pass < 15, "runtime_links", "Java 运行时链接无法解析。");
    }
    require(!executable.empty(), "runtime_missing_executable", "Java 运行时缺少执行文件。");
    auto j = probe_java(checked_path(runtime, executable));
    require(j.value("major", 0) == recommended && normalize_arch(j.value("arch", "")) == p.arch,
            "runtime_mismatch", "下载的 Java 无法运行或版本、架构不匹配。");
    atomic_write(runtime / ".solar-complete", "verified\n");
    return j;
}
Json install_game(const Json &r, const Task &t) {
    auto root = fs::absolute(fs::u8path(r.at("root").get<std::string>()));
    auto target_id = r.at("id").get<std::string>();
    require(id(target_id), "invalid_id", "实例 ID 无效。");
    auto document = checked_path(root, "versions/" + target_id + "/" + target_id + ".json");
    Lock lock(document.parent_path());
    auto original = fs::is_regular_file(document) ? read_file(document) : "";
    Json m;
    if (r.contains("version_entry"))
        m = version_document(r.at("version_entry"), t);
    else {
        require(!original.empty(), "missing_version", "需要已有版本或官方版本条目。");
        m = load_version(root, target_id);
    }
    auto components = loader_components(m);
    require(components.size() <= 1, "multiple_loaders", "不能同时使用多个主加载器。");
    auto current = components.empty() ? "vanilla" : components.begin().key();
    auto requested = r.value("loader", current);
    require(requested == "vanilla" || requested == "fabric" || requested == "quilt" ||
                requested == "forge" || requested == "neoforge",
            "unsupported_loader", "该加载器尚未接入。");
    require(current == "vanilla" || requested == current, "loader_change",
            "请为其他主加载器创建独立实例。");
    auto version = m.value("_baseId", m.value("id", ""));
    auto base = m.value("_solarVanilla", m.value("_m2clVanilla", Json::object()));
    if (requested != "vanilla" && r.contains("loader_version")) {
        require(!m.contains("_m2clOptifine") && !m.contains("_solarOptifine"),
                "remove_optifine_first", "请先移除 OptiFine 再更新主加载器。");
        if (base.empty()) {
            require(current == "vanilla", "external_loader",
                    "外部加载器配置缺少原版元数据，不能直接覆盖。");
            base = m;
        }
        auto req = r;
        req["version"] = version;
        if (requested == "forge" || requested == "neoforge") {
            auto j = choose_java(req, base, t);
            m = merge(base,
                      install_forge(req, base, fs::u8path(j.at("path").get<std::string>()), t));
        } else {
            bool quilt = requested == "quilt";
            auto profile =
                fetch_json(std::string(quilt ? "https://meta.quiltmc.org/v3/versions/loader/"
                                             : "https://meta.fabricmc.net/v2/versions/loader/") +
                               url_encode(version) + "/" + url_encode(r.at("loader_version")) +
                               "/profile/json",
                           t);
            require(profile.value("inheritsFrom", "") == version &&
                        profile.value("mainClass", "") ==
                            (quilt ? "org.quiltmc.loader.impl.launch.knot.KnotClient"
                                   : "net.fabricmc.loader.impl.launch.knot.KnotClient"),
                    "loader_profile_mismatch", "加载器启动配置与游戏版本不匹配。");
            require(loader_components(profile) == Json{{requested, r.at("loader_version")}},
                    "loader_profile_mismatch", "加载器构建不匹配。");
            m = merge(base, profile);
        }
        m["_solarVanilla"] = base;
    }
    auto java = choose_java(r, m, t);
    auto p = platform();
    p.arch = normalize_arch(java.value("arch", p.arch));
    Json pending = Json::array();
    auto add = [&](Json d, std::string path) {
        d["path"] = path;
        pending.push_back(d);
    };
    auto libs = m.value("libraries", Json::array());
    for (auto &lib : libs) {
        if (!rules_allow(lib.value("rules", Json::array()), p))
            continue;
        if (!lib.contains("downloads")) {
            auto path = maven_path(lib.at("name"));
            auto url = lib.value("url", "https://libraries.minecraft.net/");
            if (url.back() != '/')
                url += '/';
            url += path;
            auto hash = lib.value("sha1", "");
            if (hash.empty())
                hash = sha1(fetch_data(url + ".sha1", t, 4096));
            lib["downloads"] = {
                {"artifact", {{"path", path}, {"url", url}, {"sha1", hash}, {"size", -1}}}};
        }
        auto d = lib.at("downloads");
        if (d.contains("artifact"))
            add(d.at("artifact"), "libraries/" + d.at("artifact").at("path").get<std::string>());
        auto native = lib.value("natives", Json::object()).value(p.os, "");
        if (!native.empty()) {
            auto n = native.find("${arch}");
            if (n != native.npos)
                native.replace(n, 7, p.arch == "x86" ? "32" : "64");
            auto all = d.value("classifiers", Json::object());
            require(all.contains(native), "native_unavailable", "该平台的原生运行库不存在。");
            add(all.at(native), "libraries/" + all.at(native).at("path").get<std::string>());
        }
    }
    m["libraries"] = libs;
    auto client = m.value("downloads", Json::object()).value("client", Json::object());
    require(!client.empty(), "missing_client", "缺少官方客户端信息。");
    add(client, "versions/" + target_id + "/" + target_id + ".jar");
    auto index = m.value("assetIndex", Json::object());
    auto index_id = index.value("id", m.value("assets", ""));
    require(id(index_id) && index.contains("url"), "missing_asset_index", "缺少官方资源索引。");
    add(index, "assets/indexes/" + index_id + ".json");
    auto logging = m.value("logging", Json::object())
                       .value("client", Json::object())
                       .value("file", Json::object());
    if (!logging.empty())
        add(logging, "assets/log_configs/" + logging.at("id").get<std::string>());
    files(pending, root, t);
    auto assets = read_json(checked_path(root, "assets/indexes/" + index_id + ".json"));
    pending = Json::array();
    auto objects = assets.value("objects", Json::object());
    for (auto &e : objects.items()) {
        auto d = e.value();
        auto hash = d.value("hash", "");
        require(std::regex_match(hash, std::regex("[0-9a-f]{40}")), "invalid_asset",
                "资源哈希无效。");
        add({{"url", "https://resources.download.minecraft.net/" + hash.substr(0, 2) + "/" + hash},
             {"sha1", hash},
             {"size", d.at("size")}},
            "assets/objects/" + hash.substr(0, 2) + "/" + hash);
    }
    files(pending, root, t);
    auto directory = r.value("directory", (root / "instances" / target_id).u8string());
    fs::create_directories(fs::u8path(directory));
    if (assets.value("virtual", false) || assets.value("map_to_resources", false)) {
        auto target = assets.value("map_to_resources", false)
                          ? fs::u8path(directory) / "resources"
                          : checked_path(root, "assets/virtual/" + index_id);
        for (auto &e : objects.items()) {
            auto hash = e.value().at("hash").get<std::string>();
            auto dest = checked_path(target, e.key());
            if (!fs::exists(dest))
                atomic_write(dest, read_file(checked_path(root, "assets/objects/" +
                                                                    hash.substr(0, 2) + "/" + hash),
                                             256 * 1024 * 1024));
        }
    }
    t.check();
    verify_loader_outputs(m, root);
    if (!original.empty())
        require(read_file(document) == original, "version_changed",
                "安装期间版本配置已变化，请重新确认。");
    m["_baseId"] = version;
    m["id"] = target_id;
    m["_solarGameDirectory"] = directory;
    atomic_write(document, m.dump(2));
    return {{"id", target_id}, {"directory", directory}, {"java", java}, {"metadata", m}};
}
} // namespace solar::engine
