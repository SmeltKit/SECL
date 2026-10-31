// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <regex>
#include <set>
#include <mutex>
namespace solar::engine {
static std::string id(const std::string &s) {
    if (!std::regex_match(s, std::regex("[A-Za-z0-9_-]{1,160}")))
        throw Error("resources", "invalid_id", "资源项目标识无效。");
    return s;
}
static bool includes(const Json &values, const std::string &s) {
    return values.is_array() && std::find(values.begin(), values.end(), s) != values.end();
}
std::string game_channel(const std::string &v, const std::string &type) {
    static const std::set<std::string> special{
        "20w14infinite", "22w13oneblockatatime", "23w13a_or_b", "24w14potato", "25w14craftmine",
        "26w14a",        "3D Shareware v1.34",   "1.RV-Pre1",   "15w14a",      "2.0"};
    if (special.count(v))
        return "special";
    if (type == "release" || std::regex_match(v, std::regex(R"(^\d+\.\d+(?:\.\d+)*$)")))
        return "release";
    if (type == "old_alpha" || type == "old_beta" ||
        std::regex_search(v, std::regex(R"(^(?:a|b|c|inf-|rd-))")))
        return "historical";
    return "snapshot";
}
Json version_families(const Json &versions) {
    std::set<std::string> seen;
    Json out = Json::array();
    for (auto &v : versions) {
        if (!v.is_string())
            continue;
        auto text = v.get<std::string>();
        if (game_channel(text) != "release")
            continue;
        std::smatch m;
        if (std::regex_search(text, m, std::regex(R"(^(1\.\d+|\d+\.\d+))")) &&
            seen.insert(m[1].str()).second)
            out.push_back(m[1].str());
    }
    return out;
}
std::string resource_tag_label(const std::string &s) {
    static const std::map<std::string, std::string> labels{{"adventure", "冒险"},
                                                           {"audio", "音效"},
                                                           {"blocks", "方块"},
                                                           {"combat", "战斗"},
                                                           {"core-shaders", "核心着色器"},
                                                           {"entities", "实体"},
                                                           {"environment", "环境"},
                                                           {"fonts", "字体"},
                                                           {"gui", "界面"},
                                                           {"items", "物品"},
                                                           {"locale", "语言"},
                                                           {"modded", "模组适配"},
                                                           {"models", "模型"},
                                                           {"simplistic", "简约"},
                                                           {"themed", "主题风格"},
                                                           {"tweaks", "细节调整"},
                                                           {"vanilla-like", "原版风格"},
                                                           {"semi-realistic", "半写实"},
                                                           {"reflections", "反射"},
                                                           {"screenshot", "截图展示"},
                                                           {"cursed", "趣味与整活"},
                                                           {"decoration", "装饰与建筑"},
                                                           {"economy", "经济"},
                                                           {"equipment", "装备"},
                                                           {"food", "食物"},
                                                           {"game-mechanics", "游戏机制"},
                                                           {"library", "前置与类库"},
                                                           {"magic", "魔法"},
                                                           {"management", "管理"},
                                                           {"minigame", "小游戏"},
                                                           {"mobs", "生物"},
                                                           {"optimization", "性能优化"},
                                                           {"social", "社交"},
                                                           {"storage", "存储"},
                                                           {"technology", "科技"},
                                                           {"transportation", "交通"},
                                                           {"utility", "实用工具"},
                                                           {"worldgen", "世界生成"},
                                                           {"atmosphere", "氛围"},
                                                           {"bloom", "泛光"},
                                                           {"cartoon", "卡通"},
                                                           {"colored-lighting", "彩色光照"},
                                                           {"fantasy", "奇幻"},
                                                           {"foliage", "植被"},
                                                           {"high", "高配置"},
                                                           {"medium", "中等配置"},
                                                           {"low", "低配置"},
                                                           {"potato", "极低配置"},
                                                           {"path-tracing", "路径追踪"},
                                                           {"pbr", "PBR 材质"},
                                                           {"realistic", "写实"},
                                                           {"fabric", "Fabric"},
                                                           {"quilt", "Quilt"},
                                                           {"forge", "Forge"},
                                                           {"neoforge", "NeoForge"}};
    auto it = labels.find(s);
    return it == labels.end() ? s : it->second;
}
Json resource_search(const Json &q, const Task &task) {
    auto type = q.value("type", "mod");
    if (type != "mod" && type != "shader" && type != "resourcepack")
        throw Error("resources", "invalid_type", "资源类型无效。");
    Json facets = Json::array({Json::array({"project_type:" + type})});
    for (const auto &pair : std::vector<std::pair<std::string, std::string>>{
             {"version", "versions:"}, {"loader", "categories:"}, {"category", "categories:"}}) {
        auto v = q.value(pair.first, "");
        if (!v.empty())
            facets.push_back(Json::array({pair.second + v}));
    }
    auto sort = q.value("sort", "relevance");
    if (sort != "relevance" && sort != "downloads" && sort != "follows" && sort != "newest" &&
        sort != "updated")
        sort = "relevance";
    return fetch_json(
        "https://api.modrinth.com/v2/search?query=" + url_encode(q.value("query", "")) +
            "&facets=" + url_encode(facets.dump()) + "&index=" + sort +
            "&offset=" + std::to_string(std::max(0, q.value("offset", 0))) + "&limit=20",
        task);
}
Json resource_tags(const std::string &type, const Task &task) {
    auto categories = fetch_json("https://api.modrinth.com/v2/tag/category", task),
         loaders = fetch_json("https://api.modrinth.com/v2/tag/loader", task),
         versions = fetch_json("https://api.modrinth.com/v2/tag/game_version", task);
    Json filtered = Json::array();
    for (auto &c : categories)
        if (c.value("project_type", "") == type) {
            c["label"] = resource_tag_label(c.value("name", ""));
            filtered.push_back(c);
        }
    for (auto &v : versions)
        v["channel"] = game_channel(v.value("version", ""), v.value("version_type", ""));
    std::map<std::string, int> rank{{"forge", 0}, {"neoforge", 1}, {"fabric", 2}, {"quilt", 3}};
    std::stable_sort(loaders.begin(), loaders.end(), [&](const Json &a, const Json &b) {
        auto an = a.value("name", ""), bn = b.value("name", "");
        return (rank.count(an) ? rank[an] : 99) < (rank.count(bn) ? rank[bn] : 99);
    });
    return {{"categories", filtered}, {"loaders", loaders}, {"game_versions", versions}};
}
Json resource_categories(const std::string &type, const Task &task) {
    static std::mutex mutex;
    static Json cached;
    static std::int64_t fetched = 0;
    Json categories;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (now() - fetched < 3600 && cached.is_array())
            categories = cached;
    }
    if (!categories.is_array()) {
        categories = fetch_json("https://api.modrinth.com/v2/tag/category", task);
        std::lock_guard<std::mutex> lock(mutex);
        cached = categories;
        fetched = now();
    }
    Json result = Json::array();
    for (auto category : categories)
        if (category.value("project_type", "") == type) {
            category["label"] = resource_tag_label(category.value("name", ""));
            result.push_back(category);
        }
    return result;
}
Json resource_project(const std::string &project, const Task &task) {
    return fetch_json("https://api.modrinth.com/v2/project/" + id(project), task);
}
Json resource_versions(const std::string &project, const std::string &game,
                       const std::string &loader, const Task &task) {
    std::string url = "https://api.modrinth.com/v2/project/" + id(project) + "/version?";
    if (!game.empty())
        url += "game_versions=" + url_encode(Json::array({game}).dump()) + "&";
    if (!loader.empty())
        url += "loaders=" + url_encode(Json::array({loader}).dump());
    return resource_files(fetch_json(url, task), true);
}
Json resource_files(const Json &versions, bool packs) {
    Json out = Json::array();
    for (auto &v : versions) {
        auto files = v.value("files", Json::array());
        for (auto &f : files) {
            auto name = f.value("filename", "");
            if (name.empty() || name.find_first_of("/\\:") != name.npos || name == "." ||
                name == ".." || name.find('\0') != name.npos)
                continue;
            if (!packs && fs::path(name).extension() != ".jar")
                continue;
            auto hashes = f.value("hashes", Json::object());
            if (!std::regex_match(hashes.value("sha512", ""), std::regex("[0-9a-fA-F]{128}")))
                continue;
            out.push_back({{"version_id", v.value("id", "")},
                           {"project_id", v.value("project_id", "")},
                           {"name", v.value("name", "")},
                           {"version", v.value("version_number", "")},
                           {"filename", name},
                           {"url", f.value("url", "")},
                           {"size", f.value("size", 0LL)},
                           {"sha512", hashes.value("sha512", "")},
                           {"sha1", hashes.value("sha1", "")},
                           {"game_versions", v.value("game_versions", Json::array())},
                           {"loaders", v.value("loaders", Json::array())},
                           {"release_type", v.value("version_type", "release")},
                           {"published", v.value("date_published", "")},
                           {"dependencies", v.value("dependencies", Json::array())}});
            if (f.value("primary", false))
                break;
        }
    }
    return out;
}
bool resource_compatible(const Json &f, const std::string &game, const std::string &loader) {
    if (!includes(f.value("game_versions", Json::array()), game))
        return false;
    auto loaders = f.value("loaders", Json::array());
    return includes(loaders, loader) || (loader == "quilt" && includes(loaders, "fabric"));
}
} // namespace solar::engine
