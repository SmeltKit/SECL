// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include "tinyxml2.h"
namespace solar::engine {
using namespace detail;
static std::string repository(const std::string &loader) {
    if (loader == "forge")
        return "https://maven.minecraftforge.net/net/minecraftforge/forge/";
    if (loader == "neoforge")
        return "https://maven.neoforged.net/releases/net/neoforged/neoforge/";
    throw Error("loader", "unsupported", "不支持此主加载器。");
}
static std::string neo_game(const std::string &v) {
    std::smatch m;
    if (!std::regex_match(v, m,
                          std::regex("([0-9]+)\\.([0-9]+)\\.([0-9]+)(?:\\.([0-9]+"
                                     "))?(?:-[A-Za-z0-9.]+)?")))
        return {};
    int first = std::stoi(m[1]);
    if (first < 20)
        return {};
    return first >= 26
               ? (m[4].matched
                      ? m[1].str() + "." + m[2].str() + (m[3] == "0" ? "" : "." + m[3].str())
                      : "")
               : "1." + m[1].str() + (m[2] == "0" ? "" : "." + m[2].str());
}
std::string maven_path(const std::string &coordinate) {
    std::smatch m;
    require(std::regex_match(coordinate, m,
                             std::regex("([A-Za-z0-9_.-]+):([A-Za-z0-9_.-]+):([A-Za-z0-9_.+-]+"
                                        ")(?::([A-Za-z0-9_.-]+))?(?:@([A-Za-z0-9]+))?")),
            "invalid_coordinate", "无效的 Maven 坐标。");
    auto group = m[1].str();
    std::replace(group.begin(), group.end(), '.', '/');
    auto path = group + "/" + m[2].str() + "/" + m[3].str() + "/" + m[2].str() + "-" + m[3].str() +
                (m[4].matched ? "-" + m[4].str() : "") + "." + (m[5].matched ? m[5].str() : "jar");
    checked_path(fs::temp_directory_path(), path);
    return path;
}
Json loader_versions(const std::string &loader, const std::string &game, const Task &t) {
    require(id(game), "invalid_game", "游戏版本无效。");
    if (loader == "fabric" || loader == "quilt")
        return fetch_json(std::string(loader == "quilt"
                                          ? "https://meta.quiltmc.org/v3/versions/loader/"
                                          : "https://meta.fabricmc.net/v2/versions/loader/") +
                              url_encode(game),
                          t);
    auto xml = fetch_data(repository(loader) + "maven-metadata.xml", t);
    tinyxml2::XMLDocument doc;
    require(doc.Parse(xml.data(), xml.size()) == tinyxml2::XML_SUCCESS, "invalid_loader_xml",
            "加载器版本列表 XML 无效。");
    std::vector<std::string> versions;
    auto meta = doc.FirstChildElement("metadata");
    auto versioning = meta ? meta->FirstChildElement("versioning") : nullptr;
    auto list = versioning ? versioning->FirstChildElement("versions") : nullptr;
    require(list != nullptr, "invalid_loader_xml", "加载器列表缺少版本数据。");
    for (auto v = list->FirstChildElement("version"); v; v = v->NextSiblingElement("version")) {
        auto s = std::string(v->GetText() ? v->GetText() : "");
        if (loader == "forge" && s.rfind(game + "-", 0) == 0)
            versions.push_back(s.substr(game.size() + 1));
        else if (loader == "neoforge" && neo_game(s) == game)
            versions.push_back(s);
    }
    std::reverse(versions.begin(), versions.end());
    Json out = Json::array();
    std::set<std::string> seen;
    for (auto &v : versions)
        if (seen.insert(v).second)
            out.push_back({{"loader",
                            {{"version", v},
                             {"stable", v.find("beta") == v.npos && v.find("alpha") == v.npos}}}});
    return out;
}
void verify_loader_outputs(const Json &m, const fs::path &root) {
    auto components = loader_components(m);
    if (!components.contains("forge") && !components.contains("neoforge"))
        return;
    auto files = m.value("_solarLoaderFiles", m.value("_m2clLoaderFiles", Json::array()));
    require(!files.empty(), "missing_loader_record", "此加载器缺少校验记录，请重新安装。");
    for (auto &f : files) {
        auto p = checked_path(root, f.at("path"));
        require(fs::is_regular_file(p) && fs::file_size(p) == f.at("size") &&
                    hash_file(p, "sha1") == f.at("sha1"),
                "loader_file_changed", "加载器文件缺失或损坏：" + f.at("path").get<std::string>());
    }
}
Json install_forge(const Json &r, const Json &base, const fs::path &java, const Task &t) {
    auto loader = r.at("loader").get<std::string>(),
         version = r.at("loader_version").get<std::string>(),
         game = base.at("id").get<std::string>();
    auto root = fs::u8path(r.at("root").get<std::string>());
    require(std::regex_match(version, std::regex("[0-9]+(?:\\.[0-9]+)+(?:-[A-Za-z0-9.]+)?")) &&
                id(game),
            "invalid_loader_version", "加载器构建无效。");
    if (loader == "neoforge")
        require(neo_game(version) == game, "wrong_neoforge", "NeoForge 构建与游戏版本不匹配。");
    auto coordinate = loader == "forge" ? game + "-" + version : version;
    auto url = repository(loader) + coordinate + "/" + loader + "-" + coordinate + "-installer.jar";
    auto checksum = sha1(fetch_data(url + ".sha1", t, 4096));
    Workspace work(root);
    auto installer = work.path / "installer.jar";
    download({{"url", url}, {"sha1", checksum}, {"size", -1}}, installer, t);
    auto profile = Json::parse(jar_entry(installer, "install_profile.json"));
    require(profile.value("spec", 0) == 1 && profile.value("minecraft", "") == game,
            "unsupported_installer", "此安装器格式或游戏版本尚未接入。");
    auto child = Json::parse(jar_entry(installer, "version.json"));
    require(id(child.value("id", "")) && child.value("inheritsFrom", "") == game,
            "loader_profile_mismatch", "安装器启动配置不匹配。");
    auto client = checked_path(work.path, "versions/" + game + "/" + game + ".jar");
    download(base.at("downloads").at("client"), client, t);
    std::map<std::string, std::string> vars{{"SIDE", "client"},
                                            {"ROOT", work.path.u8string()},
                                            {"LIBRARY_DIR", (work.path / "libraries").u8string()},
                                            {"INSTALLER", installer.u8string()},
                                            {"MINECRAFT_JAR", client.u8string()},
                                            {"MINECRAFT_VERSION", game}};
    auto data = profile.value("data", Json::object());
    for (auto &e : data.items())
        vars[e.key()] = e.value().value("client", "");
    std::function<std::string(std::string, int)> resolve = [&](std::string value, int depth) {
        require(depth <= 24, "recursive_placeholder", "安装变量存在循环引用。");
        if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')
            return value.substr(1, value.size() - 2);
        std::smatch m;
        std::regex re("\\{([A-Z0-9_]+)\\}");
        int count = 0;
        while (std::regex_search(value, m, re)) {
            require(++count <= 128 && vars.count(m[1]), "unknown_placeholder",
                    "安装参数包含未知或循环变量。");
            value.replace(size_t(m.position()), size_t(m.length()),
                          resolve(vars.at(m[1]), depth + 1));
        }
        if (value.size() >= 2 && value.front() == '[' && value.back() == ']')
            return checked_path(work.path,
                                "libraries/" + maven_path(value.substr(1, value.size() - 2)))
                .u8string();
        if (value.rfind("/data/", 0) == 0) {
            auto path = checked_path(work.path, value.substr(1));
            if (!fs::exists(path))
                atomic_write(path, jar_entry(installer, value.substr(1), 128 * 1024 * 1024));
            return path.u8string();
        }
        require(value.find('{') == value.npos && value.find('}') == value.npos,
                "invalid_placeholder", "安装参数无法解析。");
        return value;
    };
    std::map<std::string, Json> artifacts;
    auto libs = profile.value("libraries", Json::array());
    for (auto &v : child.value("libraries", Json::array()))
        libs.push_back(v);
    for (auto &lib : libs) {
        t.check();
        auto a = lib.value("downloads", Json::object()).value("artifact", Json::object());
        require(!a.empty(), "unsupported_installer_library", "安装库格式不支持。");
        auto relative = "libraries/" + a.at("path").get<std::string>();
        auto path = checked_path(work.path, relative);
        if (artifacts.count(relative)) {
            require(artifacts.at(relative).at("sha1") == a.at("sha1"), "conflicting_library",
                    "安装库校验声明冲突。");
            continue;
        }
        artifacts[relative] = a;
        if (a.value("url", "").empty()) {
            auto bytes =
                jar_entry(installer, "maven/" + a.at("path").get<std::string>(), 128 * 1024 * 1024);
            require(bytes.size() == a.at("size") && hash_bytes(bytes, "sha1") == a.at("sha1"),
                    "embedded_library_checksum", "安装器内置库校验失败。");
            atomic_write(path, bytes);
        } else {
            auto cache = checked_path(root, relative);
            download(a, cache, t);
            atomic_write(path, read_file(cache, 256 * 1024 * 1024));
        }
    }
    auto processors = profile.value("processors", Json::array());
    size_t step = 0;
    for (auto &processor : processors) {
        auto sides = processor.value("sides", Json::array());
        if (!sides.empty() && std::find(sides.begin(), sides.end(), "client") == sides.end())
            continue;
        t.report("处理客户端补丁", ++step, processors.size());
        std::vector<std::string> args;
        for (auto &a : processor.value("args", Json::array()))
            args.push_back(resolve(a, 0));
        auto outputs = processor.value("outputs", Json::object());
        auto verify = [&](bool mandatory) {
            bool valid = true;
            for (auto &e : outputs.items()) {
                auto path = fs::u8path(resolve(e.key(), 0));
                auto relative = path.lexically_relative(work.path).generic_u8string();
                checked_path(work.path, relative);
                auto hash = resolve(e.value(), 0);
                require(std::regex_match(hash, std::regex("[a-fA-F0-9]{40}")),
                        "invalid_output_hash", "补丁输出校验值无效。");
                if (!fs::is_regular_file(path) || hash_file(path, "sha1") != sha1(hash))
                    valid = false;
            }
            if (mandatory)
                require(valid, "processor_output_mismatch", "客户端补丁输出校验失败。");
            return valid;
        };
        if (!outputs.empty() && verify(false))
            continue;
        auto task = std::find(args.begin(), args.end(), "--task");
        if (task != args.end() && std::next(task) != args.end() &&
            *std::next(task) == "DOWNLOAD_MOJMAPS") {
            auto output = std::find(args.begin(), args.end(), "--output");
            require(output != args.end() && std::next(output) != args.end(),
                    "missing_mapping_output", "映射下载缺少输出路径。");
            auto path = fs::u8path(*std::next(output));
            checked_path(work.path, path.lexically_relative(work.path).generic_u8string());
            download(base.at("downloads").at("client_mappings"), path, t);
            verify(true);
            continue;
        }
        auto relative = "libraries/" + maven_path(processor.at("jar"));
        require(artifacts.count(relative), "undeclared_processor", "补丁工具未在校验清单中声明。");
        auto jar = checked_path(work.path, relative);
        auto manifest = jar_entry(jar, "META-INF/MANIFEST.MF");
        manifest = std::regex_replace(manifest, std::regex("\r?\n "), "");
        std::smatch main;
        require(std::regex_search(
                    manifest, main,
                    std::regex("(?:^|\n)Main-Class: ([A-Za-z_$][A-Za-z0-9_$.]*)\r?(?:\n|$)")),
                "processor_main_missing", "补丁工具没有有效主类。");
        std::string cp = jar.u8string();
        for (auto &c : processor.value("classpath", Json::array())) {
            auto path = "libraries/" + maven_path(c);
            require(artifacts.count(path), "undeclared_processor_library", "补丁工具依赖未声明。");
            cp +=
                (platform().os == "windows" ? ";" : ":") + checked_path(work.path, path).u8string();
        }
        Command command{java, work.path, {"-Djava.awt.headless=true", "-cp", cp, main[1]}};
        command.arguments.insert(command.arguments.end(), args.begin(), args.end());
        process(command, t);
        verify(true);
    }
    for (auto &key : {"PATCHED", "MC_EXTRA"})
        if (vars.count(key))
            require(fs::is_regular_file(fs::u8path(resolve(vars.at(key), 0))),
                    "missing_generated_client", "客户端补丁输出文件缺失。");
    Json record = Json::array();
    for (const auto &e : fs::recursive_directory_iterator(work.path / "libraries")) {
        t.check();
        require(!e.is_symlink(), "installer_symlink", "安装器生成了不支持的符号链接。");
        if (!e.is_regular_file())
            continue;
        auto relative = e.path().lexically_relative(work.path).generic_u8string();
        auto hash = hash_file(e.path(), "sha1", t.cancel);
        record.push_back({{"path", relative}, {"sha1", hash}, {"size", e.file_size()}});
        auto to = checked_path(root, relative);
        if (!fs::is_regular_file(to) || hash_file(to, "sha1") != hash)
            atomic_write(to, read_file(e.path(), 512 * 1024 * 1024));
    }
    require(!record.empty(), "missing_loader_outputs", "加载器未生成运行库。");
    child["_solarLoaderFiles"] = record;
    child["_solarLoader"] = loader;
    child["_solarLoaderVersion"] = version;
    child["_solarInstallerSha1"] = checksum;
    return child;
}
} // namespace solar::engine
