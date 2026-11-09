// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <set>
#include <regex>

namespace solar::engine {
static std::string update_folder(const std::string &kind) {
    if (kind == "mod") return "mods";
    if (kind == "shader") return "shaderpacks";
    if (kind == "resourcepack") return "resourcepacks";
    throw Error("updates", "invalid_kind", "资源类型无效。");
}
static Json inventory(const Json &i, const std::string &kind) {
    auto game = fs::u8path(i.at("game_directory").get<std::string>());
    return kind == "mod" ? local_mods(game, i.value("loader", "")) : local_packs(game, kind);
}
static Json fingerprints(const Json &rows) {
    Json out = Json::object();
    for (auto &r : rows) out[r.at("filename").get<std::string>()] = r.value("sha512", "");
    return out;
}
static bool has(const Json &values, const std::string &s) {
    return values.is_array() && std::find(values.begin(), values.end(), s) != values.end();
}
static bool channel_allowed(const std::string &type, const std::string &channel) {
    if (channel != "release" && channel != "beta" && channel != "alpha")
        throw Error("updates", "invalid_channel", "更新频道无效。");
    return type == "release" || (channel != "release" && type == "beta") ||
           (channel == "alpha" && type == "alpha");
}

// Both identities are server results keyed by the local file's hash. Never
// associate a local file with a project by its display name or filename.
Json evaluate_resource_update(const Json &instance, const std::string &kind,
                              const std::string &channel, const Json &local,
                              const Json &current, const Json &candidate) {
    update_folder(kind); channel_allowed("release", channel);
    Json row = local;
    row["status"] = "unknown";
    auto finish = [&](const char *status, const char *message) {
        row["status"] = status; row["message"] = message; return row;
    };
    auto hash = local.value("sha512", "");
    if (local.value("directory", false))
        return finish("unsupported", "文件夹资源暂不支持自动更新。");
    if (hash.empty() || !current.is_object() || current.value("project_id", "").empty())
        return finish("unknown", "网站未识别此文件，无法确认更新。");
    bool hash_matches = false;
    for (auto &f : current.value("files", Json::array()))
        hash_matches |= f.value("hashes", Json::object()).value("sha512", "") == hash;
    if (!hash_matches) return finish("unknown", "网站返回的文件身份无法校验。");
    row["project_id"] = current.at("project_id");
    row["current_version"] = current.value("version_number", local.value("version", ""));
    auto matches = [&](const Json &v) {
        if (!has(v.value("game_versions", Json::array()), instance.value("version", ""))) return false;
        return kind != "mod" || resource_compatible(v, instance.value("version", ""), instance.value("loader", ""));
    };
    // A Forge jar found in a Fabric instance is an environment problem, not a
    // request to silently switch the project's loader variant.
    if (!matches(current)) return finish("incompatible", "当前文件与实例环境不匹配，请先处理。");
    if (!candidate.is_object() || candidate.empty())
        return finish("no_candidate", "所选频道没有匹配此实例的版本。");
    if (candidate.value("project_id", "") != current.at("project_id") || !matches(candidate) ||
        !channel_allowed(candidate.value("version_type", ""), channel))
        return finish("unknown", "候选版本的项目、频道或环境无法校验。");
    if (candidate.value("id", "") == current.value("id", ""))
        return finish("current", "所选频道内暂无更新。");
    // Do not downgrade a locally installed beta when the user checks releases.
    auto before = current.value("date_published", ""), after = candidate.value("date_published", "");
    if (before.empty() || after.empty()) return finish("unknown", "发布时间未知，无法确认这是更新。");
    if (after <= before) return finish("current", "所选频道内暂无更新。");
    // Some versions include sources/javadoc or alternate artifacts before the
    // primary jar. Prefer the primary artifact; ambiguity needs manual review.
    auto normalized = candidate;
    Json artifacts = Json::array();
    for (auto &f : candidate.value("files", Json::array())) {
        auto ext = fs::u8path(f.value("filename", "")).extension();
        auto file_type = f.value("file_type", Json());
        if (ext != (kind == "mod" ? ".jar" : ".zip") ||
            file_type == "sources-jar" || file_type == "dev-jar" || file_type == "javadoc-jar") continue;
        if (f.value("primary", false)) { artifacts = Json::array({f}); break; }
        artifacts.push_back(f);
    }
    if (artifacts.size() != 1) return finish("unknown", "候选版本包含多个文件，请手动选择。");
    normalized["files"] = artifacts;
    auto files = resource_files(Json::array({normalized}), kind != "mod");
    if (files.empty()) return finish("unknown", "候选版本没有可校验的下载文件。");
    auto file = files.front();
    if (file.value("sha512", "") == hash) return finish("current", "所选频道内暂无更新。");
    if (fs::u8path(file.value("filename", "")).extension() != (kind == "mod" ? ".jar" : ".zip"))
        return finish("unsupported", "候选文件格式暂不支持自动更新。");
    file["title"] = local.value("name", candidate.value("name", ""));
    file["icon_url"] = local.value("icon_url", Json());
    row["candidate"] = file;
    return finish("update", "仅通过网站环境筛选，仍需检查依赖和实际兼容性。");
}

static Json post(const std::string &endpoint, const Json &body, const Task &task, const Transport &transport) {
    task.check();
    auto url = "https://api.modrinth.com/v2/" + endpoint;
    if (!transport) return post_json(url, body, task);
    auto response = transport(url, body.dump(), "", "application/json");
    task.check();
    if (response.status != 200)
        throw Error("updates", "lookup_failed", "更新查询失败，请检查网络后重试。", response.status, url);
    auto result = Json::parse(response.body, nullptr, false);
    if (!result.is_object()) throw Error("updates", "invalid_response", "更新服务返回格式无效。");
    return result;
}
Json check_resource_updates(const Json &instance, const std::string &kind,
                            const std::string &channel, const Task &task, const Transport &transport) {
    update_folder(kind); channel_allowed("release", channel);
    auto locals = inventory(instance, kind);
    annotate_resource_locks(instance, kind, locals);
    Json rows = Json::array();
    auto component = instance.value("optifine", Json::object()).value("modFile", "");
    for (auto &local : locals) {
        auto row = local; row["status"] = "unknown";
        const auto name = row.value("filename", "");
        if (row.value("update_locked", false)) {
            row["status"] = "locked"; row["message"] = "此文件已锁定版本，检查更新时已跳过。";
        } else if (!component.empty() && (name == component || name == component + ".disabled")) {
            row["status"] = "unsupported"; row["message"] = "请在版本组件中更新 OptiFine。";
        } else if (row.value("directory", false) || row.value("sha512", "").empty()) {
            row["status"] = "unsupported"; row["message"] = "文件夹或无法读取的资源暂不支持自动更新。";
        }
        rows.push_back(row);
    }
    Json types = Json::array({"release"});
    if (channel != "release") types.push_back("beta");
    if (channel == "alpha") types.push_back("alpha");
    Json loaders = Json::array();
    if (kind == "mod") {
        loaders.push_back(instance.value("loader", ""));
        if (instance.value("loader", "") == "quilt") loaders.push_back("fabric");
    }
    // Bounded requests, no automatic retries. A failed chunk does not discard
    // successful chunks, nor turn an unqueried item into "up to date".
    for (size_t offset = 0; offset < rows.size(); offset += 64) {
        task.check(); task.report("正在检查资源更新", offset, rows.size());
        Json hashes = Json::array();
        auto end = std::min(offset + 64, rows.size());
        for (size_t n = offset; n < end; ++n)
            if (rows[n].at("status") == "unknown") hashes.push_back(rows[n].at("sha512"));
        if (hashes.empty()) continue;
        try {
            auto identities = post("version_files", {{"hashes", hashes}, {"algorithm", "sha512"}}, task, transport);
            auto updates = post("version_files/update", {{"hashes", hashes}, {"algorithm", "sha512"},
                {"loaders", loaders}, {"game_versions", Json::array({instance.value("version", "")})},
                {"version_types", types}}, task, transport);
            for (size_t n = offset; n < end; ++n) if (rows[n].at("status") == "unknown") {
                auto hash = rows[n].at("sha512").get<std::string>();
                try {
                    rows[n] = evaluate_resource_update(instance, kind, channel, rows[n],
                        identities.value(hash, Json()), updates.value(hash, Json()));
                } catch (const std::exception &) {
                    rows[n]["status"] = "unknown"; rows[n]["message"] = "此资源的更新信息格式异常。";
                }
            }
        } catch (const Error &e) {
            task.check();
            for (size_t n = offset; n < end; ++n) if (rows[n].at("status") == "unknown") {
                rows[n]["status"] = "failed"; rows[n]["message"] = e.what(); rows[n]["error"] = e.json();
            }
            // A rate limit/outage should not cause a request storm on large packs.
            for (size_t n = end; n < rows.size(); ++n) if (rows[n].at("status") == "unknown") {
                rows[n]["status"] = "failed"; rows[n]["message"] = "网络查询已停止，请稍后手动重试。";
                rows[n]["error"] = e.json();
            }
            break;
        }
    }
    // Multiple top-level copies of one project need manual resolution. Embedded
    // libraries are not top-level update targets.
    std::map<std::string, int> counts;
    for (auto &r : rows) if (!r.value("project_id", "").empty()) ++counts[r.at("project_id")];
    for (auto &r : rows) if (r.value("status", "") != "locked" && counts[r.value("project_id", "")] > 1) {
        r["status"] = "duplicate"; r["message"] = "检测到同一项目的多个副本，请先处理。";
    }
    return {{"target", instance}, {"kind", kind}, {"channel", channel}, {"items", rows},
            {"snapshot", fingerprints(locals)}};
}

static void assert_unchanged(const Json &plan) {
    if (fingerprints(inventory(plan.at("target"), plan.at("update_kind"))) != plan.at("update_snapshot"))
        throw Error("updates", "plan_stale", "资源文件已变化，请重新检查更新。");
}
static void assert_stopped(const Json &instance, const Task &task) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    if (!task.active_game_directory.empty() && fs::exists(game) &&
        fs::exists(fs::u8path(task.active_game_directory)) && fs::equivalent(game, fs::u8path(task.active_game_directory)))
        throw Error("updates", "game_running", "请先退出此实例的游戏，再更新资源。");
}
Json plan_resource_updates(const Json &report, const Json &selected, const Task &task) {
    const auto instance = report.at("target");
    const std::string kind = report.at("kind"); update_folder(kind);
    Json envelope{{"target", instance}, {"update_kind", kind}, {"update_snapshot", report.at("snapshot")}};
    assert_unchanged(envelope); assert_stopped(instance, task);
    if (!selected.is_array() || selected.empty() || selected.size() > 128)
        throw Error("updates", "invalid_selection", "请选择 1 至 128 项更新。");
    Json roots = Json::array(), replacements = Json::array();
    std::set<std::string> seen;
    for (auto &name : selected) {
        auto it = std::find_if(report.at("items").begin(), report.at("items").end(),
                               [&](const Json &r) { return r.at("filename") == name; });
        if (it == report.at("items").end() || it->value("status", "") != "update" ||
            !seen.insert(name.get<std::string>()).second)
            throw Error("updates", "invalid_selection", "更新选择已失效，请重新检查。");
        auto file = it->at("candidate");
        assert_resource_replacement_unlocked(instance, kind, *it, file);
        file["previous_version"] = it->value("current_version", it->value("version", ""));
        roots.push_back(file);
        replacements.push_back({{"filename", name}, {"sha512", it->at("sha512")},
                                {"enabled", it->value("enabled", true)}, {"file", file}});
    }
    Json plan;
    if (kind == "mod") {
        plan = plan_mod_installs(instance, roots, task, report.at("channel"));
        // Bind replacements to the checked hash, even if the identity service
        // temporarily fails during dependency planning.
        for (auto &replacement : replacements) {
            auto project = replacement.at("file").at("project_id"); bool bound = false;
            for (auto &file : plan["files"]) if (file.at("project_id") == project) {
                if (file.value("replaces", Json()).is_object() &&
                    file["replaces"].value("filename", "") != replacement.at("filename"))
                    throw Error("updates", "duplicate_project", "检测到同一项目的多个副本，请先处理。");
                file["replaces"] = replacement; file["replaces"].erase("file");
                file["install_disabled"] = !replacement.value("enabled", true); bound = true;
            }
            if (!bound) throw Error("updates", "plan_stale", "更新选择已失效，请重新检查。");
        }
    } else {
        for (auto &file : roots) plan_pack_install(instance, file, kind);
        plan = {{"target", instance}, {"kind", kind}, {"files", roots}, {"replacements", replacements}};
        if (kind == "shader") plan["support"] = shader_support(instance);
    }
    plan["update_kind"] = kind; plan["update_snapshot"] = report.at("snapshot");
    plan["channel"] = report.at("channel");
    assert_unchanged(plan);
    return plan;
}

Json apply_resource_updates(const Json &plan, const Task &task) {
    assert_stopped(plan.at("target"), task); assert_unchanged(plan);
    for (const auto &r : plan.value("replacements", Json::array()))
        assert_resource_replacement_unlocked(plan.at("target"), plan.at("update_kind"), r, r.at("file"));
    if (plan.at("update_kind") == "mod") {
        auto result = apply_mod_plan(plan, task); result["atomic_batch"] = true; return result;
    }
    auto kind = plan.at("update_kind").get<std::string>();
    auto game = fs::u8path(plan.at("target").at("game_directory").get<std::string>());
    auto folder = checked_path(game, update_folder(kind));
    auto stage = checked_path(game, ".solar-pack-stage/" + random_url_token());
    auto lock = checked_path(game, ".solar-resource-update-lock");
    if (!fs::create_directory(lock)) throw Error("updates", "busy", "已有资源更新操作正在进行。");
    struct Cleanup { fs::path p; ~Cleanup() { if (!p.empty()) { std::error_code ec; fs::remove_all(p, ec); } } } guard{lock}, clean{stage};
    std::vector<std::pair<fs::path, fs::path>> moved;
    std::vector<fs::path> added;
    Json installed = Json::array();
    try {
        std::set<std::string> names;
        for (auto &r : plan.at("replacements")) {
            auto f = r.at("file"); plan_pack_install(plan.at("target"), f, kind);
            auto name = f.at("filename").get<std::string>();
            if (!names.insert(name).second) throw Error("updates", "target_exists", "更新目标文件名重复。");
            auto dest = checked_path(folder, name);
            if (fs::exists(dest) && name != r.at("filename"))
                throw Error("updates", "target_exists", "更新目标已存在，请先处理重复文件。");
            auto payload = checked_path(stage, "payload/" + name);
            download(f, payload, task); inspect_pack(payload, kind);
        }
        task.check(); assert_stopped(plan.at("target"), task); assert_unchanged(plan);
        for (const auto &r : plan.at("replacements"))
            assert_resource_replacement_unlocked(plan.at("target"), kind, r, r.at("file"));
        // Write hash-addressed metadata before touching live payloads. An orphan
        // metadata record after failure cannot match any other file's content.
        for (auto &r : plan.at("replacements")) {
            auto f = r.at("file");
            atomic_write(game / ".solar-pack-metadata" / (f.at("sha512").get<std::string>() + ".json"),
                Json{{"name", f.value("title", f.value("name", ""))}, {"version", f.value("version", "")},
                     {"description", f.value("description", "")}, {"project_id", f.at("project_id")},
                     {"icon_url", f.value("icon_url", Json())}}.dump());
        }
        for (auto &r : plan.at("replacements")) {
            const auto old = r.at("filename").get<std::string>();
            auto source = checked_path(folder, old), backup = checked_path(stage, "previous/" + old);
            if (!fs::is_regular_file(source) || hash_file(source) != r.at("sha512"))
                throw Error("updates", "plan_stale", "资源文件已变化，请重新检查更新。");
            fs::create_directories(backup.parent_path()); fs::rename(source, backup); moved.push_back({source, backup});
        }
        for (auto &r : plan.at("replacements")) {
            auto name = r.at("file").at("filename").get<std::string>(), destName = name;
            auto dest = checked_path(folder, destName);
            if (fs::exists(dest)) throw Error("updates", "target_exists", "更新目标已存在，请先处理重复文件。");
            fs::rename(checked_path(stage, "payload/" + name), dest); added.push_back(dest); installed.push_back(name);
        }
        Json warnings = Json::array();
        for (auto &r : plan.at("replacements")) if (r.at("filename") != r.at("file").at("filename")) {
            warnings.push_back("文件名已变化，请在游戏内重新选择更新后的光影或材质包。"); break;
        }
        return {{"installed", installed}, {"warnings", warnings}, {"atomic_batch", true}};
    } catch (...) {
        bool failed = false;
        for (auto &p : added) { std::error_code ec; fs::remove(p, ec); failed |= bool(ec); }
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) { std::error_code ec; fs::rename(it->second, it->first, ec); failed |= bool(ec); }
        if (failed) {
            clean.p.clear(); // Never delete the only remaining previous copy.
            throw Error("updates", "rollback_failed", "回滚未完成，旧文件保留在：" + stage.u8string());
        }
        throw;
    }
}
} // namespace solar::engine
