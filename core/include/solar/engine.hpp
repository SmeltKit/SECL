// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root. Business capabilities migrated from M2CL. No Qt types cross this
// interface.
#pragma once
#include "core.hpp"
#include <map>
#include <memory>
namespace solar::engine {
using Cancel = std::shared_ptr<std::atomic_bool>;
using Progress = std::function<void(const std::string &, std::int64_t, std::int64_t)>;
struct Task {
    Cancel cancel = std::make_shared<std::atomic_bool>(false);
    Progress progress;
    std::string source = "auto", time_zone;
    void check() const;
    void report(const std::string &, std::int64_t = -1, std::int64_t = -1) const;
};
fs::path checked_path(const fs::path &, const std::string &);
void atomic_write(const fs::path &, const std::string &);
Json read_json(const fs::path &, const Json &fallback = Json::object());
std::string hash_bytes(const std::string &, const std::string &algorithm = "sha512");
std::string hash_file(const fs::path &, const std::string &algorithm = "sha512",
                      const Cancel &cancel = {});
struct ZipEntry {
    std::string name;
    std::uint32_t crc = 0, compressed = 0, size = 0, offset = 0;
    unsigned method = 0;
};
std::vector<ZipEntry> zip_entries(const std::string &);
std::string zip_entry_bytes(const std::string &, const std::string &,
                            size_t limit = 2 * 1024 * 1024);
std::string jar_entry(const fs::path &, const std::string &, size_t limit = 2 * 1024 * 1024);
void extract_zip(const fs::path &, const fs::path &, const Task &task = {});
std::string normalize_arch(std::string);
Json parse_java(const std::string &);
Json probe_java(const fs::path &);
Json scan_java(const std::vector<fs::path> &, bool deep, const Task &task = {}, int depth = 5,
               int budget = 12000);
Json java_legal(const fs::path &);
Json system_memory();
Json recommend_memory(const Json &, const Json &);
void validate_launch_options(const Json &);
Json loader_components(const Json &);
Json scan_instances(const std::vector<fs::path> &, const Json &overrides = Json::object());
Json load_instance_settings(const fs::path &);
void save_instance_settings(const fs::path &, const Json &);
std::string clean_log(const std::string &, const std::vector<std::string> &);
std::vector<std::string> download_candidates(const std::string &, const Task &task = {});
std::string fetch_data(const std::string &, const Task &task = {}, size_t limit = 32 * 1024 * 1024);
Json fetch_json(const std::string &, const Task &task = {});
void download(const Json &descriptor, const fs::path &target, const Task &task = {});
Json resource_search(const Json &, const Task &task = {});
Json resource_tags(const std::string &type, const Task &task = {});
Json resource_categories(const std::string &type, const Task &task = {});
Json plan_pack_install(const Json &instance, const Json &file, const std::string &kind);
Json apply_pack_plan(const Json &plan, const Task &task = {});
Json resource_project(const std::string &, const Task &task = {});
Json resource_versions(const std::string &, const std::string &game = {},
                       const std::string &loader = {}, const Task &task = {});
Json resource_files(const Json &, bool packs = false);
bool resource_compatible(const Json &, const std::string &game, const std::string &loader);
std::string game_channel(const std::string &, const std::string &type = {});
Json version_families(const Json &);
std::string resource_tag_label(const std::string &);
Json inspect_mod(const fs::path &, const std::string &loader = {});
Json inspect_mod_bytes(const std::string &, const std::string &file_name,
                       const std::string &loader = {}, int depth = 0);
Json mod_inventory(const fs::path &);
Json local_mods(const fs::path &game_directory, const std::string &loader = {});
int version_matches(const std::string &range, const std::string &version);
Json check_mods(const Json &instance, const Json &mods, bool allow_unknown = false,
                bool allow_missing = false);
void import_mods(const Json &instance, const std::vector<fs::path> &, bool allow_unknown = false);
void set_mods_enabled(const Json &instance, const std::vector<std::string> &names, bool enabled);
void remove_mods(const Json &instance, const std::vector<std::string> &names);
Json inspect_pack(const fs::path &, const std::string &kind);
Json local_packs(const fs::path &, const std::string &kind);
void import_pack(const fs::path &game, const std::string &kind, const fs::path &source);
void remove_pack(const fs::path &game, const std::string &kind, const std::string &name);
Json shader_support(const Json &instance);
Json compatibility_rules(const fs::path &game);
Json update_compatibility(const fs::path &game, const Task &task = {}, bool force = false);
Json plan_mod_install(const Json &instance, const Json &file, const Task &task = {});
Json select_dependencies(const Json &plan, const Json &selected,
                         const Json &redownload = Json::array(),
                         const Json &enable = Json::array());
Json apply_mod_plan(const Json &plan, const Task &task = {});
Json java_builds(int major, const std::string &vendor, const std::string &os,
                 const std::string &arch, const Task &task = {});
Json java_build_legal(const Json &);
void extract_java_archive(const fs::path &, const fs::path &, const Task &task = {});
Json install_java(const Json &build, const fs::path &directory, const Task &task = {});
Json game_versions(const Task &task = {});
Json loader_versions(const std::string &loader, const std::string &game, const Task &task = {});
std::string maven_path(const std::string &);
Json install_game(const Json &request, const Task &task = {});
Json install_forge(const Json &request, const Json &base, const fs::path &java,
                   const Task &task = {});
void verify_loader_outputs(const Json &, const fs::path &root);
Json optifine_builds(const std::string &game, const Task &task = {});
Json inspect_optifine(const fs::path &);
std::string optifine_compatibility(const Json &, const Json &instance);
Json install_optifine(const Json &request, const Task &task = {});
Json dispatch(const Json &, const Task &task = {});
} // namespace solar::engine
