// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include "solar/accounts.hpp"
namespace solar::engine {
Json dispatch(const Json &r, const Task &t) {
    const auto op = r.at("op").get<std::string>();
    auto path = [&](const char *key) { return fs::u8path(r.at(key).get<std::string>()); };
    auto paths = [&](const char *key) {
        std::vector<fs::path> out;
        for (auto &p : r.at(key))
            out.push_back(fs::u8path(p.get<std::string>()));
        return out;
    };
    auto names = [&](const char *key) { return r.at(key).get<std::vector<std::string>>(); };
    if (op == "capabilities")
        return {"accounts.list",
                "accounts.save",
                "platform", "build.info",
                "java.scan",
                "java.probe",
                "java.legal",
                "java.releases",
                "java.catalog", "java.roots",
                "java.builds",
                "java.build_legal",
                "java.install",
                "memory.snapshot",
                "memory.recommend",
                "instances.scan",
                "worlds.list", "worlds.open", "worlds.import", "worlds.export", "worlds.copy",
                "worlds.backup", "worlds.backups", "worlds.restore", "worlds.remove",
                "instance.backups.list", "instance.backups.create", "instance.backups.restore",
                "screenshots.list", "screenshots.inspect", "screenshots.open", "screenshots.export", "screenshots.remove",
                "instance.inventory", "instance.logs", "mods.enrich", "shaders.plan", "path.open",
                "settings.load",
                "settings.save",
                "resource.search",
                "resource.tags",
                "resource.categories",
                "resource.games",
                "resource.open",
                "resource.project",
                "resource.projects",
                "resource.versions",
                "resource.version_families",
                "updates.check", "updates.plan", "updates.apply", "updates.lock",
                "mods.list",
                "mods.inspect",
                "mods.check",
                "mods.import",
                "mods.enable",
                "mods.remove",
                "mods.plan",
                "mods.select",
                "mods.apply",
                "packs.list",
                "packs.inspect",
                "packs.import",
                "packs.remove",
                "packs.plan",
                "packs.apply",
                "shaders.support",
                "quilt.rules",
                "quilt.update",
                "modpacks.inspect", "modpacks.plan", "modpacks.apply",
                "game.versions",
                "game.install",
                "loader.versions",
                "optifine.builds",
                "optifine.inspect",
                "optifine.compatibility",
                "optifine.install"};
    if (op == "accounts.list")
        return load_accounts(path("directory"));
    if (op == "accounts.save") {
        save_accounts(path("directory"), r.at("accounts"), r.value("selected", ""));
        return {{"saved", true}};
    }
    if (op == "build.info")
        return {{"curseforge_key_embedded", detail::curseforge_key_embedded()}};
    if (op == "platform") {
        auto p = platform();
        return {{"os", p.os}, {"arch", p.arch}, {"version", p.version}};
    }
    if (op == "java.scan")
        return scan_java(paths("roots"), r.value("deep", false), t, r.value("depth", 5),
                         r.value("budget", 12000));
    if (op == "java.probe")
        return probe_java(path("path"));
    if (op == "java.legal")
        return java_legal(path("path"));
    if (op == "java.releases")
        return java_releases(t);
    if (op == "java.catalog") return java_catalog(r,t);
    if (op == "java.roots") return java_scan_roots();
    if (op == "java.builds")
        return java_builds(r.at("major"), r.at("vendor"), r.at("os"), r.at("arch"), t);
    if (op == "java.build_legal")
        return java_build_legal(r.at("build"));
    if (op == "java.install")
        return install_java(r.at("build"), path("directory"), t);
    if (op == "memory.snapshot")
        return system_memory();
    if (op == "memory.recommend")
        return recommend_memory(r.value("snapshot", system_memory()), r.at("context"));
    if (op == "instances.scan")
        return scan_instances(paths("roots"), r.value("overrides", Json::object()));
    if (op.rfind("worlds.", 0) == 0) return worlds_command(r,t);
    if (op.rfind("instance.backups.", 0) == 0) return instance_backups_command(r,t);
    if (op.rfind("screenshots.", 0) == 0) return screenshots_command(r,t);
    if (op == "instance.inventory")
        return instance_inventory(r.at("instance"), r.at("kind"), r.value("fresh", false));
    if (op == "instance.logs")
        return instance_logs(r.at("instance"), t);
    if (op == "mods.enrich")
        return enrich_mod_inventory(r.at("instance"), t);
    if (op == "shaders.plan")
        return shader_support_plan(r.at("instance"), t);
    if (op == "path.open") {
        auto file = path("path");
        if (!fs::exists(file)) throw Error("files", "missing_path", "路径不存在。");
        auto name = file.filename().u8string();
        if (!fs::is_directory(file) && name.rfind("LICENSE",0)!=0 && name!="ASSEMBLY_EXCEPTION" &&
            file.extension()!=".txt" && file.extension()!=".md" && file.extension()!=".log")
            throw Error("files", "unsupported_open", "只能打开目录、许可证或日志文本。");
        open_browser(fs::canonical(file).u8string());
        return {{"opened",true}};
    }
    if (op == "settings.load")
        return load_instance_settings(path("directory"));
    if (op == "settings.save") {
        save_instance_settings(path("directory"), r.at("settings"));
        return {{"saved", true}};
    }
    const bool curseforge = r.value("source", "") == "curseforge";
    if (op == "resource.search")
        return curseforge ? curseforge_search(r, t) : resource_search(r, t);
    if (op == "resource.tags")
        return resource_tags(r.value("type", "mod"), t);
    if (op == "resource.categories") {
        const auto kind = r.value("type", "mod");
        return curseforge ? curseforge_categories(kind, t) : resource_categories(kind, t);
    }
    if (op == "resource.games") {
        auto versions = fetch_json("https://api.modrinth.com/v2/tag/game_version", t);
        for (auto &version : versions)
            version["channel"] = game_channel(version.value("version", ""),
                                              version.value("version_type", ""));
        return versions;
    }
    if (op == "resource.open") {
        auto url = r.at("url").get<std::string>();
        if (url.rfind("https://", 0) != 0 || url.find_first_of("\r\n") != url.npos)
            throw Error("resources", "invalid_url", "只能打开 HTTPS 项目链接。");
        open_browser(url);
        return {{"opened", true}};
    }
    if (op == "resource.project")
        return curseforge ? curseforge_project(r.at("project"), t) : resource_project(r.at("project"), t);
    if (op == "resource.projects") {
        const auto &ids = r.at("ids");
        if (!ids.is_array() || ids.size() > 128)
            throw Error("resources", "invalid_ids", "项目数量超过限制。");
        if (curseforge) return curseforge_projects(ids, t);
        return fetch_json("https://api.modrinth.com/v2/projects?ids=" + url_encode(ids.dump()), t);
    }
    if (op == "resource.versions") {
        const auto &project = r.at("project");
        const auto game = r.value("game", "");
        const auto loader = r.value("loader", "");
        if (curseforge && r.contains("offset"))
            return curseforge_versions_page(project, game, loader, r.at("offset").get<int>(), t);
        return curseforge ? curseforge_versions(project, game, loader, t) :
                           resource_versions(project, game, loader, t);
    }
    if (op == "resource.version_families")
        return version_families(r.at("versions"));
    if (op == "mods.list")
        return local_mods(path("directory"), r.value("loader", ""));
    if (op == "updates.lock")
        return set_resource_locks(r.at("instance"), r.at("kind"), r.at("files"), r.at("locked").get<bool>());
    if (op == "updates.check")
        return check_resource_updates(r.at("instance"), r.at("kind"), r.value("channel", "release"), t);
    if (op == "updates.plan")
        return plan_resource_updates(r.at("report"), r.at("selected"), t);
    if (op == "updates.apply")
        return apply_resource_updates(r.at("plan"), t);
    if (op == "mods.inspect")
        return inspect_mod(path("path"), r.value("loader", ""));
    if (op == "mods.check")
        return check_mods(r.at("instance"), r.at("mods"), r.value("allow_unknown", false),
                          r.value("allow_missing", false));
    if (op == "mods.import") {
        import_mods(r.at("instance"), paths("files"), r.value("allow_unknown", false));
        return {{"imported", true}};
    }
    if (op == "mods.enable") {
        set_mods_enabled(r.at("instance"), names("files"), r.at("enabled"));
        return {{"updated", true}};
    }
    if (op == "mods.remove") {
        remove_mods(r.at("instance"), names("files"));
        return {{"removed", true}};
    }
    if (op == "mods.plan")
        return plan_mod_install(r.at("instance"), r.at("file"), t);
    if (op == "mods.select")
        return select_dependencies(r.at("plan"), r.at("selected"),
                                   r.value("redownload", Json::array()),
                                   r.value("enable", Json::array()));
    if (op == "mods.apply")
        return apply_mod_plan(r.at("plan"), t);
    if (op == "packs.list")
        return local_packs(path("directory"), r.at("kind"));
    if (op == "packs.inspect")
        return inspect_pack(path("path"), r.at("kind"));
    if (op == "packs.import") {
        import_pack(path("directory"), r.at("kind"), path("path"));
        return {{"imported", true}};
    }
    if (op == "packs.remove") {
        remove_pack(path("directory"), r.at("kind"), r.at("name"));
        return {{"removed", true}};
    }
    if (op == "packs.plan")
        return plan_pack_install(r.at("instance"), r.at("file"), r.at("kind"));
    if (op == "packs.apply")
        return apply_pack_plan(r.at("plan"), t);
    if (op == "shaders.support")
        return shader_support(r.at("instance"));
    if (op == "quilt.rules")
        return compatibility_rules(path("directory"));
    if (op == "quilt.update")
        return update_compatibility(path("directory"), t, r.value("force", false));
    if (op == "modpacks.inspect") return inspect_modpack(path("path"),t);
    if (op == "modpacks.plan") return plan_modpack(r,t);
    if (op == "modpacks.apply") return apply_modpack(r.at("plan"),t);
    if (op == "game.versions")
        return game_versions(t);
    if (op == "game.install")
        return install_game(r.at("request"), t);
    if (op == "loader.versions")
        return loader_versions(r.at("loader"), r.at("game"), t);
    if (op == "optifine.builds")
        return optifine_builds(r.at("game"), t);
    if (op == "optifine.inspect")
        return inspect_optifine(path("path"));
    if (op == "optifine.compatibility")
        return {{"problem", optifine_compatibility(r.at("build"), r.at("instance"))}};
    if (op == "optifine.install")
        return install_optifine(r.at("request"), t);
    throw Error("engine", "unknown_operation", "不支持的核心操作：" + op);
}
} // namespace solar::engine
