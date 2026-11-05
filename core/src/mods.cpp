// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include "solar/engine.hpp"
#include "mod_scan_cache.hpp"
#include <regex>
#include <set>
#include <sstream>
namespace solar::engine {
Json inspect_mod(const fs::path &p, const std::string &loader) {
    return scan_mod_file(p, loader, false);
}
Json mod_inventory(const fs::path &game) {
    auto current = game / "mods/.solar-mods.json";
    if (fs::exists(current)) {
        auto result = read_json(current);
        if (!result.is_object()) throw Error("mods", "invalid_manifest", "模组来源索引格式异常，已保留原文件。");
        return result;
    }
    auto old = read_json(game / "mods/.m2cl-mods.json");
    if (!old.is_object() || (old.contains("files") && !old.at("files").is_array()))
        throw Error("mods", "invalid_manifest", "模组来源索引格式异常，已保留原文件。");
    Json out = Json::object();
    for (auto &v : old.value("files", Json::array())) {
        auto name = v.value("fileName", "");
        if (!name.empty())
            out[name] = v;
    }
    return out;
}
Json local_mods(const fs::path &game, const std::string &loader, bool use_cache, bool tolerate_errors) {
    Json out = Json::array();
    auto dir = game / "mods";
    if (!fs::is_directory(dir))
        return out;
    Json manifest;
    bool bad_manifest = false;
    try { manifest = mod_inventory(game); }
    catch (...) {
        if (!tolerate_errors) throw;
        manifest = Json::object(); bad_manifest = true;
    }
    for (auto &entry : fs::directory_iterator(dir)) {
        std::error_code status_error;
        auto status = entry.symlink_status(status_error);
        if (status_error) {
            if (!tolerate_errors) throw Error("mods", "file_unreadable", "模组文件无法读取，请检查文件后刷新。");
            continue;
        }
        if (fs::is_symlink(status) || !fs::is_regular_file(status))
            continue;
        auto file = entry.path().filename().u8string();
        if (file.find(".jar") == file.npos && file.find(".zip") == file.npos &&
            file.find(".litemod") == file.npos)
            continue;
        Json mod;
        try { mod = scan_mod_file(entry.path(), loader, use_cache); }
        catch (const std::exception &) {
            if (!tolerate_errors) throw;
            mod = {{"filename",file},{"name",file},{"known",false},{"valid_archive",false},
                {"sha512",""},{"enabled", file.size()<9 || file.substr(file.size()-9)!=".disabled"},
                {"problem","模组文件无法读取，请检查文件后刷新。"}};
        }
        auto key = file.size() > 9 && file.substr(file.size() - 9) == ".disabled"
                       ? file.substr(0, file.size() - 9)
                       : file;
        auto record = manifest.is_object() ? manifest.value(key, manifest.value(file, Json::object())) : Json::object();
        bool valid = record.is_object();
        if (valid) for (auto field : {"sha512","projectId","versionId","title","sourceStatus"})
            if (record.contains(field) && !record.at(field).is_string()) valid = false;
        if (valid && record.contains("icon_url") && !record.at("icon_url").is_null() && !record.at("icon_url").is_string()) valid = false;
        if (!valid) {
            if (!tolerate_errors)
                throw Error("mods", "invalid_manifest", "模组来源索引格式异常，已保留原文件。");
            record = Json::object();
            mod["source_problem"] = "模组来源索引格式异常，已保留原文件。";
        }
        if (bad_manifest) mod["source_problem"] = "模组来源索引格式异常，已保留原文件。";
        mod["source_status"] = "not_checked";
        if (!mod.value("sha512", "").empty() && record.value("sha512", "") == mod.value("sha512", "")) {
            mod["project_id"] = record.value("projectId", "");
            mod["version_id"] = record.value("versionId", "");
            mod["icon_url"] = record.value("icon_url",Json());
            if(!record.value("title","").empty()) mod["name"]=record.at("title");
            mod["source_status"] = !record.value("projectId", "").empty() && !record.value("versionId", "").empty()
                ? "matched" : record.value("sourceStatus", "not_checked");
        }
        out.push_back(mod);
    }
    return out;
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
