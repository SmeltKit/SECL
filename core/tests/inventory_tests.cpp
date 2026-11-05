// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include "../src/mod_scan_cache.hpp"
#include <fstream>
#include <iostream>
#include <zlib.h>

using namespace solar;
using namespace solar::engine;

static int checks = 0, failures = 0;
static void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static void rejects(const std::function<void()> &operation, const char *message) {
    bool rejected = false;
    try { operation(); } catch (const std::exception &) { rejected = true; }
    check(rejected, message);
}
static void run(const char *name, const std::function<void()> &test) {
    try { test(); }
    catch (const std::exception &e) {
        ++failures;
        std::cerr << "FAIL " << name << ": " << e.what() << '\n';
    }
}
static void word(std::string &bytes, unsigned value, int count) {
    while (count--) { bytes.push_back(char(value & 255)); value >>= 8; }
}
static std::string zip(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string out, central;
    for (const auto &file : files) {
        auto offset = out.size();
        auto crc = crc32(0, reinterpret_cast<const Bytef *>(file.second.data()), unsigned(file.second.size()));
        word(out, 0x04034b50, 4); word(out, 20, 2); word(out, 0, 2); word(out, 0, 2);
        word(out, 0, 4); word(out, crc, 4);
        word(out, unsigned(file.second.size()), 4); word(out, unsigned(file.second.size()), 4);
        word(out, unsigned(file.first.size()), 2); word(out, 0, 2);
        out += file.first + file.second;
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, crc, 4);
        word(central, unsigned(file.second.size()), 4); word(central, unsigned(file.second.size()), 4);
        word(central, unsigned(file.first.size()), 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, unsigned(offset), 4);
        central += file.first;
    }
    auto offset = out.size();
    out += central;
    word(out, 0x06054b50, 4); word(out, 0, 2); word(out, 0, 2);
    word(out, unsigned(files.size()), 2); word(out, unsigned(files.size()), 2);
    word(out, unsigned(central.size()), 4); word(out, unsigned(offset), 4); word(out, 0, 2);
    return out;
}
static Json mod(const std::string &id, const Json &depends = Json::object()) {
    return {{"schemaVersion", 1}, {"id", id}, {"name", id}, {"version", "1.2.3"}, {"depends", depends}};
}
static std::string jar(const Json &metadata) {
    return zip({{"fabric.mod.json", metadata.dump()}});
}
static Json instance(const fs::path &game) {
    fs::create_directories(game / "mods");
    return {{"game_directory", game.u8string()}, {"version", "1.20.1"},
            {"loader", "fabric"}, {"loader_version", "0.16.10"}, {"java_major", 17}};
}
static const Json &row(const Json &report, const std::string &filename) {
    for (const auto &item : report.at("items"))
        if (item.at("filename") == filename) return item;
    throw std::runtime_error("missing inventory row: " + filename);
}
static bool issue(const Json &item, const std::string &code) {
    for (const auto &entry : item.at("detection").at("issues"))
        if (entry.at("code") == code) return true;
    return false;
}
static std::string compatibility(const Json &item) {
    return item.at("detection").at("compatibility").get<std::string>();
}
static Json source_record(const std::string &hash, const std::string &title) {
    return {{"sha512", hash}, {"projectId", "fixture-project"}, {"versionId", "fixture-version"},
            {"title", title}, {"sourceStatus", "matched"}};
}

int main() {
    const auto temp = fs::temp_directory_path() / ("solar-inventory-tests-" + random_url_token());
    try {
        fs::create_directories(temp);

        run("browsing and fresh cache scans", [&] {
            auto i = instance(temp / "cache");
            atomic_write(temp / "cache/mods/demo.jar", jar(mod("demo")));
            clear_mod_scan_cache();
            auto first = instance_inventory(i, "mod");
            auto initial = mod_scan_cache_stats();
            check(initial.at("reads") == 1 && initial.at("parses") == 1, "initial inventory reads and parses one jar");
            auto second = instance_inventory(i, "mod");
            auto cached = mod_scan_cache_stats();
            check(cached.at("reads") == initial.at("reads"), "repeat browsing avoids a jar read");
            check(cached.at("parses") == initial.at("parses"), "repeat browsing avoids parsing");
            check(cached.at("fast_hits").get<unsigned>() > initial.at("fast_hits").get<unsigned>(), "repeat browsing records a cache hit");
            check(row(first, "demo.jar").at("sha512") == row(second, "demo.jar").at("sha512"), "cache preserves verified content identity");
            check(compatibility(row(second, "demo.jar")) == "no_known_conflict", "recognized jar has qualified static status");
            auto fresh = instance_inventory(i, "mod", true);
            auto refreshed = mod_scan_cache_stats();
            check(refreshed.at("reads").get<unsigned>() > cached.at("reads").get<unsigned>(), "explicit fresh scan reads live bytes");
            check(refreshed.at("parses") == cached.at("parses"), "fresh hashing can reuse content metadata");
            check(row(fresh, "demo.jar").at("sha512") == row(second, "demo.jar").at("sha512"), "fresh scan preserves unchanged hash");
        });

        run("same-size replacement with restored mtime", [&] {
            auto i = instance(temp / "replacement");
            auto file = temp / "replacement/mods/demo.jar";
            auto before_bytes = jar(mod("oldx")), after_bytes = jar(mod("newx"));
            check(before_bytes.size() == after_bytes.size(), "replacement fixture has identical size");
            atomic_write(file, before_bytes);
            clear_mod_scan_cache();
            auto before = instance_inventory(i, "mod");
            const auto mtime = fs::last_write_time(file);
            // Atomic replacement changes identity even on filesystems with coarse timestamps.
            atomic_write(file, after_bytes);
            fs::last_write_time(file, mtime);
            auto after = instance_inventory(i, "mod");
            check(row(after, "demo.jar").at("id") == "newx", "same size and mtime cannot preserve old metadata");
            check(row(after, "demo.jar").at("sha512") != row(before, "demo.jar").at("sha512"), "same size and mtime cannot preserve old hash");
            check(mod_scan_cache_stats().at("reads") == 2, "replacement is read again");
            const auto edited_bytes = jar(mod("last"));
            check(edited_bytes.size() == after_bytes.size(), "in-place edit fixture has identical size");
            {
                std::ofstream stream(file, std::ios::binary | std::ios::trunc);
                stream.write(edited_bytes.data(), std::streamsize(edited_bytes.size()));
                check(bool(stream), "in-place edit fixture is written");
            }
            fs::last_write_time(file, mtime);
            auto edited = instance_inventory(i, "mod");
            check(row(edited, "demo.jar").at("id") == "last", "change time invalidates an in-place edit with restored mtime");
            check(row(edited, "demo.jar").at("sha512") == hash_bytes(edited_bytes), "in-place edit gets the live hash");
        });

        run("disabled rename rebinding", [&] {
            auto i = instance(temp / "rename");
            auto file = temp / "rename/mods/demo.jar";
            atomic_write(file, jar(mod("demo")));
            clear_mod_scan_cache();
            auto enabled = instance_inventory(i, "mod");
            auto off = file; off += ".disabled";
            fs::rename(file, off);
            auto disabled = instance_inventory(i, "mod");
            check(disabled.at("items").size() == 1, "rename leaves one current row");
            check(row(disabled, "demo.jar.disabled").at("enabled") == false, "cached content rebinds disabled state");
            check(compatibility(row(disabled, "demo.jar.disabled")) == "disabled", "disabled compatibility is explicit");
            check(row(disabled, "demo.jar.disabled").at("sha512") == row(enabled, "demo.jar").at("sha512"), "rename preserves content hash");
            fs::rename(off, file);
            auto restored = instance_inventory(i, "mod");
            check(row(restored, "demo.jar").at("enabled") == true, "rename back restores enabled state");
        });

        run("loader-specific metadata cache", [&] {
            auto i = instance(temp / "loaders");
            Json quilt{{"quilt_loader", {{"id", "quilt_variant"}, {"version", "2.0.0"},
                {"metadata", {{"name", "Quilt variant"}}}}}};
            atomic_write(temp / "loaders/mods/multi.jar", zip({{"fabric.mod.json", mod("fabric_variant").dump()},
                {"quilt.mod.json", quilt.dump()}}));
            clear_mod_scan_cache();
            auto fabric = instance_inventory(i, "mod");
            check(row(fabric, "multi.jar").at("loader") == "fabric", "Fabric selects Fabric metadata");
            i["loader"] = "quilt"; i["loader_version"] = "0.28.0";
            auto quilt_report = instance_inventory(i, "mod");
            check(row(quilt_report, "multi.jar").at("loader") == "quilt", "loader switch does not reuse Fabric projection");
            check(row(quilt_report, "multi.jar").at("id") == "quilt_variant", "loader switch selects Quilt identity");
            i["loader"] = "fabric";
            auto fabric_again = instance_inventory(i, "mod");
            check(row(fabric_again, "multi.jar").at("id") == "fabric_variant", "switch back restores Fabric identity");
        });

        run("manifest changes on cache hit", [&] {
            auto i = instance(temp / "manifest-hit");
            auto game = temp / "manifest-hit";
            const auto bytes = jar(mod("local_name")), hash = hash_bytes(bytes);
            atomic_write(game / "mods/demo.jar", bytes);
            clear_mod_scan_cache();
            auto initial = instance_inventory(i, "mod");
            check(row(initial, "demo.jar").at("detection").at("source") == "not_checked", "unqueried source is explicit");
            const auto reads = mod_scan_cache_stats().at("reads");
            Json manifest{{"demo.jar", source_record(hash, "Website title")}};
            atomic_write(game / "mods/.solar-mods.json", manifest.dump());
            auto matched = instance_inventory(i, "mod");
            check(row(matched, "demo.jar").at("name") == "Website title", "source title applied on parse cache hit");
            check(row(matched, "demo.jar").at("detection").at("source") == "matched", "hash-verified source applied on cache hit");
            check(mod_scan_cache_stats().at("reads") == reads, "manifest update does not reread payload");
            manifest["demo.jar"]["title"] = "Changed website title";
            atomic_write(game / "mods/.solar-mods.json", manifest.dump());
            auto renamed = instance_inventory(i, "mod");
            check(row(renamed, "demo.jar").at("name") == "Changed website title", "cached row does not freeze source title");
            manifest["demo.jar"]["sha512"] = hash_bytes("different file");
            atomic_write(game / "mods/.solar-mods.json", manifest.dump());
            auto stale = instance_inventory(i, "mod");
            check(row(stale, "demo.jar").at("name") == "local_name", "mismatched source hash cannot override local title");
            check(row(stale, "demo.jar").at("detection").at("source") == "not_checked", "mismatched source hash cannot claim a match");
        });

        run("full-context dependencies recalculate", [&] {
            auto i = instance(temp / "dependencies");
            auto game = temp / "dependencies";
            atomic_write(game / "mods/consumer.jar", jar(mod("consumer", {{"library", ">=1.0.0"}})));
            clear_mod_scan_cache();
            auto missing = instance_inventory(i, "mod");
            check(issue(row(missing, "consumer.jar"), "missing_dependency"), "missing dependency is attributed to consumer row");
            check(compatibility(row(missing, "consumer.jar")) == "conflict", "missing dependency conflicts in row status");
            const auto dependency = jar(mod("library"));
            atomic_write(game / "mods/library.jar", dependency);
            auto satisfied = instance_inventory(i, "mod");
            check(row(satisfied, "consumer.jar").at("errors").empty(), "adding dependency clears cached consumer error");
            check(compatibility(row(satisfied, "consumer.jar")) == "no_known_conflict", "adding dependency recalculates compatibility");
            fs::remove(game / "mods/library.jar");
            auto removed = instance_inventory(i, "mod");
            check(issue(row(removed, "consumer.jar"), "missing_dependency"), "removing dependency restores consumer issue");
            atomic_write(game / "mods/library.jar", dependency);
            instance_inventory(i, "mod");
            fs::rename(game / "mods/library.jar", game / "mods/library.jar.disabled");
            auto disabled = instance_inventory(i, "mod");
            check(issue(row(disabled, "consumer.jar"), "missing_dependency"), "disabled dependency cannot satisfy consumer");
            check(compatibility(row(disabled, "library.jar.disabled")) == "disabled", "disabled dependency keeps disabled row status");
        });

        run("duplicate and declared-conflict related rows", [&] {
            auto i = instance(temp / "duplicates");
            auto game = temp / "duplicates";
            const auto bytes = jar(mod("duplicate"));
            atomic_write(game / "mods/one.jar", bytes);
            atomic_write(game / "mods/two.jar", bytes);
            auto duplicate = instance_inventory(i, "mod");
            for (auto name : {"one.jar", "two.jar"}) {
                check(issue(row(duplicate, name), "duplicate_id"), "duplicate ID issue belongs to both copies");
                check(compatibility(row(duplicate, name)) == "conflict", "both duplicate copies show conflict status");
            }
            fs::remove(game / "mods/two.jar");
            auto resolved = instance_inventory(i, "mod");
            check(row(resolved, "one.jar").at("errors").empty(), "removing duplicate clears surviving row issue");

            auto other_i = instance(temp / "breaks");
            auto breaking = mod("breaking"); breaking["breaks"] = {{"target", ">=1.0.0"}};
            atomic_write(temp / "breaks/mods/breaking.jar", jar(breaking));
            atomic_write(temp / "breaks/mods/target.jar", jar(mod("target")));
            auto conflict = instance_inventory(other_i, "mod");
            for (auto name : {"breaking.jar", "target.jar"}) {
                check(issue(row(conflict, name), "declared_conflict"), "declared breaks issue belongs to both related rows");
                check(compatibility(row(conflict, name)) == "conflict", "both sides of breaks display conflict");
            }
        });

        run("runtime unknown versus mismatch", [&] {
            auto i = instance(temp / "runtime");
            atomic_write(temp / "runtime/mods/runtime.jar", jar(mod("runtime", {{"java", ">=17"}, {"minecraft", "1.20.1"}})));
            i.erase("java_major");
            auto unknown = instance_inventory(i, "mod");
            check(row(unknown, "runtime.jar").at("errors").empty(), "unknown Java is not Java zero incompatibility");
            check(issue(row(unknown, "runtime.jar"), "runtime_unknown"), "unknown runtime has a structured warning");
            check(compatibility(row(unknown, "runtime.jar")) == "needs_review", "unknown runtime cannot claim conflict-free checks");
            i["java_major"] = 8;
            auto wrong = instance_inventory(i, "mod");
            check(issue(row(wrong, "runtime.jar"), "dependency_version"), "known wrong runtime produces a version issue");
            check(compatibility(row(wrong, "runtime.jar")) == "conflict", "known wrong runtime is incompatible");
            i["java_major"] = 17;
            auto matching = instance_inventory(i, "mod");
            check(compatibility(row(matching, "runtime.jar")) == "no_known_conflict", "matching runtime clears previous issues");
            i.erase("version");
            auto unknown_game = instance_inventory(i, "mod");
            check(row(unknown_game, "runtime.jar").at("errors").empty(), "unknown Minecraft version is not a mismatch");
            check(issue(row(unknown_game, "runtime.jar"), "runtime_unknown"), "unknown Minecraft version requires review");
        });

        run("malformed manifests preserve browsing and block mutation", [&] {
            auto i = instance(temp / "malformed");
            auto game = temp / "malformed", manifest = game / "mods/.solar-mods.json";
            const auto bytes = jar(mod("demo"));
            atomic_write(game / "mods/demo.jar", bytes);
            instance_inventory(i, "mod");
            for (const auto &invalid : {std::string("[1,2]"), std::string("{not-json"),
                    std::string("{\"demo.jar\":{\"sha512\":42}}")}) {
                atomic_write(manifest, invalid);
                auto report = instance_inventory(i, "mod");
                check(report.at("items").size() == 1, "malformed source manifest retains local row");
                check(row(report, "demo.jar").at("known") == true, "bad source index does not erase local recognition");
                check(issue(row(report, "demo.jar"), "source_index_invalid"), "bad source index is a row warning");
                rejects([&] { set_mods_enabled(i, {"demo.jar"}, false); }, "fresh mutation rejects malformed source manifest");
                check(fs::exists(game / "mods/demo.jar") && !fs::exists(game / "mods/demo.jar.disabled"), "rejected mutation preserves enabled file");
                check(read_file(manifest) == invalid, "bad source index is preserved for repair");
            }
        });

        run("source knowledge cannot certify unknown metadata", [&] {
            auto i = instance(temp / "unknown");
            auto game = temp / "unknown";
            const auto bytes = zip({{"readme.txt", "valid archive with no loader metadata"}});
            atomic_write(game / "mods/unknown.jar", bytes);
            atomic_write(game / "mods/.solar-mods.json", Json{{"unknown.jar", source_record(hash_bytes(bytes), "Known website project")}}.dump());
            auto report = instance_inventory(i, "mod");
            const auto &item = row(report, "unknown.jar");
            check(item.at("detection").at("local") == "unknown", "valid unknown archive retains unknown local metadata");
            check(item.at("detection").at("source") == "matched", "source identity remains independently known");
            check(compatibility(item) == "needs_review", "source match cannot certify compatibility");
        });

        run("corrupt archive retains healthy sibling", [&] {
            auto i = instance(temp / "corrupt");
            atomic_write(temp / "corrupt/mods/healthy.jar", jar(mod("healthy")));
            atomic_write(temp / "corrupt/mods/broken.jar", "not a zip archive");
            auto report = instance_inventory(i, "mod");
            check(report.at("items").size() == 2, "corrupt archive does not discard sibling rows");
            check(row(report, "healthy.jar").at("detection").at("local") == "recognized", "healthy sibling remains locally recognized");
            check(row(report, "broken.jar").at("detection").at("local") == "invalid", "corrupt archive is visibly invalid");
            check(compatibility(row(report, "broken.jar")) == "needs_review", "corrupt archive cannot claim conflict-free checks");
        });

        run("invalid source icon type is sanitized", [&] {
            auto i = instance(temp / "bad-icon");
            auto game = temp / "bad-icon";
            const auto bytes = jar(mod("demo"));
            atomic_write(game / "mods/demo.jar", bytes);
            for (const auto &invalid : {Json(42), Json::object({{"url", "https://example.invalid/icon.png"}})}) {
                auto record = source_record(hash_bytes(bytes), "Website title");
                record["icon_url"] = invalid;
                atomic_write(game / "mods/.solar-mods.json", Json{{"demo.jar", record}}.dump());
                auto report = instance_inventory(i, "mod");
                const auto icon = row(report, "demo.jar").value("icon_url", Json());
                check(icon.is_null() || icon.is_string(), "invalid source icon type cannot escape to Flutter String cast");
                check(row(report, "demo.jar").at("known") == true, "invalid source artwork preserves local recognition");
            }
        });

        fs::remove_all(temp);
        if (failures) {
            std::cerr << "FAIL " << failures << " inventory cases after " << checks << " checks\n";
            return 1;
        }
        std::cout << "PASS " << checks << " inventory checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL inventory fixture: " << e.what() << '\n';
        std::error_code ec; fs::remove_all(temp, ec);
        return 1;
    }
}
