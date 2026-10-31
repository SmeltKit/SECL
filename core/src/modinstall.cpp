// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <set>
#include <unordered_map>
namespace solar::engine {
static bool contains(const Json &a, const std::string &s) {
    return a.is_array() && std::find(a.begin(), a.end(), s) != a.end();
}
static std::string dependency_id(const Json &dependency, const char *key) {
    auto value = dependency.find(key);
    return value != dependency.end() && value->is_string() ? value->get<std::string>() : "";
}
Json plan_mod_install(const Json &instance, const Json &root, const Task &task) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto mods = local_mods(game, instance.value("loader", ""));
    Json known = Json::object(), hashes = Json::array(), expected = Json::object();
    for (auto &m : mods) {
        expected[m.at("filename").get<std::string>()] = m["sha512"];
        hashes.push_back(m["sha512"]);
        auto embedded = m.value("embedded_hashes", Json::object());
        for (auto &e : embedded.items())
            hashes.push_back(e.value());
        if (m.contains("project_id") && !m.value("project_id", "").empty())
            known[m["sha512"].get<std::string>()] = {{"project_id", m["project_id"]},
                                                     {"id", m.value("version_id", "")}};
    }
    if (!hashes.empty()) {
        task.check();
        auto response = https_request("https://api.modrinth.com/v2/version_files",
                                      Json{{"hashes", hashes}, {"algorithm", "sha512"}}.dump(), "",
                                      "application/json");
        if (response.status == 200) {
            auto identities = Json::parse(response.body, nullptr, false);
            if (identities.is_object())
                for (auto &v : identities.items())
                    known[v.key()] = v.value();
        }
    }
    std::map<std::string, Json> existing;
    for (auto &m : mods) {
        auto hash = m.value("sha512", "");
        if (known.contains(hash)) {
            auto identity = known[hash];
            existing[identity.value("project_id", "")] = {{"filename", m["filename"]},
                                                          {"sha512", hash},
                                                          {"version_id", identity.value("id", "")},
                                                          {"enabled", m.value("enabled", true)},
                                                          {"embedded", false}};
        }
        auto embedded = m.value("embedded_hashes", Json::object());
        for (auto &e : embedded.items())
            if (known.contains(e.value().get<std::string>())) {
                auto identity = known[e.value().get<std::string>()];
                existing[identity.value("project_id", "")] = {
                    {"filename", m["filename"]},
                    {"sha512", e.value()},
                    {"version_id", identity.value("id", "")},
                    {"enabled", m.value("enabled", true)},
                    {"embedded", true}};
            }
    }
    Json plan{{"target", instance},
              {"files", Json::array()},
              {"reuse", Json::array()},
              {"edges", Json::array()},
              {"optional", Json::array()},
              {"skipped", Json::array()},
              {"disable_files", Json::array()},
              {"expected_files", expected},
              {"root_project", root.value("project_id", "")},
              {"original_manifest", mod_inventory(game)}};
    std::map<std::string, std::string> chosen;
    std::set<std::string> visiting;
    std::function<void(Json, std::string, bool, bool, int)> resolve = [&](Json file, std::string from,
                                                                    bool optional, bool direct_optional, int depth) {
        task.check();
        if (depth > 24 || chosen.size() > 128)
            throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
        auto project = file.value("project_id", "");
        if (project.empty())
            throw Error("dependencies", "missing_project", "资源缺少项目标识。");
        if (!resource_compatible(file, instance.value("version", ""), instance.value("loader", "")))
            throw Error("dependencies", "incompatible_file",
                        "模组文件与目标游戏版本或加载器不匹配。");
        if (chosen.count(project)) {
            if (chosen[project] != file.value("version_id", ""))
                throw Error("dependencies", "version_conflict", "同一依赖被要求安装不同版本。");
            return;
        }
        chosen[project] = file.value("version_id", "");
        if (!from.empty())
            plan["edges"].push_back({{"from", from}, {"to", project}, {"optional", direct_optional}});
        file["optional"] = optional;
        file["root"] = from.empty();
        if (existing.count(project) &&
            existing[project].value("version_id", "") == file.value("version_id", "")) {
            auto reuse = existing[project];
            reuse["project_id"] = project;
            reuse["file"] = file;
            plan["reuse"].push_back(reuse);
        } else {
            file["replaces"] = existing.count(project) ? existing[project] : Json();
            plan["files"].push_back(file);
        }
        for (auto &dep : file.value("dependencies", Json::array())) {
            auto type = dep.value("dependency_type", "required");
            if (type == "incompatible") {
                auto p = dependency_id(dep, "project_id");
                if (existing.count(p) && existing[p].value("enabled", true)) {
                    auto name = existing[p].value("filename", "");
                    plan["conflicts"].push_back({{"filename", name}, {"reason", "作者声明不兼容"}});
                }
                continue;
            }
            if (type != "required" && type != "optional")
                continue;
            auto dep_project = dependency_id(dep, "project_id");
            auto version = dependency_id(dep, "version_id");
            if (dep_project.empty() && version.empty())
                continue;
            Json candidate;
            if (!version.empty()) {
                auto raw =
                    fetch_json("https://api.modrinth.com/v2/version/" + url_encode(version), task);
                auto files = resource_files(Json::array({raw}), false);
                if (files.empty())
                    throw Error("dependencies", "missing_file", "依赖版本没有可安装文件。");
                candidate = files.front();
                if (!dep_project.empty() && candidate.value("project_id", "") != dep_project)
                    throw Error("dependencies", "project_mismatch", "依赖版本与项目不一致。");
            } else {
                auto files = resource_versions(dep_project, instance.value("version", ""),
                                               instance.value("loader", ""), task);
                if (files.empty() && instance.value("loader", "") == "quilt")
                    files = resource_versions(dep_project, instance.value("version", ""), "fabric",
                                              task);
                if (files.empty()) {
                    if (type == "optional") {
                        plan["optional_errors"][dep_project] = "没有匹配文件";
                        continue;
                    }
                    throw Error("dependencies", "missing_compatible_version",
                                "没有找到适配的前置模组文件。");
                }
                auto stable = std::find_if(files.begin(), files.end(), [](const Json &f) {
                    return f.value("release_type", "") == "release";
                });
                candidate = stable == files.end() ? files.front() : *stable;
            }
            auto p = candidate.value("project_id", "");
            if (type == "optional")
                plan["optional"].push_back(p);
            resolve(candidate, project, optional || type == "optional", type == "optional", depth + 1);
        }
    };
    resolve(root, "", false, false, 0);
    return plan;
}
Json select_dependencies(const Json &original, const Json &selected, const Json &redownload,
                         const Json &enable) {
    auto plan = original;
    Json files = Json::array(), reuse = Json::array(), skipped = Json::array(),
         enabled = Json::array();
    std::set<std::string> needed{plan.value("root_project", "")};
    for (auto &p : selected)
        needed.insert(p.get<std::string>());
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto &e : plan.value("edges", Json::array()))
            if (needed.count(e.value("from", "")) && !e.value("optional", false) &&
                contains(selected, e.value("to", "")))
                changed |= needed.insert(e.value("to", "")).second;
    }
    for (auto &f : plan.value("files", Json::array())) {
        auto id = f.value("project_id", "");
        if (needed.count(id))
            files.push_back(f);
        else
            skipped.push_back(id);
    }
    for (auto &r : plan.value("reuse", Json::array())) {
        auto id = r.value("project_id", "");
        if (!needed.count(id)) {
            skipped.push_back(id);
            continue;
        }
        if (contains(redownload, id)) {
            if (r.value("embedded", false))
                throw Error("dependencies", "embedded_redownload",
                            "内嵌依赖不能单独覆盖，需更新其所属模组。");
            auto file = r.at("file");
            file["replaces"] = r;
            files.push_back(file);
        } else {
            reuse.push_back(r);
            if (!r.value("enabled", true) && contains(enable, id))
                enabled.push_back(r.at("filename"));
        }
    }
    plan["files"] = files;
    plan["reuse"] = reuse;
    plan["skipped"] = skipped;
    plan["enable_files"] = enabled;
    return plan;
}
Json apply_mod_plan(const Json &plan, const Task &task) {
    for (const auto &conflict : plan.value("conflicts", Json::array()))
        if (!contains(plan.value("disable_files", Json::array()), conflict.value("filename", "")))
            throw Error("mods", "unresolved_conflict", "请先确认停用作者声明不兼容的组件。");
    auto i = plan.at("target");
    auto game = fs::u8path(i.at("game_directory").get<std::string>());
    auto folder = checked_path(game, "mods");
    fs::create_directories(folder);
    auto lock = folder / ".solar-install-lock";
    if (!fs::create_directory(lock))
        throw Error("mods", "busy", "模组目录正在被其他安装操作使用。");
    struct Lock {
        fs::path p;
        ~Lock() {
            std::error_code e;
            fs::remove(p, e);
        }
    } guard{lock};
    auto expected = plan.value("expected_files", Json::object());
    for (auto &e : expected.items()) {
        auto path = checked_path(folder, e.key());
        if (!fs::is_regular_file(path) || hash_file(path) != e.value())
            throw Error("mods", "plan_stale", "确认后模组目录发生变化，请重新生成安装计划。");
    }
    if (mod_inventory(game) != plan.value("original_manifest", Json::object()))
        throw Error("mods", "manifest_changed", "模组索引发生变化，请重新确认。");
    auto stage = checked_path(game, ".solar-mod-stage/" + random_url_token());
    fs::create_directories(stage);
    std::vector<std::pair<fs::path, fs::path>> moved;
    std::vector<fs::path> added;
    Json installed = Json::array();
    try {
        auto mods = local_mods(game, i.value("loader", ""));
        for (auto &f : plan.value("files", Json::array())) {
            task.check();
            auto name = f.value("filename", "");
            if (name.find('/') != name.npos)
                throw Error("mods", "unsafe_filename", "模组文件名无效。");
            auto target = checked_path(stage, name);
            download(f, target, task);
            auto metadata = inspect_mod(target, i.value("loader", ""));
            if (!metadata.value("known", false))
                throw Error("mods", "unknown_metadata", "下载的模组缺少可确认的元数据。");
            auto replacement = f.value("replaces", Json());
            if (replacement.is_object()) {
                auto old = replacement.value("filename", "");
                mods.erase(
                    std::remove_if(mods.begin(), mods.end(),
                                   [&](const Json &m) { return m.value("filename", "") == old; }),
                    mods.end());
            }
            mods.push_back(metadata);
        }
        auto disabled = plan.value("disable_files", Json::array());
        for (auto &m : mods)
            if (contains(disabled, m.value("filename", "")))
                m["enabled"] = false;
        for (auto &mod : mods)
            if (contains(plan.value("enable_files", Json::array()), mod.value("filename", "")))
                mod["enabled"] = true;
        auto check = check_mods(i, mods, false, !plan.value("skipped", Json::array()).empty());
        if (!check["errors"].empty())
            throw Error("mods", "conflict", check["errors"].dump());
        auto manifest = plan.value("original_manifest", Json::object());
        for (auto &f : plan.value("files", Json::array())) {
            auto name = f.value("filename", "");
            auto replacement = f.value("replaces", Json());
            if (replacement.is_object() && !replacement.value("embedded", false)) {
                auto old = replacement.value("filename", "");
                auto source = checked_path(folder, old),
                     backup = checked_path(stage, "backup/" + old);
                if (!fs::exists(source) || hash_file(source) != replacement.value("sha512", ""))
                    throw Error("mods", "replacement_changed", "待替换文件发生变化。");
                fs::create_directories(backup.parent_path());
                fs::rename(source, backup);
                moved.push_back({source, backup});
                manifest.erase(old);
            }
            auto dest = checked_path(folder, name);
            if (fs::exists(dest))
                throw Error("mods", "target_exists", "目标模组文件已存在，不能静默覆盖。");
            fs::rename(checked_path(stage, name), dest);
            added.push_back(dest);
            manifest[name] = {{"projectId", f.value("project_id", "")},
                              {"versionId", f.value("version_id", "")},
                              {"sha512", f.value("sha512", "")}};
            installed.push_back(name);
        }
        for (auto &n : disabled) {
            auto name = n.get<std::string>();
            auto from = checked_path(folder, name), to = checked_path(folder, name + ".disabled");
            if (fs::exists(to))
                throw Error("mods", "target_exists", "停用目标已存在。");
            fs::rename(from, to);
            moved.push_back({from, to});
        }
        for (auto &n : plan.value("enable_files", Json::array())) {
            auto name = n.get<std::string>();
            if (name.size() < 9 || name.substr(name.size() - 9) != ".disabled")
                throw Error("mods", "invalid_filename", "启用计划的文件名无效。");
            auto from = checked_path(folder, name),
                 to = checked_path(folder, name.substr(0, name.size() - 9));
            if (fs::exists(to))
                throw Error("mods", "target_exists", "启用目标已存在。");
            fs::rename(from, to);
            moved.push_back({from, to});
        }
        atomic_write(game / "mods/.solar-mods.json", manifest.dump(2));
        std::error_code cleanup;
        fs::remove_all(stage, cleanup);
        if (!plan.value("skipped", Json::array()).empty())
            check["warnings"].push_back("用户取消选择了部分前置，游戏可能无法启动。");
        return {{"installed", installed}, {"warnings", check["warnings"]}};
    } catch (...) {
        for (auto &p : added) {
            std::error_code e;
            fs::remove(p, e);
        }
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            std::error_code e;
            if (fs::exists(it->second))
                fs::rename(it->second, it->first, e);
        }
        std::error_code e;
        fs::remove_all(stage, e);
        throw;
    }
}
} // namespace solar::engine
