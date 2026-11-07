// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <zlib.h>

namespace solar::engine { Json inspect_modpack(const fs::path &, const Task &); }
using namespace solar;
using namespace solar::engine;
namespace {
int checks = 0, failures = 0;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void run(const char *name, const std::function<void()> &test) {
    try { test(); }
    catch (const std::exception &e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
void word(std::string &bytes, unsigned value, int count) {
    while (count--) { bytes.push_back(char(value & 255)); value >>= 8; }
}
struct Entry {
    std::string name, bytes;
    unsigned flags = 0, mode = 0, method = 0;
};
std::string zip(const std::vector<Entry> &files) {
    std::string bytes, central;
    for (const auto &file : files) {
        auto offset = unsigned(bytes.size()), size = unsigned(file.bytes.size());
        auto crc = crc32(0, reinterpret_cast<const Bytef *>(file.bytes.data()), size);
        word(bytes, 0x04034b50, 4); word(bytes, 20, 2); word(bytes, file.flags, 2); word(bytes, file.method, 2);
        word(bytes, 0, 4); word(bytes, crc, 4); word(bytes, size, 4); word(bytes, size, 4);
        word(bytes, unsigned(file.name.size()), 2); word(bytes, 0, 2); bytes += file.name + file.bytes;
        word(central, 0x02014b50, 4); word(central, 0x0314, 2); word(central, 20, 2);
        word(central, file.flags, 2); word(central, file.method, 2); word(central, 0, 4); word(central, crc, 4);
        word(central, size, 4); word(central, size, 4); word(central, unsigned(file.name.size()), 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 2); word(central, 0, 2);
        word(central, file.mode << 16, 4); word(central, offset, 4); central += file.name;
    }
    auto offset = unsigned(bytes.size()); bytes += central;
    word(bytes, 0x06054b50, 4); word(bytes, 0, 2); word(bytes, 0, 2);
    word(bytes, unsigned(files.size()), 2); word(bytes, unsigned(files.size()), 2);
    word(bytes, unsigned(central.size()), 4); word(bytes, offset, 4); word(bytes, 0, 2);
    return bytes;
}
Json file(const std::string &path = "mods/demo.jar") {
    return {{"path", path}, {"hashes", {{"sha1", std::string(40, 'a')}, {"sha512", std::string(128, 'b')}}},
        {"downloads", Json::array({"https://cdn.modrinth.com/data/demo/demo.jar"})}, {"fileSize", 123}};
}
Json index() {
    return {{"formatVersion", 1}, {"game", "minecraft"}, {"versionId", "1.0.0"}, {"name", "Fixture 整合包"},
        {"files", Json::array({file()})}, {"dependencies", {{"minecraft", "1.21.1"}, {"fabric-loader", "0.16.14"}}}};
}
struct Fixture {
    fs::path directory = fs::temp_directory_path() / ("solar-mrpack-tests-" + random_url_token());
    Fixture() { fs::create_directories(directory); }
    ~Fixture() { std::error_code error; fs::remove_all(directory, error); }
    Json inspect_bytes(const std::string &bytes) {
        auto path = directory / "fixture.mrpack";
        atomic_write(path, bytes);
        return inspect_modpack(path, Task{});
    }
    Json inspect(const Json &doc, const std::vector<Entry> &extra = {}) {
        std::vector<Entry> entries{{"modrinth.index.json", doc.dump()}};
        entries.insert(entries.end(), extra.begin(), extra.end());
        return inspect_bytes(zip(entries));
    }
};
void rejected(const std::function<void()> &action, const std::string &code = "") {
    bool caught = false;
    try { action(); }
    catch (const Error &error) { caught = true; check(code.empty() || error.code == code, "error code retained"); }
    check(caught, "unsafe or invalid pack rejected");
}
} // namespace

int main() {
    Fixture fixture;
    run("normalized identity and all dependency families", [&] {
        const std::vector<std::pair<std::string, std::string>> loaders{
            {"", "vanilla"}, {"fabric-loader", "fabric"}, {"quilt-loader", "quilt"}, {"forge", "forge"}, {"neoforge", "neoforge"}};
        for (const auto &loader : loaders) {
            auto doc = index(); doc["dependencies"] = {{"minecraft", "1.21.1"}};
            if (!loader.first.empty()) doc["dependencies"][loader.first] = "123.4";
            auto pack = fixture.inspect(doc);
            check(pack["name"] == "Fixture 整合包" && pack["version_id"] == "1.0.0", "identity retained");
            check(pack["minecraft"] == "1.21.1" && pack["loader"] == loader.second, "dependency family normalized");
            check(pack["loader_version"] == (loader.first.empty() ? "" : "123.4"), "loader version retained");
            check(pack["archive_sha512"].get<std::string>().size() == 128, "archive pinned to SHA512");
            check(pack["files"][0]["client"] == "required", "unspecified environment defaults to required");
            check(pack["files"][0]["size"] == 123 && pack["summary"] == "", "size and optional summary normalized");
        }
    });
    run("required optional and server-only environments", [&] {
        auto doc = index();
        auto optional = file("mods/optional.jar"), server = file("mods/server.jar");
        optional["env"] = {{"client", "optional"}, {"server", "unsupported"}};
        server["env"] = {{"client", "unsupported"}, {"server", "required"}};
        doc["files"].push_back(optional); doc["files"].push_back(server);
        auto pack = fixture.inspect(doc, {{"server-overrides/mods/dedicated.jar", "server bytes"}});
        check(pack["files"].size() == 3 && pack["files"][2]["client"] == "unsupported", "server-only files explicitly retained for exclusion");
        check(pack["optional_count"] == 1 && pack["server_files"] == 1, "environment counts normalized");
        check(pack["overrides"].empty(), "dedicated server overrides excluded");
        for (const auto &bad : std::vector<Json>{Json(nullptr), Json("client"), Json::object(),
            Json{{"client", "required"}}, Json{{"client", "sometimes"}, {"server", "required"}},
            Json{{"client", "required"}, {"server", "required"}, {"future", "required"}}}) {
            auto invalid = index(); invalid["files"][0]["env"] = bad;
            rejected([&] { fixture.inspect(invalid); });
        }
    });
    run("overrides follow downloaded files and client layer", [&] {
        auto doc = index(); doc["files"].push_back(file("config/example.cfg"));
        auto pack = fixture.inspect(doc, {{"client-overrides/config/example.cfg", "client"},
            {"overrides/config/", ""}, {"overrides/config/example.cfg", "shared"}, {"overrides/options.txt", "settings"}});
        check(pack["override_count"] == 3, "only files counted");
        check(pack["overrides"][0]["entry"] == "overrides/config/example.cfg", "common layer sorted first");
        check(pack["overrides"][2]["entry"] == "client-overrides/config/example.cfg", "client layer follows shared layer");
        check(pack["overrides"][2]["layer"] == 1 && pack["overrides"][2]["path"] == "config/example.cfg", "client destination retained");
        check(pack["overrides"][2]["size"] == 6 && pack["overrides"][2]["crc32"].is_number_unsigned(), "override integrity fields retained");
        rejected([&] { fixture.inspect(doc, {{"overrides/config/example.cfg/child", "bad"}}); }, "path_conflict");
        rejected([&] { fixture.inspect(doc, {{"overrides/CONFIG/example.cfg", "bad"}}); }, "path_conflict");
        rejected([&] { fixture.inspect(doc, {{"overrides/config/example.cfg", "one"}, {"client-overrides/config/EXAMPLE.cfg", "two"}}); }, "path_conflict");
    });
    run("required index fields and supported formats", [&] {
        for (const auto *field : {"formatVersion", "game", "versionId", "name", "files", "dependencies"}) {
            auto doc = index(); doc.erase(field); rejected([&] { fixture.inspect(doc); });
        }
        for (auto version : {Json(2), Json("1"), Json(1.0), Json(true)}) {
            auto doc = index(); doc["formatVersion"] = version; rejected([&] { fixture.inspect(doc); });
        }
        auto doc = index(); doc["game"] = "future-game"; rejected([&] { fixture.inspect(doc); }, "unsupported_game");
        doc = index(); doc["dependencies"].erase("minecraft"); rejected([&] { fixture.inspect(doc); });
        doc = index(); doc["dependencies"]["future-loader"] = "1";
        rejected([&] { fixture.inspect(doc); }, "unsupported_dependency");
        doc = index(); doc["dependencies"]["forge"] = "47.1";
        rejected([&] { fixture.inspect(doc); }, "multiple_loaders");
        doc = index(); doc["dependencies"]["fabric-loader"] = 1; rejected([&] { fixture.inspect(doc); });
        rejected([&] { fixture.inspect_bytes(zip({{"nested/modrinth.index.json", index().dump()}})); });
        rejected([&] { fixture.inspect_bytes(zip({{"modrinth.index.json/", ""}})); });
        rejected([&] { fixture.inspect_bytes(zip({{"modrinth.index.json", ""}})); });
        rejected([&] { fixture.inspect_bytes(zip({{"modrinth.index.json", std::string("{\"name\":\"\xff\"}")}})); });
        rejected([&] { fixture.inspect_bytes(zip({{"modrinth.index.json", "{\"formatVersion\":1,\"formatVersion\":2}"}})); });
        rejected([&] { fixture.inspect_bytes(zip({{"modrinth.index.json", std::string(16 * 1024 * 1024 + 1, ' ')}})); }, "archive_limit");
    });
    run("CurseForge ZIP manifest and overrides", [&] {
        auto manifest = Json{{"manifestType", "minecraftModpack"}, {"manifestVersion", 1},
            {"name", "CurseForge fixture"}, {"version", "1.2"},
            {"minecraft", {{"version", "1.20.1"}, {"modLoaders", Json::array({
                {{"id", "forge-47.4.0"}, {"primary", true}}})}}},
            {"overrides", "client-files"},
            {"files", Json::array({{{"projectID", 10}, {"fileID", 20}, {"required", true}},
                {{"projectID", 11}, {"fileID", 21}, {"required", false}}})}};
        auto path = fixture.directory / "fixture.ZIP";
        const auto inspect = [&](const Json &doc, const std::string &override = "client-files/config/demo.toml") {
            atomic_write(path, zip({{"manifest.json", doc.dump()}, {override, "conf=true"},
                {"server-overrides/server.properties", "ignored"}}));
            return inspect_modpack(path);
        };
        auto pack = inspect(manifest);
        check(pack["format"] == "curseforge" && pack["minecraft"] == "1.20.1" &&
            pack["loader"] == "forge" && pack["loader_version"] == "47.4.0", "exact pack platform retained");
        check(pack["files"][0]["path"] == "10/20" && pack["files"][1]["client"] == "optional" &&
            pack["optional_count"] == 1, "exact file identities and optional flags retained");
        check(pack["overrides"].size() == 1 && pack["overrides"][0]["path"] == "config/demo.toml", "declared overrides applied");
        auto invalid = manifest;
        invalid["files"][0]["projectID"] = -1;
        rejected([&] { inspect(invalid); }, "invalid_modpack");
        invalid = manifest;
        invalid["files"].push_back(invalid["files"][0]);
        rejected([&] { inspect(invalid); }, "invalid_modpack");
        invalid = manifest;
        invalid["minecraft"]["modLoaders"].push_back({{"id", "fabric-0.16.10"}, {"primary", true}});
        rejected([&] { inspect(invalid); }, "multiple_loaders");
        invalid = manifest;
        invalid["overrides"] = "../elsewhere";
        rejected([&] { inspect(invalid); }, "unsafe_path");
        rejected([&] { inspect(manifest, "client-files/versions/overwritten.json"); }, "reserved_path");
        atomic_write(path, zip({{"nested/manifest.json", manifest.dump()}}));
        rejected([&] { inspect_modpack(path); }, "invalid_modpack");
    });
    run("hashes and integral file sizes", [&] {
        for (const auto &bad : std::vector<Json>{Json::object(), Json{{"sha1", std::string(40, 'a')}},
            Json{{"sha1", "abcd"}, {"sha512", std::string(128, 'b')}},
            Json{{"sha1", std::string(40, 'g')}, {"sha512", std::string(128, 'b')}},
            Json{{"sha1", std::string(40, 'a')}, {"sha512", std::string(127, 'b')}}}) {
            auto doc = index(); doc["files"][0]["hashes"] = bad; rejected([&] { fixture.inspect(doc); });
        }
        for (const auto &bad : std::vector<Json>{Json(-1), Json(12.5), Json("123"), Json(nullptr), Json(2147483649ULL)}) {
            auto doc = index(); doc["files"][0]["fileSize"] = bad; rejected([&] { fixture.inspect(doc); });
        }
        auto doc = index(); doc["files"][0]["fileSize"] = 0;
        doc["files"][0]["hashes"]["sha1"] = std::string(40, 'A');
        auto pack = fixture.inspect(doc);
        check(pack["files"][0]["sha1"] == std::string(40, 'a'), "uppercase hex normalized");
        check(pack["files"][0]["size"] == 0, "empty file permitted");
    });
    run("download domain allowlist and URL syntax", [&] {
        for (const auto &url : std::vector<std::string>{"http://cdn.modrinth.com/file", "https://example.com/file",
            "https://cdn.modrinth.com.evil.test/file", "https://user@cdn.modrinth.com/file", "https://cdn.modrinth.com:443/file",
            "https://cdn.modrinth.com/file name", "https://cdn.modrinth.com/file\\name", "https://cdn.modrinth.com/%zz",
            "https://cdn.modrinth.com/#fragment", "https://cdn.modrinth.com/\nfile", "https://cdn.modrinth.com/模组.jar"}) {
            auto doc = index(); doc["files"][0]["downloads"] = Json::array({url});
            rejected([&] { fixture.inspect(doc); }, "unsafe_url");
        }
        auto doc = index(); doc["files"][0]["downloads"] = Json::array(); rejected([&] { fixture.inspect(doc); });
        doc["files"][0]["downloads"] = Json::array({"https://github.com/org/repo/file", "https://raw.githubusercontent.com/a/b/c", "https://gitlab.com/a/b/-/raw/c", "https://CDN.MODRINTH.COM/%E6%A8%A1.jar"});
        check(fixture.inspect(doc)["files"][0]["urls"].size() == 4, "documented allowed domains supported");
    });
    run("portable safe paths and reserved launcher internals", [&] {
        for (const auto &path : std::vector<std::string>{"../escape", "mods/../escape", "/absolute", "C:/escape", "mods\\escape",
            "mods//empty", "mods/./file", "mods/CON.jar", "mods/LPT1.txt", "mods/COM¹.jar", "mods/LPT³.txt", "mods/CON .txt",
            "mods/CONOUT$", "mods/trailing.", "mods/trailing ", "mods/with?glob"}) {
            auto doc = index(); doc["files"][0]["path"] = path; rejected([&] { fixture.inspect(doc); }, "unsafe_path");
            rejected([&] { fixture.inspect(index(), {{"overrides/" + path, "bad"}}); }, "unsafe_path");
        }
        for (const auto &path : std::vector<std::string>{"versions/a.json", "Libraries/a.jar", "assets/indexes/a.json", "runtime/java",
            "natives/lib", ".solar-instance.json", "mods/.solar-mods.json", "config/.m2cl-settings", ".secl-stage/file"}) {
            auto doc = index(); doc["files"][0]["path"] = path; rejected([&] { fixture.inspect(doc); }, "reserved_path");
            rejected([&] { fixture.inspect(index(), {{"overrides/" + path, "bad"}}); }, "reserved_path");
        }
        check(fixture.inspect(index(), {{"overrides/config/中文.cfg", "valid"}})["overrides"].size() == 1, "valid UTF8 paths supported");
    });
    run("duplicate case and prefix conflicts", [&] {
        auto doc = index(); doc["files"].push_back(file()); rejected([&] { fixture.inspect(doc); }, "path_conflict");
        doc = index(); doc["files"].push_back(file("MODS/other.jar")); rejected([&] { fixture.inspect(doc); }, "path_conflict");
        doc = index(); doc["files"].push_back(file("mods")); rejected([&] { fixture.inspect(doc); }, "path_conflict");
        rejected([&] { fixture.inspect(index(), {{"overrides/config/a", "1"}, {"overrides/config/a", "2"}}); }, "path_conflict");
        rejected([&] { fixture.inspect(index(), {{"overrides/config", "1"}, {"overrides/config/a", "2"}}); }, "path_conflict");
        rejected([&] { fixture.inspect(index(), {{"unrelated/../../escape", "bad"}}); }, "unsafe_path");
        rejected([&] { fixture.inspect(index(), {{"server-overrides/../escape", "bad"}}); }, "unsafe_path");
    });
    run("ZIP symlink encryption special files and local-header integrity", [&] {
        rejected([&] { fixture.inspect(index(), {{"overrides/link", "target", 0, 0120777}}); }, "unsafe_archive");
        rejected([&] { fixture.inspect(index(), {{"overrides/file", "bytes", 1}}); }, "unsafe_archive");
        rejected([&] { fixture.inspect(index(), {{"overrides/fifo", "", 0, 0010644}}); }, "unsafe_archive");
        auto bytes = zip({{"modrinth.index.json", index().dump()}}); bytes[30] = 'x';
        rejected([&] { fixture.inspect_bytes(bytes); }, "invalid_archive");
        bytes = zip({{"modrinth.index.json", index().dump()}}); bytes[30 + std::string("modrinth.index.json").size()] ^= 1;
        rejected([&] { fixture.inspect_bytes(bytes); }, "crc_mismatch");
        bytes = zip({{"modrinth.index.json", index().dump()}}); bytes.pop_back();
        rejected([&] { fixture.inspect_bytes(bytes); }, "invalid_archive");
        rejected([&] { fixture.inspect(index(), {{"overrides/file", "content", 0, 0, 12}}); }, "invalid_archive");
    });
    run("archive count and cancellation limits", [&] {
        std::vector<Entry> entries{{"modrinth.index.json", index().dump()}};
        for (int i = 0; i != 10000; ++i) entries.push_back({"extra/" + std::to_string(i), ""});
        rejected([&] { fixture.inspect_bytes(zip(entries)); }, "archive_limit");
        auto path = fixture.directory / "cancelled.mrpack"; atomic_write(path, zip({{"modrinth.index.json", index().dump()}}));
        Task task; task.cancel->store(true); rejected([&] { inspect_modpack(path, task); }, "cancelled");
    });
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
