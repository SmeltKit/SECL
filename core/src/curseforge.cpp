// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <algorithm>
#include <cctype>
#include <limits>
#include <regex>
#include <set>

namespace solar::engine {
namespace {
constexpr int minecraft = 432;
constexpr int max_results = 10000;
const std::map<std::string, int> loader_ids{
    {"forge", 1}, {"cauldron", 2}, {"liteloader", 3},
    {"fabric", 4}, {"quilt", 5}, {"neoforge", 6}};

[[noreturn]] void invalid_response() {
    throw Error("curseforge", "invalid_response", "CurseForge 返回的资源信息格式无效。");
}

std::string string_at(const Json &value, const char *key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_string() ? found->get<std::string>() : "";
}

Json array_at(const Json &value, const char *key) {
    const auto found = value.find(key);
    return found != value.end() && found->is_array() ? *found : Json::array();
}

std::int64_t number_at(const Json &value, const char *key, std::int64_t fallback = 0) {
    const auto found = value.find(key);
    if (found == value.end() || !found->is_number_integer()) return fallback;
    if (found->is_number_unsigned() &&
        found->get<std::uint64_t>() > std::uint64_t(std::numeric_limits<std::int64_t>::max()))
        return fallback;
    return found->get<std::int64_t>();
}

bool flag_at(const Json &value, const char *key, bool fallback = false) {
    const auto found = value.find(key);
    return found != value.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

std::string numeric_id(std::int64_t value) {
    if (value <= 0 || value > std::numeric_limits<std::uint32_t>::max()) return "";
    return std::to_string(value);
}

std::uint32_t request_id(const std::string &value) {
    if (value.empty() || value.size() > 10 || value.front() == '0' ||
        !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
        throw Error("curseforge", "invalid_id", "CurseForge 项目标识无效。");
    const auto parsed = std::stoull(value);
    if (parsed > std::numeric_limits<std::uint32_t>::max())
        throw Error("curseforge", "invalid_id", "CurseForge 项目标识无效。");
    return static_cast<std::uint32_t>(parsed);
}

int class_id(const std::string &kind) {
    if (kind == "mod") return 6;
    if (kind == "resourcepack") return 12;
    if (kind == "shader") return 6552;
    throw Error("resources", "invalid_type", "资源类型无效。");
}

std::string class_name(std::int64_t value) {
    if (value == 6) return "mod";
    if (value == 12) return "resourcepack";
    if (value == 6552) return "shader";
    return "";
}

int loader_id(const std::string &loader) {
    if (loader.empty()) return 0;
    const auto found = loader_ids.find(loader);
    if (found != loader_ids.end()) return found->second;
    throw Error("curseforge", "invalid_loader", "CurseForge 加载器筛选无效。");
}

std::string website_url(const Json &project) {
    const auto links = project.find("links");
    if (links == project.end() || !links->is_object()) return "";
    const auto url = string_at(*links, "websiteUrl");
    static const std::regex allowed(R"(https://(?:www\.)?curseforge\.com/[^[:cntrl:][:space:]?#\\]*)");
    return std::regex_match(url, allowed) ? url : "";
}

std::string asset_url(const Json &asset, const char *key) {
    const auto url = string_at(asset, key);
    static const std::regex allowed(
        R"(https://(?:media|mediafilez|edge)\.forgecdn\.net/[^[:cntrl:][:space:]\\]+)");
    return std::regex_match(url, allowed) ? url : "";
}

Json envelope_data(const Json &response, Json::value_t expected) {
    const auto data = response.find("data");
    if (data == response.end() || data->type() != expected) invalid_response();
    return *data;
}

Json project_record(const std::string &id, const Task &task) {
    request_id(id);
    auto project = envelope_data(fetch_curseforge_json("/v1/mods/" + id, task), Json::value_t::object);
    if (numeric_id(number_at(project, "id")) != id || number_at(project, "gameId") != minecraft)
        invalid_response();
    return project;
}

constexpr std::size_t project_batch_limit = 128;

Json project_records(const Json &ids, const Task &task) {
    if (!ids.is_array() || ids.size() > project_batch_limit)
        throw Error("resources", "invalid_ids", "项目数量超过限制。");
    if (ids.empty()) return Json::array();
    std::set<std::string> expected;
    Json keys = Json::array();
    for (const auto &value : ids) {
        if (!value.is_string()) throw Error("curseforge", "invalid_id", "CurseForge 项目标识无效。");
        const auto id = value.get<std::string>();
        const auto key = request_id(id);
        if (expected.insert(id).second) keys.push_back(key);
    }
    const auto response = fetch_curseforge_json("/v1/mods", task, {{"modIds", std::move(keys)}});
    Json projects = Json::array();
    for (auto &raw : envelope_data(response, Json::value_t::array)) {
        if (number_at(raw, "gameId") == minecraft &&
            expected.erase(numeric_id(number_at(raw, "id")))) projects.push_back(std::move(raw));
    }
    return projects;
}

void add_unique(Json &array, const std::string &value) {
    if (!value.empty() && std::find(array.begin(), array.end(), value) == array.end())
        array.push_back(value);
}

Json category_entry(const Json &category) {
    auto id = numeric_id(number_at(category, "id"));
    auto label = string_at(category, "name");
    if (id.empty() || label.empty() || flag_at(category, "isClass")) return Json();
    return {{"name", std::move(id)}, {"label", std::move(label)},
            {"order", number_at(category, "displayIndex")}};
}

Json normalize_project(const Json &project) {
    const auto id = numeric_id(number_at(project, "id"));
    if (id.empty() || number_at(project, "gameId") != minecraft) return Json();
    const auto summary = string_at(project, "summary");
    std::string author;
    for (const auto &entry : array_at(project, "authors")) {
        const auto name = string_at(entry, "name");
        if (!name.empty()) {
            if (!author.empty()) author += ", ";
            author += name;
        }
    }
    Json versions = Json::array(), tags = Json::array(), labels = Json::array(), gallery = Json::array();
    for (const auto &entry : array_at(project, "latestFilesIndexes"))
        add_unique(versions, string_at(entry, "gameVersion"));
    for (const auto &entry : array_at(project, "categories")) {
        auto category = category_entry(entry);
        if (category.is_object()) {
            tags.push_back(category["name"]);
            labels.push_back(category["label"]);
        }
    }
    for (const auto &asset : array_at(project, "screenshots")) {
        auto image = asset_url(asset, "url");
        if (!image.empty()) gallery.push_back({{"url", std::move(image)}});
    }
    std::string icon;
    const auto logo = project.find("logo");
    if (logo != project.end()) {
        icon = asset_url(*logo, "thumbnailUrl");
        if (icon.empty()) icon = asset_url(*logo, "url");
    }
    return {{"id", id}, {"project_id", id}, {"source", "curseforge"},
            {"project_type", class_name(number_at(project, "classId"))},
            {"title", string_at(project, "name")}, {"slug", string_at(project, "slug")},
            {"description", summary}, {"author", std::move(author)},
            {"icon_url", std::move(icon)}, {"website_url", website_url(project)},
            {"downloads", std::max<std::int64_t>(0, number_at(project, "downloadCount"))},
            {"versions", std::move(versions)}, {"categories", std::move(tags)},
            {"display_categories", std::move(labels)}, {"gallery", std::move(gallery)}};
}

void classify_file_tag(const std::string &value, Json &versions, Json &loaders) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (loader_ids.count(lower)) {
        add_unique(loaders, lower);
        return;
    }
    static const std::regex game_version(
        R"((?:[0-9]+\.[0-9]+[0-9A-Za-z_. -]*|[0-9]{2}w[0-9]{2}[0-9A-Za-z_]*|[ab][0-9]+\.[0-9]+[0-9A-Za-z_. -]*))");
    if (std::regex_match(value, game_version)) add_unique(versions, value);
}

std::string sha1_of(const Json &file) {
    static const std::regex hex("[0-9a-fA-F]{40}");
    for (const auto &entry : array_at(file, "hashes")) {
        auto value = string_at(entry, "value");
        if (number_at(entry, "algo") != 1 || !std::regex_match(value, hex)) continue;
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
    return "";
}

Json normalize_file(const Json &project, const Json &file) {
    const auto id = numeric_id(number_at(file, "id"));
    const auto project_id = numeric_id(number_at(project, "id"));
    auto filename = string_at(file, "fileName");
    if (id.empty() || numeric_id(number_at(file, "modId")) != project_id ||
        number_at(file, "gameId") != minecraft || filename.empty() ||
        filename == "." || filename == ".." || filename.find_first_of("/\\:") != filename.npos ||
        std::any_of(filename.begin(), filename.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return Json();

    Json games = Json::array(), loaders = Json::array();
    for (const auto &tag : array_at(file, "gameVersions"))
        if (tag.is_string()) classify_file_tag(tag.get<std::string>(), games, loaders);
    for (const auto &tag : array_at(file, "sortableGameVersions"))
        classify_file_tag(string_at(tag, "gameVersionName"), games, loaders);
    for (const auto &index : array_at(project, "latestFilesIndexes")) {
        if (numeric_id(number_at(index, "fileId")) != id) continue;
        add_unique(games, string_at(index, "gameVersion"));
        const auto loader = number_at(index, "modLoader");
        for (const auto &entry : loader_ids)
            if (entry.second == loader) add_unique(loaders, entry.first);
    }

    const auto channel = number_at(file, "releaseType");
    const auto size = number_at(file, "fileLength", -1);
    const auto status = number_at(file, "fileStatus");
    const auto available = flag_at(file, "isAvailable") && !flag_at(file, "isServerPack") &&
        (status == 4 || status == 10 || status == 12);
    auto hash = sha1_of(file);
    auto url = string_at(file, "downloadUrl");
    // An explicit opt-out always wins. An omitted permission still requires a
    // valid URL supplied by the authenticated API.
    const auto kind = class_name(number_at(project, "classId"));
    const auto can_download = available && !kind.empty() &&
        flag_at(project, "allowModDistribution", true) &&
        curseforge_download_url(url) && !hash.empty() && size >= 0 &&
        size <= 4LL * 1024 * 1024 * 1024;
    if (!can_download) url.clear();
    auto website = website_url(project);
    if (!website.empty()) {
        if (website.back() == '/') website.pop_back();
        website += "/files/" + id;
    }
    auto version = string_at(file, "displayName");
    if (version.empty()) version = filename;
    return {{"source", "curseforge"}, {"id", id}, {"version_id", id}, {"project_id", project_id},
            {"project_type", kind}, {"slug", string_at(project, "slug")},
            {"title", string_at(project, "name")}, {"name", version}, {"version", version},
            {"filename", std::move(filename)}, {"url", std::move(url)}, {"website_url", std::move(website)},
            {"size", std::max<std::int64_t>(0, size)}, {"sha1", std::move(hash)},
            {"game_versions", std::move(games)}, {"loaders", std::move(loaders)},
            {"release_type", channel == 3 ? "alpha" : channel == 2 ? "beta" : "release"},
            {"published", string_at(file, "fileDate")}, {"dependencies", curseforge_dependencies(file)},
            {"available", available}, {"downloadable", can_download}};
}

Json versions_page(const Json &project, const std::string &game, const std::string &loader,
                   int offset, const Task &task) {
    if (offset < 0 || offset >= max_results)
        throw Error("curseforge", "invalid_offset", "CurseForge 分页位置无效。");
    const auto page_size = std::min(50, max_results - offset);
    auto path = "/v1/mods/" + numeric_id(number_at(project, "id")) + "/files?index=" +
        std::to_string(offset) + "&pageSize=" + std::to_string(page_size);
    if (!game.empty()) path += "&gameVersion=" + url_encode(game);
    if (!loader.empty()) path += "&modLoaderType=" + std::to_string(loader_id(loader));
    const auto response = fetch_curseforge_json(path, task);
    const auto raw_files = envelope_data(response, Json::value_t::array);
    Json files = Json::array();
    for (const auto &raw : raw_files) {
        auto file = normalize_file(project, raw);
        if (file.is_object()) files.push_back(std::move(file));
    }
    const auto page = response.find("pagination");
    if (page == response.end() || !page->is_object() || raw_files.size() > std::size_t(page_size))
        invalid_response();
    const auto next = offset + static_cast<int>(raw_files.size());
    const auto total = std::clamp<std::int64_t>(number_at(*page, "totalCount"), 0, max_results);
    return {{"files", std::move(files)}, {"next_offset", next > offset && next < total ? Json(next) : Json()}};
}
} // namespace

Json curseforge_dependencies(const Json &raw_file) {
    Json dependencies = Json::array();
    static const std::map<std::int64_t, std::string> relations{
        {1, "embedded"}, {2, "optional"}, {3, "required"}, {5, "incompatible"}};
    for (const auto &entry : array_at(raw_file, "dependencies")) {
        const auto id = numeric_id(number_at(entry, "modId"));
        const auto relation = relations.find(number_at(entry, "relationType"));
        if (!id.empty() && relation != relations.end())
            dependencies.push_back({{"project_id", id}, {"dependency_type", relation->second}});
    }
    return dependencies;
}

Json curseforge_search(const Json &query, const Task &task) {
    const auto kind = query.value("type", "mod");
    const auto category = query.value("category", "");
    const auto game = query.value("version", "");
    const auto loader = kind == "mod" ? loader_id(query.value("loader", "")) : 0;
    const auto search = query.value("query", "");
    const auto sort = query.value("sort", "relevance");
    const auto offset = std::clamp(query.value("offset", 0), 0, max_results - 20);
    const auto group = class_id(kind);
    auto path = "/v1/mods/search?gameId=" + std::to_string(minecraft) + "&classId=" +
        std::to_string(group) + "&index=" + std::to_string(offset) + "&pageSize=20";
    if (!category.empty()) path += "&categoryId=" + std::to_string(request_id(category));
    if (!game.empty()) path += "&gameVersion=" + url_encode(game);
    if (loader) path += "&modLoaderType=" + std::to_string(loader);
    if (!search.empty()) path += "&searchFilter=" + url_encode(search);
    const int order = sort == "downloads" ? 6 : sort == "updated" ? 3 : sort == "newest" ? 11 : 0;
    if (order) path += "&sortField=" + std::to_string(order) + "&sortOrder=desc";
    const auto response = fetch_curseforge_json(path, task);
    Json hits = Json::array();
    for (const auto &project : envelope_data(response, Json::value_t::array)) {
        if (number_at(project, "classId") != group) continue;
        auto normalized = normalize_project(project);
        if (normalized.is_object()) hits.push_back(std::move(normalized));
    }
    const auto page = response.find("pagination");
    if (page == response.end() || !page->is_object()) invalid_response();
    return {{"hits", std::move(hits)}, {"offset", offset}, {"limit", 20},
            {"total_hits", std::clamp<std::int64_t>(number_at(*page, "totalCount"), 0, max_results)}};
}

Json curseforge_categories(const std::string &kind, const Task &task) {
    const auto group = class_id(kind);
    const auto response = fetch_curseforge_json("/v1/categories?gameId=" + std::to_string(minecraft) +
                                               "&classId=" + std::to_string(group), task);
    Json categories = Json::array();
    for (const auto &raw : envelope_data(response, Json::value_t::array)) {
        if (number_at(raw, "gameId") != minecraft || number_at(raw, "classId") != group) continue;
        auto entry = category_entry(raw);
        if (entry.is_object()) categories.push_back(std::move(entry));
    }
    std::stable_sort(categories.begin(), categories.end(), [](const Json &a, const Json &b) {
        return a["order"] == b["order"] ? a["label"] < b["label"] : a["order"] < b["order"];
    });
    return categories;
}

Json curseforge_project(const std::string &project, const Task &task) {
    return normalize_project(project_record(project, task));
}

Json curseforge_projects(const Json &ids, const Task &task) {
    Json projects = Json::array();
    for (const auto &raw : project_records(ids, task))
        projects.push_back(normalize_project(raw));
    return projects;
}

Json curseforge_pack_files(const Json &refs, const Task &task) {
    constexpr std::size_t batch_size = 100;
    if (!refs.is_array() || refs.size() > 10000)
        throw Error("resources", "invalid_ids", "项目数量超过限制。");
    std::map<std::string, std::string> expected;
    for (const auto &ref : refs) {
        const auto project = string_at(ref, "project_id");
        const auto file = string_at(ref, "file_id");
        request_id(project);
        request_id(file);
        const auto inserted = expected.emplace(file, project);
        if (!inserted.second && inserted.first->second != project)
            throw Error("curseforge", "invalid_id", "CurseForge 项目标识无效。");
    }

    std::map<std::string, Json> files;
    std::set<std::string> needed_projects;
    for (auto it = expected.begin(); it != expected.end();) {
        task.check();
        Json ids = Json::array();
        std::set<std::string> requested;
        while (it != expected.end() && ids.size() < batch_size) {
            ids.push_back(request_id(it->first));
            requested.insert(it->first);
            ++it;
        }
        const auto response = fetch_curseforge_json("/v1/mods/files", task,
                                                     {{"fileIds", std::move(ids)}});
        for (auto &file : envelope_data(response, Json::value_t::array)) {
            auto id = numeric_id(number_at(file, "id"));
            if (!requested.count(id) || number_at(file, "gameId") != minecraft) continue;
            const auto project = numeric_id(number_at(file, "modId"));
            if (project != expected.at(id)) continue;
            if (!files.emplace(std::move(id), std::move(file)).second) invalid_response();
            needed_projects.insert(project);
        }
    }

    std::map<std::string, Json> projects;
    for (auto it = needed_projects.begin(); it != needed_projects.end();) {
        task.check();
        Json ids = Json::array();
        while (it != needed_projects.end() && ids.size() < batch_size) ids.push_back(*it++);
        for (auto &project : project_records(ids, task))
            projects.emplace(numeric_id(number_at(project, "id")), std::move(project));
    }

    Json resolved = Json::array();
    for (const auto &ref : refs) {
        const auto file = files.find(string_at(ref, "file_id"));
        const auto project = projects.find(string_at(ref, "project_id"));
        if (file == files.end() || project == projects.end()) continue;
        auto descriptor = normalize_file(project->second, file->second);
        if (descriptor.is_object()) resolved.push_back(std::move(descriptor));
    }
    return resolved;
}

Json curseforge_versions_page(const std::string &project, const std::string &game,
                              const std::string &loader, int offset, const Task &task) {
    return versions_page(project_record(project, task), game, loader, offset, task);
}

Json curseforge_versions(const std::string &project, const std::string &game,
                         const std::string &loader, const Task &task) {
    const auto record = project_record(project, task);
    Json files = Json::array();
    int offset = 0;
    // Dependency selection asks for a specific Minecraft version and loader.
    // Bound the amount of history read for a single plan.
    for (int page = 0; page < 5; ++page) {
        const auto result = versions_page(record, game, loader, offset, task);
        for (const auto &file : result["files"]) files.push_back(file);
        if (result["next_offset"].is_null()) break;
        offset = result["next_offset"].get<int>();
    }
    return files;
}

Json curseforge_file(const std::string &project, const std::string &file, const Task &task) {
    request_id(file);
    const auto record = project_record(project, task);
    const auto response = fetch_curseforge_json("/v1/mods/" + project + "/files/" + file, task);
    auto normalized = normalize_file(record, envelope_data(response, Json::value_t::object));
    if (!normalized.is_object() || normalized["id"] != file) invalid_response();
    return normalized;
}
} // namespace solar::engine
