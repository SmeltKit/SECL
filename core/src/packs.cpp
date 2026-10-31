// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <regex>
namespace solar::engine {
static std::string folder(const std::string &kind) {
    if (kind == "shader")
        return "shaderpacks";
    if (kind == "resourcepack")
        return "resourcepacks";
    throw Error("packs", "invalid_kind", "资源类型无效。");
}
Json inspect_pack(const fs::path &p, const std::string &kind) {
    folder(kind);
    if (fs::is_symlink(p))
        throw Error("packs", "symlink", "资源包不能是符号链接。");
    bool directory = fs::is_directory(p);
    std::vector<std::string> names;
    std::string bytes;
    std::uint64_t size = 0;
    if (directory) {
        for (auto &entry : fs::recursive_directory_iterator(p)) {
            if (entry.is_symlink())
                throw Error("packs", "symlink", "资源目录包含符号链接。");
            if (entry.is_regular_file()) {
                names.push_back(fs::relative(entry.path(), p).generic_u8string());
                size += entry.file_size();
                if (names.size() > 20000 || size > 2ULL * 1024 * 1024 * 1024)
                    throw Error("packs", "size_limit", "资源目录超过限制。");
            }
        }
    } else {
        bytes = read_file(p, 512 * 1024 * 1024);
        for (auto &e : zip_entries(bytes)) {
            if (e.name.empty() || e.name.back() == '/')
                continue;
            checked_path(fs::temp_directory_path(), e.name);
            names.push_back(e.name);
            size += e.size;
            if (size > 2ULL * 1024 * 1024 * 1024)
                throw Error("packs", "size_limit", "资源包解压体积超过限制。");
        }
    }
    auto read = [&](const std::string &name) {
        return directory
                   ? (fs::exists(p / name) ? read_file(p / name, 2 * 1024 * 1024) : std::string())
                   : zip_entry_bytes(bytes, name);
    };
    Json out{{"filename", p.filename().u8string()},
             {"name", p.stem().u8string()},
             {"directory", directory},
             {"sha512", directory ? "" : hash_file(p)},
             {"format", 0},
             {"description", ""},
             {"version", ""}};
    if (kind == "shader") {
        bool found = false;
        for (auto &name : names)
            if (name.rfind("shaders/", 0) == 0 &&
                std::regex_search(name, std::regex(R"(\.(vsh|fsh|glsl|csh)$)")))
                found = true;
        if (!found)
            throw Error("packs", "invalid_shader", "没有找到 shaders/ 目录下的着色器。");
    } else {
        auto data = Json::parse(read("pack.mcmeta"), nullptr, false);
        if (!data.is_object() || !data.contains("pack"))
            throw Error("packs", "invalid_pack", "资源包缺少有效的 pack.mcmeta。");
        auto pack = data["pack"];
        out["format"] = pack.value("pack_format", 0);
        auto description = pack.value("description", Json(""));
        out["description"] =
            description.is_string() ? description.get<std::string>() : description.dump();
    }
    out["valid"] = true;
    out["has_icon"] = std::find(names.begin(), names.end(), "pack.png") != names.end();
    return out;
}
Json local_packs(const fs::path &game, const std::string &kind) {
    Json out = Json::array();
    auto dir = game / folder(kind);
    if (!fs::is_directory(dir))
        return out;
    for (auto &entry : fs::directory_iterator(dir))
        if (entry.is_directory() || entry.path().extension() == ".zip") {
            try {
                auto p = inspect_pack(entry.path(), kind);
                auto hash = p.value("sha512", "");
                auto current = game / ".solar-pack-metadata" / (hash + ".json");
                auto meta = read_json(fs::exists(current) ? current :
                                     game / ".m2cl-pack-metadata" / (hash + ".json"));
                for (auto key : {"name", "description", "version"})
                    if (meta.contains(key))
                        p[key] = meta[key];
                out.push_back(p);
            } catch (const Error &e) {
                out.push_back(
                    {{"filename", entry.path().filename().u8string()}, {"problem", e.what()}});
            }
        }
    return out;
}
Json plan_pack_install(const Json &instance, const Json &file, const std::string &kind) {
    folder(kind);
    const auto name = file.value("filename", "");
    if (name.empty() || name.find_first_of("/\\:\0", 0, 4) != name.npos ||
        fs::u8path(name).extension() != ".zip")
        throw Error("packs", "invalid_filename", "资源文件必须是有效的 ZIP 文件名。");
    if (!std::regex_match(file.value("sha512", ""), std::regex("[a-fA-F0-9]{128}")))
        throw Error("packs", "hash_required", "资源文件缺少有效的 SHA-512 校验值。");
    const auto versions = file.value("game_versions", Json::array());
    if (std::find(versions.begin(), versions.end(), instance.value("version", "")) == versions.end())
        throw Error("packs", "incompatible_version", "资源文件不支持目标实例的游戏版本。");
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto target = checked_path(game, folder(kind) + "/" + name);
    std::string old_hash;
    if (fs::exists(target)) {
        if (!fs::is_regular_file(target) || fs::is_symlink(target))
            throw Error("packs", "target_exists", "同名目标不是普通文件，不能替换。");
        old_hash = hash_file(target);
    }
    Json plan{{"target", instance}, {"file", file}, {"kind", kind},
              {"expected_hash", old_hash}, {"reuse", old_hash == file.value("sha512", "")},
              {"replace", false}};
    if (kind == "shader")
        plan["support"] = shader_support(instance);
    return plan;
}
Json apply_pack_plan(const Json &plan, const Task &task) {
    const auto instance = plan.at("target"), file = plan.at("file");
    const auto kind = plan.at("kind").get<std::string>();
    auto fresh = plan_pack_install(instance, file, kind);
    if (fresh.at("expected_hash") != plan.at("expected_hash"))
        throw Error("packs", "plan_stale", "资源目录在确认后发生变化，请重新确认。");
    if (fresh.value("reuse", false) && !plan.value("redownload", false))
        return {{"reused", true}, {"installed", file.at("filename")}};
    if (!fresh.value("expected_hash", "").empty() && !fresh.value("reuse", false) &&
        !plan.value("replace", false))
        throw Error("packs", "replacement_not_confirmed", "已有同名资源，请明确确认替换。");
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto destination = checked_path(game, folder(kind) + "/" + file.at("filename").get<std::string>());
    auto stage = checked_path(game, ".solar-pack-stage/" + random_url_token());
    auto payload = stage / file.at("filename").get<std::string>();
    auto backup = stage / "previous.zip";
    bool moved = false, installed = false;
    try {
        download(file, payload, task);
        inspect_pack(payload, kind);
        task.check();
        fresh = plan_pack_install(instance, file, kind);
        if (fresh.at("expected_hash") != plan.at("expected_hash"))
            throw Error("packs", "plan_stale", "下载期间资源目录发生变化，请重新确认。");
        fs::create_directories(destination.parent_path());
        if (fs::exists(destination)) {
            fs::rename(destination, backup);
            moved = true;
        }
        fs::rename(payload, destination);
        installed = true;
        atomic_write(game / ".solar-pack-metadata" / (file.at("sha512").get<std::string>() + ".json"),
                     Json{{"name", file.value("title", file.value("name", ""))},
                          {"version", file.value("version", "")},
                          {"description", file.value("description", "")},
                          {"project_id", file.value("project_id", "")}}.dump());
        std::error_code ec;
        fs::remove_all(stage, ec);
        return {{"installed", file.at("filename")}, {"reused", false}};
    } catch (...) {
        std::error_code ec;
        if (installed) fs::remove(destination, ec);
        if (moved) fs::rename(backup, destination, ec);
        fs::remove_all(stage, ec);
        throw;
    }
}
void import_pack(const fs::path &game, const std::string &kind, const fs::path &source) {
    auto p = inspect_pack(source, kind);
    auto dest = checked_path(game, folder(kind) + "/" + source.filename().u8string());
    if (fs::exists(dest)) {
        if (!fs::is_directory(source) && hash_file(source) == hash_file(dest))
            return;
        throw Error("packs", "target_exists", "已有同名但不同内容的资源。");
    }
    auto temporary = dest;
    temporary += ".import-" + random_url_token();
    fs::create_directories(dest.parent_path());
    try {
        if (fs::is_directory(source))
            fs::copy(source, temporary, fs::copy_options::recursive);
        else
            fs::copy_file(source, temporary);
        inspect_pack(temporary, kind);
        if (!fs::is_directory(source) && hash_file(temporary) != p.at("sha512"))
            throw Error("packs", "source_changed", "导入过程中资源发生变化。");
        fs::rename(temporary, dest);
    } catch (...) {
        std::error_code ec;
        fs::remove_all(temporary, ec);
        throw;
    }
}
void remove_pack(const fs::path &game, const std::string &kind, const std::string &name) {
    if (name.find('/') != name.npos)
        throw Error("packs", "invalid_filename", "资源文件名无效。");
    auto source = checked_path(game, folder(kind) + "/" + name);
    move_to_trash(source);
}
Json shader_support(const Json &i) {
    auto mods =
        local_mods(fs::u8path(i.at("game_directory").get<std::string>()), i.value("loader", ""));
    bool iris = false, optifine = !i.value("optifine", Json::object()).empty();
    Json components = Json::array();
    for (auto &m : mods)
        if (m.value("enabled", true))
            for (auto &id : m.value("ids", Json::array())) {
                if (id == "iris")
                    iris = true;
                if (id == "optifine")
                    optifine = true;
                if (id == "iris" || id == "optifine" || id == "sodium")
                    components.push_back(m.value("name", "") + " " + m.value("version", ""));
            }
    auto check = check_mods(i, mods, true, false);
    return {{"available", (iris || optifine) && check["errors"].empty()},
            {"components", components},
            {"conflicts", check["errors"]},
            {"warnings", check["warnings"]}};
}
static const char *compatibility_source =
    "https://raw.githubusercontent.com/QuiltMC/quiltmc.org/main/src/data/"
    "incompatible-mods.json";
static Json builtin_rules() {
    static const char *raw =
#include "quilt-compatibility.inc"
        ;
    return Json::parse(raw);
}
Json compatibility_rules(const fs::path &game) {
    auto out = read_json(game / ".solar-quilt-compatibility.json");
    if (out.value("source", "") == compatibility_source && out.contains("rules") &&
        out["rules"].is_array() && out["rules"].size() <= 2000 &&
        out.value("fetched", 0LL) <= now() + 300)
        return out;
    return {{"rules", builtin_rules()},
            {"source", compatibility_source},
            {"cached", false},
            {"fetched", 0}};
}
Json update_compatibility(const fs::path &game, const Task &task, bool force) {
    auto attempt = game / ".solar-quilt-compatibility-attempt.json";
    auto last = read_json(attempt).value("last_attempt", 0LL);
    if (!force && now() - last < 86400)
        return compatibility_rules(game);
    atomic_write(attempt, Json{{"last_attempt", now()}}.dump());
    try {
        auto data = fetch_json(compatibility_source, task);
        if (!data.is_array() || data.empty() || data.size() > 2000)
            throw Error("compatibility", "invalid_list", "官方兼容列表格式变化，保留已有规则。");
        for (auto &r : data)
            if (r.value("name", "").empty() || !r.contains("ids") || r["ids"].empty() ||
                r.value("name", "").size() > 256)
                throw Error("compatibility", "invalid_rule", "官方兼容列表格式无效。");
        Json out{{"source", compatibility_source},
                 {"fetched", now()},
                 {"cached", true},
                 {"rules", data}};
        atomic_write(game / ".solar-quilt-compatibility.json", out.dump());
        return out;
    } catch (const Error &e) {
        auto cached = compatibility_rules(game);
        cached["update_error"] = e.json();
        return cached;
    }
}
} // namespace solar::engine
