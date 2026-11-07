// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include <limits>
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
static Json marketplace_request(const std::string &endpoint, const Task &task,
                                const Transport &transport, const Json &body = Json()) {
    task.check();
    const auto url = "https://api.modrinth.com/v2/" + endpoint;
    if (!transport) return body.is_null() ? fetch_json(url, task) : post_json(url, body, task);
    const auto response = transport(url, body.is_null() ? "" : body.dump(), "", "application/json");
    task.check();
    if (response.status != 200)
        throw Error("dependencies", "lookup_failed", "依赖元数据查询失败，请重新生成安装计划。", response.status, url);
    if (response.body.size() > 32 * 1024 * 1024)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    auto result = Json::parse(response.body, nullptr, false);
    if (result.is_discarded() || (!result.is_array() && !result.is_object()))
        throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
    return result;
}
static Json marketplace_versions(const std::string &project, const Json &instance, const Task &task,
                                 const Transport &transport, const std::string &loader,
                                 const std::string &source) {
    if (source == "curseforge")
        return curseforge_versions(project, instance.value("version", ""), loader, task);
    if (!transport)
        return resource_versions(project, instance.value("version", ""), loader, task);
    auto endpoint = "project/" + url_encode(project) + "/version?game_versions=" +
        url_encode(Json::array({instance.value("version", "")}).dump()) + "&loaders=" +
        url_encode(Json::array({loader}).dump());
    return resource_files(marketplace_request(endpoint, task, transport), true);
}
static void validate_marketplace_version(const Json &version) {
    if (!version.is_object() || dependency_id(version, "id").empty() ||
        dependency_id(version, "project_id").empty() || !version.contains("dependencies") ||
        !version["dependencies"].is_array() || !version.contains("files") || !version["files"].is_array())
        throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
    if (version["dependencies"].size() > 4096 || version["files"].size() > 4096)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    for (const auto &dependency : version["dependencies"])
        if (!dependency.is_object())
            throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
}
static std::uint32_t curseforge_fingerprint(std::string &bytes) {
    // CurseForge uses MurmurHash2 with seed 1 after removing four ASCII whitespace bytes.
    bytes.erase(std::remove_if(bytes.begin(), bytes.end(), [](unsigned char byte) {
        return byte == 9 || byte == 10 || byte == 13 || byte == 32;
    }), bytes.end());
    constexpr std::uint32_t multiplier = 0x5bd1e995;
    const auto data = reinterpret_cast<const unsigned char *>(bytes.data());
    std::uint32_t hash = 1 ^ static_cast<std::uint32_t>(bytes.size());
    size_t offset = 0;
    for (; bytes.size() - offset >= 4; offset += 4) {
        std::uint32_t word = data[offset] | (std::uint32_t(data[offset + 1]) << 8) |
            (std::uint32_t(data[offset + 2]) << 16) | (std::uint32_t(data[offset + 3]) << 24);
        word *= multiplier;
        word ^= word >> 24;
        hash = (hash * multiplier) ^ (word * multiplier);
    }
    for (size_t index = offset; index < bytes.size(); ++index)
        hash ^= std::uint32_t(data[index]) << (8 * (index - offset));
    if (offset != bytes.size()) hash *= multiplier;
    hash ^= hash >> 13;
    hash *= multiplier;
    return hash ^ (hash >> 15);
}
Json detail::curseforge_existing(const Json &instance, const Json &mods, const Task &task) {
    if (mods.size() > 4096)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    const auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    std::map<std::uint32_t, std::vector<std::pair<std::string, const Json *>>> local;
    for (const auto &mod : mods) {
        task.check();
        if (mod.value("sha512", "").empty()) continue;
        auto bytes = read_file(checked_path(game, "mods/" + mod.at("filename").get<std::string>()),
                               256 * 1024 * 1024);
        if (hash_bytes(bytes) != mod.at("sha512"))
            throw Error("mods", "plan_stale", "确认后模组目录发生变化，请重新生成安装计划。");
        auto sha1 = hash_bytes(bytes, "sha1");
        auto fingerprint = curseforge_fingerprint(bytes);
        local[fingerprint].emplace_back(std::move(sha1), &mod);
    }
    Json nodes = Json::array();
    std::set<std::pair<std::string, std::int64_t>> seen;
    for (auto next = local.begin(); next != local.end();) {
        Json batch = Json::array();
        for (size_t n = 0; next != local.end() && n < 64; ++next, ++n)
            batch.push_back(next->first);
        const auto response = fetch_curseforge_json("/v1/fingerprints/432", task,
                                                   {{"fingerprints", batch}});
        const auto &matches = response.at("data").at("exactMatches");
        if (!matches.is_array() || matches.size() > 4096)
            throw Error("dependencies", "invalid_response", "CurseForge 依赖元数据格式无效。");
        for (const auto &match : matches) {
            const auto &file = match.at("file");
            if (file.value("gameId", 0) != 432) continue;
            const auto fingerprint = file.value("fileFingerprint", std::uint32_t(0));
            if (std::find(batch.begin(), batch.end(), fingerprint) == batch.end()) continue;
            const auto project = file.value("modId", std::int64_t(0));
            const auto version = file.value("id", std::int64_t(0));
            constexpr auto max_id = std::numeric_limits<std::uint32_t>::max();
            if (project <= 0 || project > max_id || version <= 0 || version > max_id) continue;
            for (const auto &artifact : local.at(fingerprint)) {
                const auto *mod = artifact.second;
                // A 32-bit fingerprint is a lookup key. Confirm the full file against its SHA-1.
                const auto hashes = file.value("hashes", Json::array());
                const bool verified = std::any_of(hashes.begin(), hashes.end(), [&](const Json &hash) {
                    auto value = hash.value("value", "");
                    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                        return char(std::tolower(c));
                    });
                    return hash.value("algo", 0) == 1 && value == artifact.first;
                });
                const auto filename = mod->at("filename").get<std::string>();
                if (!verified || !seen.emplace(filename, version).second) continue;
                nodes.push_back({{"project_id", std::to_string(project)},
                                 {"version_id", std::to_string(version)},
                                 {"filename", filename}, {"sha512", mod->at("sha512")},
                                 {"name", mod->value("name", filename)},
                                 {"version", mod->value("version", "")},
                                 {"enabled", mod->value("enabled", true)}, {"source", "existing"},
                                 {"dependencies", curseforge_dependencies(file)}});
                if (nodes.size() > 4096)
                    throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
            }
        }
    }
    return nodes;
}
static bool available_curseforge_mod(const Json &file) {
    return file.value("project_type", "") == "mod" && file.value("downloadable", false) &&
        curseforge_download_url(file.value("url", "")) &&
        fs::u8path(file.value("filename", "")).extension() == ".jar";
}
static Json verified_curseforge_mod(const Json &selected, const Json &instance, const Task &task) {
    const auto file = curseforge_file(selected.at("project_id"), selected.at("version_id"), task);
    if (!available_curseforge_mod(file))
        throw Error("dependencies", "curseforge_unavailable", "此 CurseForge 文件不可通过启动器安装。", 0,
                    file.value("website_url", ""));
    if (!resource_compatible(file, instance.value("version", ""), instance.value("loader", "")) ||
        file.at("filename") != selected.at("filename") || file.at("sha1") != selected.at("sha1"))
        throw Error("dependencies", "version_mismatch", "所选文件与网站版本记录不一致，请重新确认。");
    auto verified = file;
    for (const auto field : {"optional", "root", "replaces", "previous_version", "install_disabled"})
        if (selected.contains(field)) verified[field] = selected.at(field);
    return verified;
}
static Json marketplace_existing(const Json &mods, const Task &task, const Transport &transport) {
    // components retain every nested candidate and its own bytes. The legacy
    // embedded_hashes aggregate can lose duplicate IDs and descendant hashes.
    Json artifacts = Json::array();
    std::set<std::string> artifact_keys;
    std::function<void(Json, const Json &, bool, int)> collect =
        [&](Json metadata, const Json &parent, bool embedded, int depth) {
        if (depth > 24 || artifacts.size() >= 4096)
            throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
        if (embedded && (metadata.value("environment", "*") == "server" ||
                         metadata.value("environment", "*") == "dedicated_server")) return;
        metadata["filename"] = parent.at("filename");
        metadata["enabled"] = parent.value("enabled", true);
        metadata["embedded"] = embedded;
        const auto key = metadata.value("filename", "") + ":" + metadata.value("sha512", "") +
            (embedded ? ":nested" : ":root");
        if (!artifact_keys.insert(key).second) return;
        artifacts.push_back(metadata);
        const auto components = metadata.value("components", Json::array());
        for (const auto &child : components) collect(child, parent, true, depth + 1);
        if (components.empty()) {
            const auto legacy = metadata.value("embedded_hashes", Json::object());
            for (const auto &entry : legacy.items()) {
                Json child{{"sha512", entry.value()}, {"name", entry.key()},
                           {"version", ""}, {"components", Json::array()}};
                collect(child, parent, true, depth + 1);
            }
        }
    };
    for (const auto &m : mods) collect(m, m, false, 0);
    Json known = Json::object(), hashes = Json::array();
    std::set<std::string> requested_hashes;
    for (const auto &m : artifacts) {
        const auto hash = m.value("sha512", "");
        if (!m.value("embedded", false) && !m.value("project_id", "").empty() &&
            !m.value("version_id", "").empty())
            known[hash] = {{"project_id", m["project_id"]}, {"id", m["version_id"]}};
        else if (!hash.empty() && requested_hashes.insert(hash).second) hashes.push_back(hash);
    }
    // Bound both request size and work even for a large local directory.
    if (hashes.size() > 4096 || mods.size() > 4096)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    for (size_t offset = 0; offset < hashes.size(); offset += 64) {
        Json chunk = Json::array();
        for (size_t n = offset; n < std::min(offset + 64, hashes.size()); ++n) chunk.push_back(hashes[n]);
        const auto identities = marketplace_request("version_files", task, transport,
                                                    {{"hashes", chunk}, {"algorithm", "sha512"}});
        if (!identities.is_object())
            throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
        for (const auto &entry : identities.items())
            if (contains(chunk, entry.key())) known[entry.key()] = entry.value();
    }
    std::map<std::string, Json> metadata;
    for (const auto &entry : known.items()) {
        const auto id = entry.value().value("id", "");
        if (!id.empty() && entry.value().contains("dependencies")) metadata[id] = entry.value();
    }
    Json missing = Json::array();
    std::set<std::string> seen;
    for (const auto &entry : known.items()) {
        const auto id = entry.value().value("id", "");
        if (!id.empty() && !metadata.count(id) && seen.insert(id).second) missing.push_back(id);
    }
    for (size_t offset = 0; offset < missing.size(); offset += 64) {
        Json chunk = Json::array();
        for (size_t n = offset; n < std::min(offset + 64, missing.size()); ++n) chunk.push_back(missing[n]);
        const auto versions = marketplace_request("versions?ids=" + url_encode(chunk.dump()), task, transport);
        if (!versions.is_array())
            throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
        for (const auto &version : versions) {
            const auto id = version.value("id", "");
            if (contains(chunk, id)) metadata[id] = version;
        }
        for (const auto &id : chunk)
            if (!metadata.count(id.get<std::string>()))
                throw Error("dependencies", "missing_version", "已安装模组的版本元数据不可用，请重新确认。");
    }
    Json nodes = Json::array();
    auto append = [&](const Json &m, const std::string &hash, bool embedded) {
        if (!known.contains(hash)) return;
        const auto identity = known.at(hash);
        const auto id = identity.value("id", ""), project = identity.value("project_id", "");
        if (id.empty() || project.empty()) return;
        const auto raw = metadata.at(id);
        validate_marketplace_version(raw);
        if (raw.value("project_id", "") != project)
            throw Error("dependencies", "project_mismatch", "依赖版本与项目不一致。");
        bool verified = false;
        for (const auto &file : raw.value("files", Json::array()))
            verified |= file.value("hashes", Json::object()).value("sha512", "") == hash;
        if (!verified)
            throw Error("dependencies", "version_mismatch", "已安装文件与网站版本记录不一致，请重新确认。");
        nodes.push_back({{"project_id", project}, {"version_id", id},
                         {"filename", m.at("filename")}, {"sha512", hash},
                         {"name", m.value("name", project)},
                         {"version", embedded ? raw.value("version_number", id) : m.value("version", "")},
                         {"version_number", raw.value("version_number", id)},
                         {"enabled", m.value("enabled", true)},
                         {"embedded", embedded}, {"possible", embedded}, {"source", "existing"},
                         {"dependencies", raw.value("dependencies", Json::array())}});
        if (nodes.size() > 4096)
            throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    };
    for (const auto &m : artifacts) append(m, m.value("sha512", ""), m.value("embedded", false));
    return nodes;
}
static Json active_marketplace_graph(const Json &plan) {
    Json nodes = Json::array();
    std::set<std::string> replaced;
    for (const auto &file : plan.value("files", Json::array())) {
        const auto old = file.value("replaces", Json());
        if (old.is_object() && !old.value("embedded", false)) replaced.insert(old.value("filename", ""));
    }
    for (auto node : plan.value("marketplace_existing", Json::array())) {
        const auto name = node.value("filename", "");
        if (replaced.count(name)) continue;
        if (contains(plan.value("disable_files", Json::array()), name)) node["enabled"] = false;
        if (contains(plan.value("enable_files", Json::array()), name)) node["enabled"] = true;
        if (node.value("enabled", true)) nodes.push_back(node);
    }
    for (const auto &file : plan.value("files", Json::array())) {
        if (file.value("install_disabled", false)) continue;
        auto node = file;
        node["source"] = "candidate";
        node["embedded"] = false;
        node["possible"] = false;
        nodes.push_back(node);
    }
    return nodes;
}
static std::string marketplace_label(const Json &node) {
    auto name = dependency_id(node, "title");
    if (name.empty()) name = dependency_id(node, "name");
    if (name.empty()) name = node.value("project_id", "");
    const auto version = node.value("version_number", node.value("version", node.value("version_id", "")));
    return name + (version.empty() ? "" : " · " + version);
}
static void refresh_marketplace_conflicts(Json &plan) {
    auto cards = Json::array();
    for (const auto &conflict : plan.value("conflicts", Json::array()))
        if (!conflict.value("marketplace", false) && !conflict.value("generated", false)) cards.push_back(conflict);
    const auto nodes = active_marketplace_graph(plan);
    Json conflicts = Json::array(), warnings = Json::array();
    std::set<std::string> card_files, conflict_keys;
    std::map<std::string, std::vector<const Json *>> by_project, by_version;
    for (const auto &node : nodes) {
        by_project[node.value("project_id", "")].push_back(&node);
        by_version[node.value("version_id", "")].push_back(&node);
    }
    for (const auto &source : nodes) {
        std::set<std::pair<std::string, std::string>> declarations;
        for (const auto &dep : source.value("dependencies", Json::array())) {
            if (dep.value("dependency_type", "required") != "incompatible") continue;
            const auto project = dependency_id(dep, "project_id"), version = dependency_id(dep, "version_id");
            if ((project.empty() && version.empty()) || !declarations.emplace(project, version).second) continue;
            const auto &index = project.empty() ? by_version : by_project;
            const auto matches = index.find(project.empty() ? version : project);
            if (matches == index.end()) continue;
            for (const auto *target_pointer : matches->second) {
                const auto &target = *target_pointer;
                if ((!project.empty() && target.value("project_id", "") != project) ||
                    (!version.empty() && target.value("version_id", "") != version)) continue;
                const auto key = source.value("project_id", "") + ":" + source.value("version_id", "") + ":" +
                    target.value("project_id", "") + ":" + target.value("version_id", "") + ":" +
                    source.value("filename", "") + ":" + target.value("filename", "");
                if (!conflict_keys.insert(key).second) continue;
                Json conflict{{"from_project", source.value("project_id", "")},
                              {"from_version", source.value("version_id", "")},
                              {"to_project", target.value("project_id", "")},
                              {"to_version", target.value("version_id", "")},
                              {"reason", "作者声明不兼容"}, {"marketplace", true},
                              {"optional", source.value("optional", false) || target.value("optional", false)}};
                if (source.value("embedded", false) || target.value("embedded", false)) {
                    // The local CNF selects a compatible nested assignment; a
                    // verified nested file is only a possible loader candidate.
                    // Website-only exclusions are not part of that CNF, so do
                    // not pretend every available nested version is loaded.
                    conflict["uncertain"] = true;
                    conflict["code"] = "embedded_marketplace_conflict";
                    conflict["message"] = "内嵌组件的加载选择尚未确定：" + marketplace_label(source) + " 与 " +
                        marketplace_label(target) + " 的网站不兼容声明需由实际加载器确认。";
                    conflict["from_name"] = marketplace_label(source);
                    conflict["to_name"] = marketplace_label(target);
                    warnings.push_back(conflict);
                    continue;
                }
                // Existing files have an explicit, reviewable disable action. A new
                // candidate must instead be deselected or replaced with another version.
                if (target.value("source", "") == "existing") conflict["filename"] = target.value("filename", "");
                else if (source.value("source", "") == "existing") conflict["filename"] = source.value("filename", "");
                conflicts.push_back(conflict);
                const auto name = conflict.value("filename", "");
                if (!name.empty() && card_files.insert(name).second) cards.push_back(conflict);
            }
        }
    }
    std::set<std::string> replaced;
    for (const auto &file : plan.value("files", Json::array())) {
        const auto old = file.value("replaces", Json());
        if (old.is_object() && !old.value("embedded", false)) replaced.insert(old.value("filename", ""));
    }
    const auto target_instance = plan.value("target", Json::object());
    for (const auto &source : nodes) {
        if (source.value("embedded", false)) continue; // The staged local solver checks nested rendering providers.
        const auto project = source.value("project_id", "");
        const auto slug = source.value("slug", "");
        const bool iris = project == "iris" || project == "YL57xq9U" || slug == "iris-shaders";
        if (!iris && project != "sodium" && project != "AANobbMI" && slug != "sodium") continue;
        auto append = [&](const std::string &target_project, const std::string &target_version,
                          const std::string &filename, const std::string &kind) {
            Json conflict{{"from_project", project}, {"from_version", source.value("version_id", "")},
                          {"to_project", target_project}, {"to_version", target_version},
                          {"optional", source.value("optional", false)}, {"kind", kind},
                          {"generated", true}, {"reason", "与所选渲染组件不兼容"}};
            if (!filename.empty()) conflict["filename"] = filename;
            conflicts.push_back(conflict);
            if (!filename.empty() && card_files.insert(filename).second) cards.push_back(conflict);
        };
        if (!target_instance.value("optifine", Json::object()).empty())
            append("optifine", "", "", "managed_optifine");
        for (const auto &mod : plan.value("local_graph_files", Json::array())) {
            const auto filename = mod.value("filename", "");
            if (replaced.count(filename)) continue;
            bool enabled = mod.value("enabled", true);
            if (contains(plan.value("disable_files", Json::array()), filename)) enabled = false;
            if (contains(plan.value("enable_files", Json::array()), filename)) enabled = true;
            if (!enabled) continue;
            const auto ids = mod.value("own_ids", mod.value("ids", Json::array()));
            if (contains(ids, "optifine") || (iris && contains(ids, "optifabric")))
                append(contains(ids, "optifine") ? "optifine" : "optifabric",
                       mod.value("version_id", ""), filename, "rendering");
        }
    }
    plan["marketplace_graph"] = nodes;
    plan["graph_conflicts"] = conflicts;
    plan["graph_warnings"] = warnings;
    plan["conflicts"] = cards;
}
static void validate_marketplace_conflicts(const Json &plan) {
    if (!plan.value("graph_conflicts", Json::array()).empty())
        throw Error("mods", "unresolved_conflict", "所选模组之间存在作者声明的不兼容关系，请取消选择或停用冲突组件。");
}
static Json revalidate_marketplace_plan(const Json &plan, const Json &mods, const Task &task,
                                       const Transport &transport) {
    auto reviewed = plan;
    const auto instance = plan.at("target");
    const auto source = plan.value("source", "modrinth");
    if (source != "modrinth" && source != "curseforge")
        throw Error("dependencies", "invalid_source", "模组来源无效。");
    reviewed["local_graph_files"] = mods;
    reviewed["marketplace_existing"] = source == "curseforge" ? detail::curseforge_existing(instance, mods, task)
                                                               : marketplace_existing(mods, task, transport);
    auto files = plan.value("files", Json::array());
    if (files.size() > 128)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    Json ids = Json::array();
    std::set<std::string> unique;
    for (const auto &file : files) {
        if (file.value("source", "modrinth") != source)
            throw Error("dependencies", "invalid_source", "同一安装计划中的模组来源必须一致。");
        const auto id = file.value("version_id", "");
        if (id.empty())
            throw Error("dependencies", "missing_version", "资源缺少版本标识。");
        if (source == "modrinth" && unique.insert(id).second) ids.push_back(id);
    }
    std::map<std::string, Json> metadata;
    for (size_t offset = 0; offset < ids.size(); offset += 64) {
        Json chunk = Json::array();
        for (size_t n = offset; n < std::min(offset + 64, ids.size()); ++n) chunk.push_back(ids[n]);
        const auto versions = marketplace_request("versions?ids=" + url_encode(chunk.dump()), task, transport);
        if (!versions.is_array())
            throw Error("dependencies", "invalid_response", "依赖元数据格式无效。");
        for (const auto &version : versions)
            if (contains(chunk, version.value("id", ""))) metadata[version.value("id", "")] = version;
    }
    for (auto &file : files) {
        if (source == "curseforge") {
            auto fresh = verified_curseforge_mod(file, instance, task);
            if (fresh.at("dependencies") != file.at("dependencies"))
                throw Error("dependencies", "plan_stale", "确认后模组依赖发生变化，请重新生成安装计划。");
            file = std::move(fresh);
        } else {
            const auto id = file.value("version_id", "");
            if (!metadata.count(id))
                throw Error("dependencies", "missing_version", "所选模组的版本元数据不可用，请重新确认。");
            validate_marketplace_version(metadata.at(id));
            const auto candidates = resource_files(Json::array({metadata.at(id)}), false);
            auto same = std::find_if(candidates.begin(), candidates.end(), [&](const Json &candidate) {
                return candidate.value("project_id", "") == file.value("project_id", "") &&
                    candidate.value("sha512", "") == file.value("sha512", "") &&
                    candidate.value("filename", "") == file.value("filename", "");
            });
            if (same == candidates.end() ||
                !resource_compatible(*same, instance.value("version", ""), instance.value("loader", "")))
                throw Error("dependencies", "version_mismatch", "所选文件与网站版本记录不一致，请重新确认。");
            file["dependencies"] = same->value("dependencies", Json::array());
        }
        const auto old = file.value("replaces", Json());
        if (old.is_object() && !old.value("embedded", false)) {
            const auto &existing = reviewed["marketplace_existing"];
            const auto found = std::find_if(existing.begin(), existing.end(), [&](const Json &node) {
                return !node.value("embedded", false) && node.value("filename", "") == old.value("filename", "") &&
                    node.value("sha512", "") == old.value("sha512", "") &&
                    node.value("project_id", "") == file.value("project_id", "");
            });
            if (found == existing.end())
                throw Error("mods", "replacement_changed", "待替换文件发生变化。");
        }
    }
    reviewed["files"] = files;
    refresh_marketplace_conflicts(reviewed);
    validate_marketplace_conflicts(reviewed);
    return reviewed;
}
Json plan_mod_install(const Json &instance, const Json &root, const Task &task, const Transport &transport) {
    return plan_mod_installs(instance, Json::array({root}), task, "", transport);
}
Json plan_mod_installs(const Json &instance, const Json &roots, const Task &task, const std::string &channel,
                       const Transport &transport) {
    if (!roots.is_array() || roots.empty() || roots.size() > 128)
        throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
    const auto source = roots.front().value("source", "modrinth");
    if (source != "modrinth" && source != "curseforge")
        throw Error("dependencies", "invalid_source", "模组来源无效。");
    std::map<std::string, Json> requested;
    auto permitted = [&](const Json &file) {
        auto type = file.value("release_type", "release");
        return channel.empty() || type == "release" ||
            (channel != "release" && type == "beta") || (channel == "alpha" && type == "alpha");
    };
    Json root_projects = Json::array();
    for (auto root : roots) {
        if (root.value("source", "modrinth") != source)
            throw Error("dependencies", "invalid_source", "同一安装计划中的模组来源必须一致。");
        if (source == "curseforge") root = verified_curseforge_mod(root, instance, task);
        auto project = root.at("project_id").get<std::string>();
        if (!requested.emplace(project, root).second)
            throw Error("dependencies", "version_conflict", "同一依赖被要求安装不同版本。");
        root_projects.push_back(project);
    }
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto mods = local_mods(game, instance.value("loader", ""));
    Json expected = Json::object();
    for (auto &m : mods) {
        expected[m.at("filename").get<std::string>()] = m["sha512"];
    }
    const auto installed_graph = source == "curseforge" ? detail::curseforge_existing(instance, mods, task)
                                                        : marketplace_existing(mods, task, transport);
    std::map<std::string, Json> existing;
    for (const auto &node : installed_graph) {
        const auto project = node.value("project_id", "");
        if (node.value("embedded", false) && existing.count(project) &&
            !existing.at(project).value("embedded", false)) continue;
        existing[project] = node;
    }
    Json plan{{"target", instance},
              {"source", source},
              {"files", Json::array()},
              {"reuse", Json::array()},
              {"edges", Json::array()},
              {"optional", Json::array()},
              {"skipped", Json::array()},
              {"disable_files", Json::array()},
              {"conflicts",Json::array()},
              {"marketplace_existing", installed_graph},
              {"local_graph_files", mods},
              {"expected_files", expected},
              {"root_project", roots.front().value("project_id", "")},
              {"root_projects", root_projects},
              {"original_manifest", mod_inventory(game)}};
    std::map<std::string, std::string> chosen;
    std::set<std::string> visiting;
    std::function<void(Json, std::string, bool, bool, int)> resolve = [&](Json file, std::string from,
                                                                    bool optional, bool direct_optional, int depth) {
        task.check();
        if (depth > 24 || (chosen.size() >= 128 && !chosen.count(file.value("project_id", ""))))
            throw Error("dependencies", "graph_limit", "模组依赖层级或数量超过限制。");
        auto project = file.value("project_id", "");
        const bool reusing_existing = existing.count(project) &&
            existing[project].value("version_id", "") == file.value("version_id", "");
        if (!permitted(file) && !reusing_existing)
            throw Error("dependencies", "channel_conflict", "所需依赖超出所选更新频道，请检查后重新选择。");
        if (project.empty())
            throw Error("dependencies", "missing_project", "资源缺少项目标识。");
        if (!resource_compatible(file, instance.value("version", ""), instance.value("loader", "")))
            throw Error("dependencies", "incompatible_file",
                        "模组文件与目标游戏版本或加载器不匹配。");
        if (!from.empty())
            plan["edges"].push_back({{"from", from}, {"to", project}, {"optional", direct_optional}});
        if (chosen.count(project)) {
            if (chosen[project] != file.value("version_id", ""))
                throw Error("dependencies", "version_conflict", "同一依赖被要求安装不同版本。");
            return;
        }
        chosen[project] = file.value("version_id", "");
        file["optional"] = optional;
        file["root"] = requested.count(project) != 0;
        if (file["root"] == true) file["optional"] = false;
        if (reusing_existing) {
            auto reuse = existing[project];
            reuse["project_id"] = project;
            reuse["file"] = file;
            plan["reuse"].push_back(reuse);
        } else {
            if (source == "curseforge" && !available_curseforge_mod(file))
                throw Error("dependencies", "curseforge_unavailable", "此 CurseForge 文件不可通过启动器安装。", 0,
                            file.value("website_url", ""));
            file["replaces"] = existing.count(project) ? existing[project] : Json();
            if (existing.count(project)) file["previous_version"] = existing[project].value("version", "");
            file["install_disabled"] = existing.count(project) && !existing[project].value("enabled", true);
            plan["files"].push_back(file);
        }
        for (auto &dep : file.value("dependencies", Json::array())) {
            auto type = dep.value("dependency_type", "required");
            if (type == "incompatible") {
                continue;
            }
            if (type != "required" && type != "optional")
                continue;
            auto dep_project = dependency_id(dep, "project_id");
            auto version = dependency_id(dep, "version_id");
            if (dep_project.empty() && version.empty())
                continue;
            Json candidate;
            if (requested.count(dep_project) && version.empty()) {
                candidate = requested.at(dep_project);
            } else if (!version.empty()) {
                if (source == "curseforge")
                    throw Error("dependencies", "invalid_response", "CurseForge 依赖元数据格式无效。");
                auto raw = marketplace_request("version/" + url_encode(version), task, transport);
                validate_marketplace_version(raw);
                auto files = resource_files(Json::array({raw}), false);
                if (files.empty())
                    throw Error("dependencies", "missing_file", "依赖版本没有可安装文件。");
                candidate = files.front();
                if (candidate.value("version_id", "") != version)
                    throw Error("dependencies", "version_mismatch", "所选文件与网站版本记录不一致，请重新确认。");
                if (!dep_project.empty() && candidate.value("project_id", "") != dep_project)
                    throw Error("dependencies", "project_mismatch", "依赖版本与项目不一致。");
            } else {
                auto files = marketplace_versions(dep_project, instance, task, transport,
                                                  instance.value("loader", ""), source);
                if (files.empty() && instance.value("loader", "") == "quilt")
                    files = marketplace_versions(dep_project, instance, task, transport, "fabric", source);
                // A project-only dependency does not request a different
                // version. Prefer an already installed compatible copy instead
                // of gratuitously upgrading/downgrading the user's environment.
                auto installed = existing.count(dep_project) ? existing[dep_project].value("version_id", "") : "";
                auto same = std::find_if(files.begin(), files.end(), [&](const Json &f) { return !installed.empty() && f.value("version_id", "") == installed; });
                if (same != files.end()) {
                    candidate = *same;
                } else {
                    const auto compatible = !files.empty();
                    const auto website = compatible ? files.front().value("website_url", "") : "";
                    files.erase(std::remove_if(files.begin(), files.end(), [&](const Json &f) {
                        return !permitted(f) || (source == "curseforge" && !available_curseforge_mod(f));
                    }), files.end());
                    if (files.empty()) {
                        if (type == "optional") {
                            plan["optional_errors"][dep_project] = "没有匹配文件";
                            continue;
                        }
                        if (source == "curseforge" && compatible)
                            throw Error("dependencies", "curseforge_unavailable",
                                        "此 CurseForge 文件不可通过启动器安装。", 0, website);
                        throw Error("dependencies", "missing_compatible_version", "没有找到适配的前置模组文件。");
                    }
                    auto stable = std::find_if(files.begin(), files.end(), [](const Json &f) {
                        return f.value("release_type", "") == "release";
                    });
                    candidate = stable == files.end() ? files.front() : *stable;
                }
            }
            auto p = candidate.value("project_id", "");
            if (type == "optional")
                plan["optional"].push_back(p);
            resolve(candidate, project, optional || type == "optional", type == "optional", depth + 1);
        }
    };
    for (const auto &root : root_projects) resolve(requested.at(root.get<std::string>()), "", false, false, 0);
    // A shared node may first be discovered below an optional branch. Compute
    // unconditional reachability after all roots/edges exist, so a later
    // required path promotes the node and all its required descendants. Keep
    // each author's optional edge intact; only the selected node's status is
    // promoted, independently of traversal order.
    std::set<std::string> required;
    for (const auto &root : root_projects) required.insert(root.get<std::string>());
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto &edge : plan["edges"])
            if (required.count(edge.value("from", "")) && !edge.value("optional", false))
                changed |= required.insert(edge.value("to", "")).second;
    }
    for (auto &file : plan["files"])
        if (required.count(file.value("project_id", ""))) file["optional"] = false;
    for (auto &reuse : plan["reuse"])
        if (required.count(reuse.value("project_id", ""))) reuse["file"]["optional"] = false;
    Json optional_choices = Json::array();
    std::set<std::string> optional_ids;
    for (const auto &id : plan["optional"])
        if (!required.count(id.get<std::string>()) && optional_ids.insert(id.get<std::string>()).second)
            optional_choices.push_back(id);
    plan["optional"] = optional_choices;
    refresh_marketplace_conflicts(plan);
    for (const auto &conflict : plan["graph_conflicts"])
        if (conflict.value("filename", "").empty() && !conflict.value("optional", false)) {
            if (conflict.value("kind", "") == "managed_optifine")
                throw Error("mods", "managed_optifine", "请先在版本组件中移除 OptiFine，再安装 Iris / Sodium。");
            throw Error("dependencies", "version_conflict", "所选更新之间存在作者声明的不兼容关系。");
        }
    return plan;
}
Json select_dependencies(const Json &original, const Json &selected, const Json &redownload,
                         const Json &enable) {
    auto plan = original;
    Json files = Json::array(), reuse = Json::array(), skipped = Json::array(),
         enabled = Json::array();
    std::set<std::string> needed{plan.value("root_project", "")};
    for (auto &root : plan.value("root_projects", Json::array())) needed.insert(root.get<std::string>());
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
            file["install_disabled"] = !r.value("enabled", true) && !contains(enable, id);
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
    refresh_marketplace_conflicts(plan);
    return plan;
}
Json apply_mod_plan(const Json &plan, const Task &task, const Transport &transport) {
    for (const auto &conflict : plan.value("conflicts", Json::array()))
        if (!conflict.value("marketplace", false) && !conflict.value("generated", false) &&
            !contains(plan.value("disable_files", Json::array()), conflict.value("filename", "")))
            throw Error("mods", "unresolved_conflict", "请先确认停用作者声明不兼容的组件。");
    auto i = plan.at("target");
    auto managed=i.value("optifine",Json::object()).value("modFile","");
    for(auto &name:plan.value("disable_files",Json::array()))
        if(!managed.empty() && (name==managed || name==managed+".disabled"))
            throw Error("mods","version_component","OptiFine 属于版本组件，请在版本组件中修改。");
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
    auto verify_inventory = [&] {
        Json current = Json::object();
        for (auto &m : local_mods(game, i.value("loader", ""))) current[m.at("filename").get<std::string>()] = m["sha512"];
        if (current != expected)
            throw Error("mods", "plan_stale", "确认后模组目录发生变化，请重新生成安装计划。");
    };
    verify_inventory();
    if (mod_inventory(game) != plan.value("original_manifest", Json::object()))
        throw Error("mods", "manifest_changed", "模组索引发生变化，请重新确认。");
    // Rebuild from fresh file identities and authoritative website declarations.
    // The review JSON is user-editable: cached nodes/conflict cards alone cannot
    // prove that every selected or installed version is still compatible.
    auto reviewed_graph = plan;
    if (plan.contains("marketplace_existing") || !plan.value("files", Json::array()).empty() ||
        !plan.value("reuse", Json::array()).empty() || !plan.value("enable_files", Json::array()).empty())
        reviewed_graph = revalidate_marketplace_plan(plan, local_mods(game, i.value("loader", "")), task, transport);
    auto stage = checked_path(game, ".solar-mod-stage/" + random_url_token());
    fs::create_directories(stage);
    std::vector<std::pair<fs::path, fs::path>> moved;
    std::vector<fs::path> added;
    Json installed = Json::array();
    try {
        auto mods = local_mods(game, i.value("loader", ""));
        for (const auto &f : reviewed_graph.value("files", Json::array())) {
            task.check();
            auto name = f.value("filename", "");
            if (name.find('/') != name.npos)
                throw Error("mods", "unsafe_filename", "模组文件名无效。");
            auto target = checked_path(stage, name);
            download(f, target, task);
            auto metadata = inspect_mod(target, i.value("loader", ""));
            metadata["enabled"] = !f.value("install_disabled", false);
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
        task.check(); verify_inventory();
        if (mod_inventory(game) != plan.value("original_manifest", Json::object()))
            throw Error("mods", "manifest_changed", "模组索引发生变化，请重新确认。");
        auto manifest = plan.value("original_manifest", Json::object());
        for (const auto &f : reviewed_graph.value("files", Json::array())) {
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
            auto dest = checked_path(folder, name + (f.value("install_disabled", false) ? ".disabled" : ""));
            if (fs::exists(dest))
                throw Error("mods", "target_exists", "目标模组文件已存在，不能静默覆盖。");
            fs::rename(checked_path(stage, name), dest);
            added.push_back(dest);
            if (f.value("source", "modrinth") == "curseforge") {
                manifest.erase(name);
            } else {
                manifest[name] = {{"projectId", f.value("project_id", "")},
                                  {"versionId", f.value("version_id", "")},
                                  {"sha512", f.value("sha512", "")},
                                  {"title", f.value("title", f.value("name", ""))},
                                  {"icon_url", f.value("icon_url", Json())}};
            }
            installed.push_back(dest.filename().u8string());
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
        for (const auto &warning : reviewed_graph.value("graph_warnings", Json::array()))
            check["warnings"].push_back(warning.at("message"));
        return {{"installed", installed}, {"warnings", check["warnings"]},
                {"graph_warnings", reviewed_graph.value("graph_warnings", Json::array())}};
    } catch (...) {
        bool rollback_failed = false;
        for (auto &p : added) {
            std::error_code e;
            fs::remove(p, e);
            rollback_failed |= bool(e);
        }
        for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
            std::error_code e;
            if (fs::exists(it->second))
                fs::rename(it->second, it->first, e);
            rollback_failed |= bool(e);
        }
        if (rollback_failed)
            throw Error("mods", "rollback_failed", "回滚未完成，旧文件保留在：" + stage.u8string());
        std::error_code e;
        fs::remove_all(stage, e);
        throw;
    }
}
} // namespace solar::engine
