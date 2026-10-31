// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include "solar/engine.hpp"
#include <regex>
#include <set>
#include <sstream>
namespace solar::engine {
struct Version {
    std::vector<unsigned long long> n;
    std::string pre;
    bool prerelease = false, valid = false;
};
static Version parse_version(const std::string &s) {
    Version v;
    std::smatch m;
    if (!std::regex_match(s, m,
                          std::regex(R"(^(\d+(?:\.\d+)*)(?:-([0-9A-Za-z.-]*))?(?:\+[^\s]+)?$)")))
        return v;
    std::stringstream parts(m[1]);
    std::string t;
    try {
        while (std::getline(parts, t, '.'))
            v.n.push_back(std::stoull(t));
    } catch (...) {
        return {};
    }
    v.prerelease = m[2].matched;
    v.pre = m[2];
    v.valid = true;
    return v;
}
static int compare(const Version &a, const Version &b) {
    for (size_t k = 0; k < std::max(a.n.size(), b.n.size()); ++k) {
        auto x = k < a.n.size() ? a.n[k] : 0, y = k < b.n.size() ? b.n[k] : 0;
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a.prerelease != b.prerelease)
        return a.prerelease ? -1 : 1;
    if (a.pre == b.pre)
        return 0;
    std::stringstream aa(a.pre), bb(b.pre);
    std::string x, y;
    for (;;) {
        bool ax = bool(std::getline(aa, x, '.')), by = bool(std::getline(bb, y, '.'));
        if (ax != by)
            return ax ? 1 : -1;
        if (!ax)
            return 0;
        if (x == y)
            continue;
        bool xn = std::regex_match(x, std::regex("[0-9]+")),
             yn = std::regex_match(y, std::regex("[0-9]+"));
        if (xn && yn) {
            auto a1 = x.find_first_not_of('0'), b1 = y.find_first_not_of('0');
            auto xv = a1 == x.npos ? "0" : x.substr(a1), yv = b1 == y.npos ? "0" : y.substr(b1);
            if (xv.size() != yv.size())
                return xv.size() < yv.size() ? -1 : 1;
            return xv < yv ? -1 : 1;
        }
        if (xn != yn)
            return xn ? -1 : 1;
        return x < y ? -1 : 1;
    }
}
int version_matches(const std::string &raw, const std::string &version) {
    auto range = std::regex_replace(raw, std::regex(R"(^\s+|\s+$)"), "");
    if (range.empty() || range == "*")
        return 1;
    auto pos = range.find("||");
    if (pos != range.npos) {
        auto a = version_matches(range.substr(0, pos), version),
             b = version_matches(range.substr(pos + 2), version);
        return a == 1 || b == 1 ? 1 : a < 0 || b < 0 ? -1 : 0;
    }
    auto current = parse_version(version);
    std::smatch interval;
    if (std::regex_match(range, interval, std::regex(R"(^([\[(])([^,]*),([^\])]*)([\])])$)"))) {
        if (!current.valid)
            return -1;
        for (int bound : {2, 3}) {
            auto text = std::regex_replace(interval[bound].str(), std::regex(R"(^\s+|\s+$)"), "");
            if (text.empty())
                continue;
            auto endpoint = parse_version(text);
            if (!endpoint.valid)
                return -1;
            int c = compare(current, endpoint);
            if (bound == 2 ? (c < 0 || (c == 0 && interval[1] == "("))
                           : (c > 0 || (c == 0 && interval[4] == ")")))
                return 0;
        }
        return 1;
    }
    if (range.front() == '[' && range.back() == ']')
        range = range.substr(1, range.size() - 2);
    std::stringstream ss(range);
    std::string token;
    while (ss >> token) {
        std::smatch m;
        if (!std::regex_match(token, m, std::regex(R"(^(>=|<=|>|<|=|\^|~)?(.+)$)")))
            return -1;
        auto op = m[1].str(), operand = m[2].str();
        auto star = operand.find_first_of("xX*");
        if (star != operand.npos) {
            if (!op.empty() && op != "=")
                return -1;
            auto prefix = parse_version(
                operand.substr(0, star && operand[star - 1] == '.' ? star - 1 : star));
            if (!current.valid || !prefix.valid)
                return -1;
            for (size_t n = 0; n < prefix.n.size(); ++n)
                if (n >= current.n.size() || prefix.n[n] != current.n[n])
                    return 0;
            continue;
        }
        auto want = parse_version(operand);
        if (!current.valid || !want.valid) {
            if (op.empty() || op == "=") {
                if (version != operand)
                    return 0;
                continue;
            }
            return -1;
        }
        int c = compare(current, want);
        if (op == "^" || op == "~") {
            auto upper = want;
            size_t index = op == "~" ? std::min<size_t>(1, want.n.size() - 1) : 0;
            if (op == "^")
                while (index + 1 < upper.n.size() && upper.n[index] == 0)
                    ++index;
            upper.n.resize(std::max(upper.n.size(), index + 1), 0);
            ++upper.n[index];
            for (size_t i = index + 1; i < upper.n.size(); ++i)
                upper.n[i] = 0;
            upper.prerelease = true;
            upper.pre = "";
            if (c < 0 || compare(current, upper) >= 0)
                return 0;
        } else if (op == ">="   ? c < 0
                   : op == "<=" ? c > 0
                   : op == ">"  ? c <= 0
                   : op == "<"  ? c >= 0
                                : c != 0)
            return 0;
    }
    return 1;
}
static int requirement(const Json &r, const std::string &version) {
    if (r.is_string())
        return version_matches(r.get<std::string>(), version);
    if (!r.is_array())
        return -1;
    bool unknown = false;
    for (auto &v : r) {
        auto a = requirement(v, version);
        if (a == 1)
            return 1;
        unknown |= a < 0;
    }
    return unknown ? -1 : 0;
}
static std::string toml_field(const std::string &s, const std::string &key) {
    std::smatch m;
    return std::regex_search(s, m,
                             std::regex("(?:^|\\n)\\s*" + key + "\\s*=\\s*[\"']([^\"']*)[\"']"))
               ? m[1].str()
               : "";
}
Json inspect_mod_bytes(const std::string &bytes, const std::string &file,
                       const std::string &preferred, int depth) {
    Json result{{"filename", file},
                {"name", file},
                {"enabled", file.size() < 9 || file.substr(file.size() - 9) != ".disabled"},
                {"known", false},
                {"valid_archive", false},
                {"sha512", hash_bytes(bytes)},
                {"metadata", Json::object()},
                {"problem", ""}};
    try {
        if (depth > 4)
            throw Error("mods", "nested_limit", "内嵌模组层级过深。");
        zip_entries(bytes);
        result["valid_archive"] = true;
        for (auto &entry : std::vector<std::pair<std::string, std::string>>{
                 {"fabric", "fabric.mod.json"},
                 {"quilt", "quilt.mod.json"},
                 {"forge", "META-INF/mods.toml"},
                 {"neoforge", "META-INF/neoforge.mods.toml"}}) {
            auto text = zip_entry_bytes(bytes, entry.second);
            if (text.empty())
                continue;
            auto loader = entry.first;
            Json m{{"loader", loader},
                   {"ids", Json::array()},
                   {"requirements", Json::object()},
                   {"breaks", Json::object()},
                   {"conflicts", Json::object()},
                   {"components", Json::array()},
                   {"provided_versions", Json::object()},
                   {"embedded_hashes", Json::object()},
                   {"icon_path", ""}};
            if (loader == "fabric" || loader == "quilt") {
                auto doc = Json::parse(text);
                auto data = loader == "quilt" ? doc.at("quilt_loader") : doc;
                m["id"] = data.value("id", "");
                m["version"] = data.value("version", "");
                m["name"] =
                    loader == "quilt"
                        ? data.value("metadata", Json::object()).value("name", data.value("id", ""))
                        : data.value("name", data.value("id", ""));
                auto icon = loader == "quilt"
                                ? data.value("metadata", Json::object()).value("icon", Json())
                                : data.value("icon", Json());
                if (icon.is_string())
                    m["icon_path"] = icon;
                else if (icon.is_object() && !icon.empty())
                    m["icon_path"] = icon.begin().value();
                if (loader == "quilt") {
                    for (auto &dep : data.value("depends", Json::array()))
                        if (dep.is_object() && !dep.value("optional", false))
                            m["requirements"][dep.value("id", "")] =
                                dep.value("versions", Json("*"));
                } else
                    m["requirements"] = data.value("depends", Json::object());
                m["breaks"] = data.value("breaks", Json::object());
                m["conflicts"] = data.value("conflicts", Json::object());
                for (auto &provided : data.value("provides", Json::array()))
                    if (provided.is_string())
                        m["ids"].push_back(provided);
                m["ids"].push_back(m["id"]);
                for (auto &i : m["ids"])
                    m["provided_versions"][i.get<std::string>()] = m["version"];
                auto jars = doc.value("jars", data.value("jars", Json::array()));
                if (jars.size() > 128)
                    throw Error("mods", "nested_limit", "内嵌模组数量过多。");
                for (auto &jar : jars) {
                    auto name = jar.is_string() ? jar.get<std::string>() : jar.value("file", "");
                    auto child =
                        inspect_mod_bytes(zip_entry_bytes(bytes, name), name, loader, depth + 1);
                    if (!child.value("known", false))
                        throw Error("mods", "nested_unknown", "内嵌模组元数据无法识别。");
                    m["components"].push_back(child);
                    for (auto &i : child.at("ids")) {
                        m["ids"].push_back(i);
                        m["embedded_hashes"][i.get<std::string>()] = child.at("sha512");
                        m["provided_versions"][i.get<std::string>()] =
                            child.value("provided_versions", Json::object())
                                .value(i.get<std::string>(), child.value("version", ""));
                    }
                }
            } else {
                if (loader == "forge" &&
                    std::regex_search(text, std::regex("modId\\s*=\\s*\"neoforge\"")))
                    loader = "neoforge";
                m["loader"] = loader;
                std::regex blocks(R"(\[\[mods\]\]([\s\S]*?)(?=\[\[|$))");
                for (std::sregex_iterator it(text.begin(), text.end(), blocks), end; it != end;
                     ++it) {
                    auto b = (*it)[1].str();
                    auto name = toml_field(b, "modId");
                    if (name.empty())
                        continue;
                    auto version = toml_field(b, "version");
                    m["ids"].push_back(name);
                    m["provided_versions"][name] = version;
                    if (!m.contains("id")) {
                        m["id"] = name;
                        m["version"] = version;
                        m["name"] = toml_field(b, "displayName");
                        m["icon_path"] = toml_field(b, "logoFile");
                    }
                }
                std::regex deps(R"(\[\[dependencies\.([^\]]+)\]\]([\s\S]*?)(?=\[\[|$))");
                for (std::sregex_iterator it(text.begin(), text.end(), deps), end; it != end;
                     ++it) {
                    auto b = (*it)[2].str(), name = toml_field(b, "modId"),
                         range = toml_field(b, "versionRange"), type = toml_field(b, "type");
                    if (name.empty() || type == "optional" ||
                        std::regex_search(b, std::regex(R"(mandatory\s*=\s*false)")))
                        continue;
                    auto key = type == "incompatible"  ? "breaks"
                               : type == "discouraged" ? "conflicts"
                                                       : "requirements";
                    m[key][name] = range.empty() ? "*" : range;
                }
            }
            if (m.value("id", "").empty() || m.value("version", "").empty())
                throw Error("mods", "invalid_metadata", "模组缺少 ID 或版本。");
            if (m.value("name", "").empty())
                m["name"] = m["id"];
            m["known"] = true;
            result["metadata"][loader] = m;
        }
        auto metadata = result["metadata"];
        std::string chosen = metadata.contains(preferred) ? preferred
                             : metadata.empty()           ? ""
                                                          : metadata.begin().key();
        if (!chosen.empty()) {
            for (auto &entry : metadata[chosen].items())
                result[entry.key()] = entry.value();
            result["known"] = true;
        } else {
            auto transformer = zip_entry_bytes(bytes, "optifine/OptiFineClassTransformer.class");
            if (transformer.size() > 4 &&
                transformer.substr(0, 4) == std::string("\xca\xfe\xba\xbe", 4)) {
                result["id"] = "optifine";
                result["ids"] = Json::array({"optifine"});
                result["name"] = "OptiFine";
                result["loader"] = "optifine";
                result["version"] = "unknown";
                result["known"] = true;
            } else
                result["problem"] = "无法识别模组元数据，需手动确认兼容性。";
        }
    } catch (const std::exception &e) {
        result["known"] = false;
        result["problem"] = e.what();
    }
    return result;
}
Json inspect_mod(const fs::path &p, const std::string &loader) {
    return inspect_mod_bytes(read_file(p, 256 * 1024 * 1024), p.filename().u8string(), loader);
}
Json mod_inventory(const fs::path &game) {
    auto current = game / "mods/.solar-mods.json";
    if (fs::exists(current))
        return read_json(current);
    auto old = read_json(game / "mods/.m2cl-mods.json");
    Json out = Json::object();
    for (auto &v : old.value("files", Json::array())) {
        auto name = v.value("fileName", "");
        if (!name.empty())
            out[name] = v;
    }
    return out;
}
Json local_mods(const fs::path &game, const std::string &loader) {
    Json out = Json::array();
    auto dir = game / "mods";
    if (!fs::is_directory(dir))
        return out;
    auto manifest = mod_inventory(game);
    for (auto &entry : fs::directory_iterator(dir)) {
        if (entry.is_symlink() || !entry.is_regular_file())
            continue;
        auto file = entry.path().filename().u8string();
        if (file.find(".jar") == file.npos && file.find(".zip") == file.npos &&
            file.find(".litemod") == file.npos)
            continue;
        auto mod = inspect_mod(entry.path(), loader);
        auto key = file.size() > 9 && file.substr(file.size() - 9) == ".disabled"
                       ? file.substr(0, file.size() - 9)
                       : file;
        auto record = manifest.value(key, manifest.value(file, Json::object()));
        if (record.value("sha512", "") == mod.value("sha512", "")) {
            mod["project_id"] = record.value("projectId", "");
            mod["version_id"] = record.value("versionId", "");
        }
        out.push_back(mod);
    }
    return out;
}
Json check_mods(const Json &i, const Json &mods, bool allow_unknown, bool allow_missing) {
    Json errors = Json::array(), warnings = Json::array();
    auto loader = i.value("loader", "vanilla");
    std::map<std::string, std::string> versions{{"minecraft", i.value("version", "")},
                                                {"java", std::to_string(i.value("java_major", 0))}};
    std::map<std::string, std::string> runtime{{"fabricloader", "fabric"},
                                               {"quilt_loader", "quilt"},
                                               {"forge", "forge"},
                                               {"neoforge", "neoforge"}};
    for (auto &r : runtime)
        if (r.second == loader)
            versions[r.first] = i.value("loader_version", "");
    std::map<std::string, std::string> seen;
    std::set<std::string> ids;
    for (auto &m : mods)
        if (m.value("enabled", true)) {
            if (!m.value("known", false)) {
                (allow_unknown ? warnings : errors)
                    .push_back(m.value("name", "未知文件") + "：" +
                               m.value("problem", "元数据未知"));
                continue;
            }
            auto actual = m.value("loader", "");
            if (actual != loader && !(loader == "quilt" && actual == "fabric") &&
                actual != "optifine")
                errors.push_back(m.value("name", "") + "：加载器不匹配（" + actual + " / " +
                                 loader + "）。");
            if (actual == "optifine" &&
                !(loader == "forge" &&
                  i.value("optifine", Json::object()).value("sha512", "") == m.value("sha512", "")))
                errors.push_back("OptiFine 必须由版本组件安装流程验证，不能作为普通模组直接启用。");
            for (auto &id : m.value("ids", Json::array())) {
                auto key = id.get<std::string>();
                if (runtime.count(key) || key == "minecraft" || key == "java")
                    errors.push_back("加载器与 Java 是实例版本组件，不能由 mods 文件替代：" + key);
                else if (seen.count(key))
                    errors.push_back("重复模组 ID：" + key + "（" + seen[key] + " / " +
                                     m.value("filename", "") + "）。");
                seen[key] = m.value("filename", "");
                ids.insert(key);
                versions[key] =
                    m.value("provided_versions", Json::object()).value(key, m.value("version", ""));
            }
        }
    std::function<void(const Json &)> check = [&](const Json &m) {
        auto requirements = m.value("requirements", Json::object());
        for (auto &dep : requirements.items()) {
            if (dep.key() == "fabricloader" && loader == "quilt")
                continue;
            if (!versions.count(dep.key())) {
                if (!allow_missing)
                    errors.push_back(m.value("name", "") + " 缺少前置：" + dep.key());
            } else {
                auto match = requirement(dep.value(), versions[dep.key()]);
                if (match == 0)
                    errors.push_back(m.value("name", "") + " 前置版本不符合：" + dep.key());
                if (match < 0)
                    warnings.push_back("无法完整比较版本范围：" + dep.key());
            }
        }
        for (const auto &key : {"breaks", "conflicts"}) {
            auto rules = m.value(key, Json::object());
            for (auto &dep : rules.items())
                if (versions.count(dep.key()) && requirement(dep.value(), versions[dep.key()]) == 1)
                    (std::string(key) == "breaks" ? errors : warnings)
                        .push_back(m.value("name", "") + " 与 " + dep.key() + " 不兼容。");
        }
        for (auto &child : m.value("components", Json::array()))
            check(child);
    };
    for (auto &m : mods)
        if (m.value("enabled", true) && m.value("known", false))
            check(m);
    for (auto &pair : std::vector<std::pair<std::string, std::string>>{
             {"iris", "optifine"}, {"iris", "optifabric"}, {"sodium", "optifine"}})
        if (ids.count(pair.first) && ids.count(pair.second))
            errors.push_back(pair.first + " 与 " + pair.second + " 不能同时启用。");
    if (loader == "quilt") {
        auto list = compatibility_rules(fs::u8path(i.value("game_directory", "")));
        for (auto &r : list.value("rules", Json::array()))
            for (auto &id : r.value("ids", Json::array()))
                if (ids.count(id.get<std::string>()))
                    warnings.push_back("Quilt 官方兼容性提醒：" + r.value("name", "") +
                                       "。未提供精确文件哈希，需核对具体构建。");
    }
    return {{"errors", errors}, {"warnings", warnings}};
}
static void filename_ok(const std::string &s) {
    if (s.empty() || s == "." || s == ".." || s.find_first_of("/\\:") != s.npos)
        throw Error("mods", "invalid_filename", "模组文件名无效。");
}
void import_mods(const Json &i, const std::vector<fs::path> &paths, bool allow_unknown) {
    auto game = fs::u8path(i.at("game_directory").get<std::string>());
    detail::Lock lock(game / "mods");
    std::map<std::string, std::string> expected;
    auto current = local_mods(game, i.value("loader", ""));
    std::vector<std::pair<fs::path, fs::path>> copies;
    for (auto &source : paths) {
        auto name = source.filename().u8string();
        filename_ok(name);
        auto target = checked_path(game, "mods/" + name);
        auto mod = inspect_mod(source, i.value("loader", ""));
        if (fs::exists(target)) {
            if (hash_file(target) == mod.value("sha512", ""))
                continue;
            throw Error("mods", "duplicate_filename", "已有同名但内容不同的模组。");
        }
        expected[target.u8string()] = mod.at("sha512");
        current.push_back(mod);
        copies.push_back({source, target});
    }
    auto check = check_mods(i, current, allow_unknown, false);
    if (!check["errors"].empty())
        throw Error("mods", "incompatible", check["errors"].dump());
    std::vector<fs::path> created;
    try {
        for (auto &copy : copies) {
            created.push_back(copy.second);
            atomic_write(copy.second, read_file(copy.first, 256 * 1024 * 1024));
            if (expected.at(copy.second.u8string()) != hash_file(copy.second))
                throw Error("mods", "source_changed", "复制过程中原文件发生变化。");
        }
    } catch (...) {
        for (auto &p : created) {
            std::error_code ec;
            fs::remove(p, ec);
        }
        throw;
    }
}
static void check_mutation(const Json &i, const Json &all, const std::vector<std::string> &names) {
    auto component = i.value("optifine", Json::object()).value("modFile", "");
    std::set<std::string> ids, projects, versions;
    for (auto &name : names) {
        filename_ok(name);
        if (!component.empty() && (name == component || name == component + ".disabled"))
            throw Error("mods", "version_component",
                        "OptiFine 属于版本组件，请通过版本组件接口修改。");
        auto found = std::find_if(all.begin(), all.end(),
                                  [&](const Json &m) { return m.value("filename", "") == name; });
        if (found == all.end())
            throw Error("mods", "file_changed", "模组文件已变化，请刷新列表。");
        for (auto &id : found->value("ids", Json::array()))
            ids.insert(id.get<std::string>());
        projects.insert(found->value("project_id", ""));
        versions.insert(found->value("version_id", ""));
    }
    auto inventory = mod_inventory(fs::u8path(i.at("game_directory").get<std::string>()));
    for (auto &m : all)
        if (m.value("enabled", true) &&
            std::find(names.begin(), names.end(), m.value("filename", "")) == names.end()) {
            std::function<void(const Json &)> check = [&](const Json &part) {
                auto requirements = part.value("requirements", Json::object());
                for (auto &e : requirements.items())
                    if (ids.count(e.key()))
                        throw Error("mods", "required_by_other",
                                    m.value("name", "") +
                                        " 仍依赖所选模组，请一起选择或先禁用依赖者。");
                for (auto &c : part.value("components", Json::array()))
                    check(c);
            };
            check(m);
            auto name = m.value("filename", "");
            auto record = inventory.value(name, Json::object());
            for (auto &d : record.value("dependencies", Json::array()))
                if (d.value("type", d.value("dependency_type", "")) == "required") {
                    auto project = d.value("projectId", d.value("project_id", "")),
                         version = d.value("versionId", d.value("version_id", ""));
                    if ((!project.empty() && projects.count(project)) ||
                        (!version.empty() && versions.count(version)))
                        throw Error("mods", "required_by_other", "其他已启用模组仍依赖所选项目。");
                }
        }
}
void set_mods_enabled(const Json &i, const std::vector<std::string> &names, bool enabled) {
    auto game = fs::u8path(i.at("game_directory").get<std::string>());
    detail::Lock lock(game / "mods");
    auto all = local_mods(game, i.value("loader", ""));
    if (!enabled)
        check_mutation(i, all, names);
    else {
        auto component = i.value("optifine", Json::object()).value("modFile", "");
        for (auto &name : names)
            if (!component.empty() && (name == component || name == component + ".disabled"))
                throw Error("mods", "version_component", "OptiFine 属于版本组件。");
    }
    for (auto &m : all)
        if (std::find(names.begin(), names.end(), m.value("filename", "")) != names.end())
            m["enabled"] = enabled;
    auto check = check_mods(i, all, true, false);
    if (enabled && !check["errors"].empty())
        throw Error("mods", "incompatible", check["errors"].dump());
    std::vector<std::pair<fs::path, fs::path>> done;
    try {
        for (auto &name : names) {
            filename_ok(name);
            bool off = name.size() > 9 && name.substr(name.size() - 9) == ".disabled";
            if (off == !enabled)
                continue;
            auto target = enabled ? name.substr(0, name.size() - 9) : name + ".disabled";
            auto from = checked_path(game, "mods/" + name),
                 to = checked_path(game, "mods/" + target);
            if (fs::exists(to))
                throw Error("mods", "target_exists", "启停目标文件已存在。");
            fs::rename(from, to);
            done.push_back({from, to});
        }
    } catch (...) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code ec;
            fs::rename(it->second, it->first, ec);
        }
        throw;
    }
}
void remove_mods(const Json &i, const std::vector<std::string> &names) {
    auto game = fs::u8path(i.at("game_directory").get<std::string>());
    detail::Lock lock(game / "mods");
    check_mutation(i, local_mods(game, i.value("loader", "")), names);
    for (auto &name : names) {
        filename_ok(name);
        auto from = checked_path(game, "mods/" + name);
        move_to_trash(from);
    }
}
} // namespace solar::engine
