// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <mutex>
#include <regex>

namespace solar::engine {
namespace {
std::mutex policy_mutex;
std::string resource_folder(const std::string &kind) {
    if (kind == "mod") return "mods";
    if (kind == "shader") return "shaderpacks";
    if (kind == "resourcepack") return "resourcepacks";
    throw Error("updates", "invalid_kind", "资源类型无效。");
}
Json read_policy(const fs::path &game) {
    auto file = checked_path(game, ".solar-resource-locks.json");
    if (!fs::exists(file)) return {{"schema", 1}, {"locks", Json::object()}};
    auto data = Json::parse(read_file(file, 2 * 1024 * 1024), nullptr, false);
    if (!data.is_object() || !data.contains("schema") || data.at("schema") != 1 ||
        !data.contains("locks") || !data.at("locks").is_object())
        throw Error("updates", "invalid_locks", "资源版本锁记录损坏，请检查实例目录中的记录文件。");
    for (const auto &entry : data.at("locks").items()) {
        resource_folder(entry.key());
        if (!entry.value().is_object())
            throw Error("updates", "invalid_locks", "资源版本锁记录损坏，请检查实例目录中的记录文件。");
        for (const auto &record : entry.value().items())
            if (!std::regex_match(record.key(), std::regex("[a-f0-9]{128}")) || !record.value().is_object())
                throw Error("updates", "invalid_locks", "资源版本锁记录损坏，请检查实例目录中的记录文件。");
    }
    return data;
}
}
Json resource_locks(const Json &instance, const std::string &kind) {
    resource_folder(kind);
    const auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    std::lock_guard<std::mutex> guard(policy_mutex);
    return read_policy(game).at("locks").value(kind, Json::object());
}
void annotate_resource_locks(const Json &instance, const std::string &kind, Json &rows) {
    auto locks = resource_locks(instance, kind);
    for (auto &row : rows) {
        const auto hash = row.value("sha512", "");
        row["update_locked"] = !hash.empty() && locks.contains(hash);
    }
}
Json set_resource_locks(const Json &instance, const std::string &kind, const Json &files, bool locked) {
    const auto folder = resource_folder(kind);
    if (!files.is_array() || files.empty() || files.size() > 128)
        throw Error("updates", "invalid_selection", "请选择 1 至 128 项资源。");
    const auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    std::lock_guard<std::mutex> guard(policy_mutex);
    auto policy = read_policy(game);
    auto records = policy.at("locks").value(kind, Json::object());
    for (const auto &file : files) {
        const auto name = file.at("filename").get<std::string>();
        // UI choices are snapshots: never pin a replacement which appeared
        // between rendering the row and clicking the lock action.
        const auto hash = file.value("sha512", "");
        if (name.empty() || fs::u8path(name).filename().u8string() != name ||
            name.find_first_of("/\\:") != name.npos ||
            !std::regex_match(hash, std::regex("[a-f0-9]{128}")))
            throw Error("updates", "invalid_lock_target", "只能锁定可校验的本地资源文件。");
        const auto target = checked_path(game, folder + "/" + name);
        if (!fs::is_regular_file(target) || fs::is_symlink(target) || hash_file(target) != hash)
            throw Error("updates", "plan_stale", "资源文件已变化，请重新检查更新。");
        if (locked) {
            records[hash] = {{"filename", name}, {"name", file.value("name", name)},
                             {"version", file.value("version", "")}, {"locked_at", now()}};
        } else records.erase(hash);
    }
    policy["locks"][kind] = records;
    atomic_write(checked_path(game, ".solar-resource-locks.json"), policy.dump(2));
    return {{"saved", true}, {"locked", locked}, {"count", files.size()}};
}
void assert_resource_replacement_unlocked(const Json &instance, const std::string &kind,
                                          const Json &previous, const Json &next) {
    const auto hash = previous.value("sha512", "");
    if (hash.empty() || !resource_locks(instance, kind).contains(hash)) return;
    // Repairing the identical build does not change the pinned version.
    if (next.value("sha512", "") == hash) return;
    if (next.value("source", "") == "curseforge" && !next.value("sha1", "").empty()) {
        const auto game = fs::u8path(instance.at("game_directory").get<std::string>());
        const auto file = checked_path(game, resource_folder(kind) + "/" + previous.at("filename").get<std::string>());
        if (fs::is_regular_file(file) && hash_file(file) == hash &&
            hash_file(file, "sha1") == next.at("sha1")) return;
    }
    throw Error("updates", "version_locked", "资源版本已锁定，请先在资源列表中解锁：" + previous.value("filename", ""));
}
void assert_mod_version_locks(const Json &instance, const Json &plan, bool include_optional) {
    const auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    for (const auto &file : plan.value("files", Json::array())) {
        if (!include_optional && file.value("optional", false)) continue;
        const auto previous = file.value("replaces", Json());
        if (previous.is_object() && !previous.value("embedded", false))
            assert_resource_replacement_unlocked(instance, "mod", previous, file);
        // Also protect a same-name target even if a hand-edited plan removes
        // its replacement metadata. Inventory validation still checks identity.
        const auto name = file.value("filename", "");
        if (name.empty()) continue;
        const auto target = checked_path(game, "mods/" + name);
        if (fs::is_regular_file(target))
            assert_resource_replacement_unlocked(instance, "mod",
                {{"filename", name}, {"sha512", hash_file(target)}}, file);
    }
}
} // namespace solar::engine
