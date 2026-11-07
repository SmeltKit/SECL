// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <map>
#include <mutex>
#include <zlib.h>

using namespace solar;
using namespace solar::engine;
namespace {
int checks = 0, failures = 0;
fs::path java_executable;
void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
void run(const char *name, const std::function<void()> &test) {
    try { test(); }
    catch (const std::exception &error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
}
void rejected(const std::function<void()> &action, const std::string &code = "") {
    bool caught = false;
    try { action(); }
    catch (const Error &error) {
        caught = true;
        if (!code.empty() && error.code != code)
            throw std::runtime_error("expected " + code + ", got " + error.code + ": " + error.what());
        check(code.empty() || error.code == code, "expected rejection code");
    }
    check(caught, "operation rejected");
}
void word(std::string &bytes, unsigned value, int count) {
    while (count--) { bytes.push_back(char(value & 255)); value >>= 8; }
}
std::string zip(const std::vector<std::pair<std::string, std::string>> &entries) {
    std::string bytes, central;
    for (const auto &entry : entries) {
        auto offset = unsigned(bytes.size()), size = unsigned(entry.second.size());
        auto crc = crc32(0, reinterpret_cast<const Bytef *>(entry.second.data()), size);
        word(bytes, 0x04034b50, 4); word(bytes, 20, 2); word(bytes, 0, 2); word(bytes, 0, 2);
        word(bytes, 0, 4); word(bytes, crc, 4); word(bytes, size, 4); word(bytes, size, 4);
        word(bytes, unsigned(entry.first.size()), 2); word(bytes, 0, 2); bytes += entry.first + entry.second;
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, crc, 4);
        word(central, size, 4); word(central, size, 4); word(central, unsigned(entry.first.size()), 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 4); word(central, offset, 4); central += entry.first;
    }
    auto offset = unsigned(bytes.size()); bytes += central;
    word(bytes, 0x06054b50, 4); word(bytes, 0, 2); word(bytes, 0, 2);
    word(bytes, unsigned(entries.size()), 2); word(bytes, unsigned(entries.size()), 2);
    word(bytes, unsigned(central.size()), 4); word(bytes, offset, 4); word(bytes, 0, 2);
    return bytes;
}
struct Network {
    std::mutex mutex;
    std::map<std::string, HttpResponse> responses;
    std::map<std::string, unsigned> requests;
    void set(const std::string &url, HttpResponse response) {
        std::lock_guard<std::mutex> lock(mutex); responses[url] = std::move(response);
    }
    void okay(const std::string &url, const std::string &bytes) { set(url, {200, bytes, ""}); }
    unsigned count(const std::string &url) {
        std::lock_guard<std::mutex> lock(mutex); return requests[url];
    }
    std::string bytes(const std::string &url) {
        std::lock_guard<std::mutex> lock(mutex); return responses.at(url).body;
    }
    HttpResponse request(const std::string &url) {
        std::lock_guard<std::mutex> lock(mutex); ++requests[url];
        auto found = responses.find(url);
        return found == responses.end() ? HttpResponse{404, "missing fixture", ""} : found->second;
    }
};
const std::string manifest_url = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";
const std::string version_url = "https://piston-meta.mojang.com/fixture/1.21.1.json";
const std::string client_url = "https://piston-data.mojang.com/fixture/client.jar";
const std::string assets_url = "https://piston-meta.mojang.com/fixture/assets.json";
const std::string first_url = "https://cdn.modrinth.com/data/fixture/first.txt";
const std::string second_url = "https://cdn.modrinth.com/data/fixture/second.txt";
const std::string optional_url = "https://cdn.modrinth.com/data/fixture/optional.txt";
const std::string server_url = "https://cdn.modrinth.com/data/fixture/server.txt";
const std::string cf_files_url = "https://api.curseforge.com/v1/mods/files";
const std::string cf_projects_url = "https://api.curseforge.com/v1/mods";
const std::string cf_first_url = "https://edge.forgecdn.net/files/1/1/fixture-resources.zip";
const std::string cf_second_url = "https://edge.forgecdn.net/files/1/2/fixture-shader.zip";
Json descriptor(const std::string &url, const std::string &bytes) {
    return {{"url", url}, {"sha1", hash_bytes(bytes, "sha1")}, {"size", bytes.size()}};
}
Json pack_file(const std::string &path, const std::string &url, const std::string &bytes,
               const std::string &client = "required") {
    return {{"path", path}, {"hashes", {{"sha1", hash_bytes(bytes, "sha1")}, {"sha512", hash_bytes(bytes)}}},
        {"downloads", Json::array({url})}, {"fileSize", bytes.size()},
        {"env", {{"client", client}, {"server", client == "unsupported" ? "required" : "unsupported"}}}};
}
Json cf_file(int id, int project, const std::string &filename, const std::string &url, const std::string &bytes) {
    return {{"id", id}, {"modId", project}, {"gameId", 432}, {"fileName", filename},
        {"downloadUrl", url}, {"fileLength", bytes.size()}, {"isAvailable", true}, {"fileStatus", 4},
        {"hashes", Json::array({{{"algo", 1}, {"value", hash_bytes(bytes, "sha1")}}})}};
}
Json cf_project(int id, int kind, const std::string &name, bool distribute) {
    return {{"id", id}, {"gameId", 432}, {"classId", kind}, {"name", name},
        {"allowModDistribution", distribute}, {"links", {{"websiteUrl", "https://www.curseforge.com/minecraft/" + name}}}};
}
struct Fixture {
    fs::path temp = fs::weakly_canonical(fs::temp_directory_path()) / ("solar-mrpack-install-" + random_url_token());
    fs::path root = temp / "root", archive = temp / "fixture.mrpack";
    const std::string id = "fixture-instance";
    Network network;
    Json document;
    std::vector<std::pair<std::string, std::string>> overrides;
    Fixture() {
        const auto assets = Json{{"objects", Json::object()}}.dump();
        const std::string client = "tiny Minecraft client fixture";
        Json metadata{{"id", "1.21.1"}, {"type", "release"}, {"mainClass", "net.minecraft.client.main.Main"},
            {"javaVersion", {{"majorVersion", 21}}}, {"libraries", Json::array()},
            {"arguments", {{"game", Json::array()}, {"jvm", Json::array()}}},
            {"downloads", {{"client", descriptor(client_url, client)}}},
            {"assetIndex", descriptor(assets_url, assets)}};
        metadata["assetIndex"]["id"] = "fixture-assets";
        const auto version = metadata.dump();
        Json entry{{"id", "1.21.1"}, {"type", "release"}, {"url", version_url}, {"sha1", hash_bytes(version, "sha1")}};
        network.okay(manifest_url, Json{{"versions", Json::array({entry})}}.dump());
        network.okay(version_url, version); network.okay(client_url, client); network.okay(assets_url, assets);
        network.okay(first_url, "first downloaded bytes"); network.okay(second_url, "second downloaded bytes");
        network.okay(optional_url, "optional bytes"); network.okay(server_url, "server bytes");
        document = {{"formatVersion", 1}, {"game", "minecraft"}, {"name", "Offline fixture pack"}, {"versionId", "1.0"},
            {"dependencies", {{"minecraft", "1.21.1"}}},
            {"files", Json::array({pack_file("config/first.txt", first_url, "first downloaded bytes"),
                pack_file("config/second.txt", second_url, "second downloaded bytes"),
                pack_file("config/optional.txt", optional_url, "optional bytes", "optional"),
                pack_file("config/server.txt", server_url, "server bytes", "unsupported")})}};
        overrides = {{"client-overrides/config/first.txt", "client override"},
            {"overrides/config/first.txt", "shared override"}, {"overrides/options.txt", "fixture options"},
            {"server-overrides/server.properties", "dedicated server only"}};
        rewrite_archive();
    }
    ~Fixture() { std::error_code error; fs::remove_all(temp, error); }
    void rewrite_archive() {
        const auto *index = archive.extension() == ".zip" ? "manifest.json" : "modrinth.index.json";
        std::vector<std::pair<std::string, std::string>> entries{{index, document.dump()}};
        entries.insert(entries.end(), overrides.begin(), overrides.end()); atomic_write(archive, zip(entries));
    }
    Task task() {
        Task result; result.source = "official";
        result.transport = [this](const std::string &url, const std::string &, const std::string &, const std::string &) {
            return network.request(url);
        };
        return result;
    }
    Json request() const {
        return {{"path", archive.u8string()}, {"root", root.u8string()}, {"id", id},
            {"name", "Displayed fixture name"}, {"java", java_executable.u8string()}};
    }
    Json plan() { return plan_modpack(request(), task()); }
    fs::path game() const { return root / "instances" / id; }
    fs::path version() const { return root / "versions" / id; }
    fs::path cached(const Json &plan, std::size_t index = 0) const {
        return root / ".solar-mrpack" / plan.at("archive_sha512").get<std::string>() /
            "downloads" / plan.at("files").at(index).at("sha512").get<std::string>();
    }
    void unpublished() const {
        check(!fs::exists(game()) && !fs::exists(version()), "failed install publishes no instance or game directory");
    }
};
struct CurseForgeFixture : Fixture {
    const std::string resource = zip({{"pack.mcmeta", "{\"pack\":{\"pack_format\":34,\"description\":\"fixture\"}}"}});
    const std::string shader = zip({{"shaders/basic.fsh", "void main() {}"}});
    Json files, projects;
    CurseForgeFixture() {
        archive = temp / "fixture.zip";
        document = {{"manifestType", "minecraftModpack"}, {"manifestVersion", 1},
            {"name", "CurseForge fixture"}, {"version", "1.0"},
            {"minecraft", {{"version", "1.21.1"}, {"modLoaders", Json::array()}}},
            {"files", Json::array({{{"projectID", 10}, {"fileID", 101}, {"required", true}},
                {{"projectID", 20}, {"fileID", 102}, {"required", false}}})}};
        overrides = {{"overrides/config/example.cfg", "configuration"}};
        files = Json::array({cf_file(101, 10, "fixture-resources.zip", cf_first_url, resource),
            cf_file(102, 20, "fixture-shader.zip", cf_second_url, shader)});
        projects = Json::array({cf_project(10, 12, "texture-packs/fixture", true),
            cf_project(20, 6552, "shaders/fixture", false)});
        network.okay(cf_first_url, resource);
        network.okay(cf_second_url, shader);
        rewrite_archive();
        reply();
    }
    void reply() {
        network.okay(cf_files_url, Json{{"data", files}}.dump());
        network.okay(cf_projects_url, Json{{"data", projects}}.dump());
    }
};
} // namespace

int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "-XshowSettings:properties") {
        std::cout << "java.version = 21.0.1\njava.runtime.version = 21.0.1+12\njava.vendor = Offline test fixture\nos.arch = "
                  << platform().arch << '\n';
        return 0;
    }
    java_executable = fs::canonical(fs::absolute(fs::u8path(argv[0])));
    run("CurseForge exact import and local supplement review", [] {
        CurseForgeFixture fixture;
        auto plan = fixture.plan();
        check(plan["pack"]["format"] == "curseforge" && plan["files"].size() == 1 &&
            plan["files"][0]["file_id"] == "101" && plan["blocked_files"].empty(), "exact required file resolved");
        check(!fs::exists(fixture.root) && fixture.network.count(cf_first_url) == 0,
            "plan does not write or download pack payloads");
        auto request = fixture.request();
        request["optional_files"] = Json::array({"20/102"});
        plan = plan_modpack(request, fixture.task());
        check(plan["blocked_files"].size() == 1 && plan["blocked_files"][0]["reason"] == "manual_download_required" &&
            plan["blocked_files"][0]["website_url"] == "https://www.curseforge.com/minecraft/shaders/fixture/files/102",
            "opted-out file has exact official page and requires a local copy");
        rejected([&] { apply_modpack(plan, fixture.task()); }, "modpack_files_blocked");
        fixture.unpublished();
        check(fixture.network.count(cf_second_url) == 0 && fixture.network.count(cf_first_url) == 0,
            "blocked plan never contacts a download server");
        const auto supplement = fixture.temp / "supplement";
        request["supplement_directory"] = supplement.u8string();
        atomic_write(supplement / "fixture-shader.zip", std::string(fixture.shader.size(), 'x'));
        plan = plan_modpack(request, fixture.task());
        check(plan["blocked_files"][0]["reason"] == "supplement_mismatch", "same-name wrong bytes do not satisfy the pack");
        atomic_write(supplement / "fixture-shader.zip", fixture.shader);
        plan = plan_modpack(request, fixture.task());
        check(plan["blocked_files"].empty() && plan["files"][1]["supplemental"] == true &&
            plan["total_download_bytes"] == fixture.resource.size(), "verified local copy included without download bytes");
        auto result = apply_modpack(plan, fixture.task());
        check(result["committed"] == true && result["modpack"]["format"] == "curseforge", "CurseForge instance committed");
        check(read_file(fixture.game() / "resourcepacks/fixture-resources.zip") == fixture.resource &&
            read_file(fixture.game() / "shaderpacks/fixture-shader.zip") == fixture.shader &&
            read_file(fixture.game() / "config/example.cfg") == "configuration", "verified content and overrides installed");
        check(fixture.network.count(cf_second_url) == 0 && fixture.network.count(cf_first_url) == 1 &&
            fs::is_regular_file(supplement / "fixture-shader.zip"), "restricted source never downloaded and local copy retained");
        check(read_json(fixture.game() / ".solar-modpack.json").dump().find("forgecdn") == std::string::npos,
            "CurseForge remote metadata not persisted in receipt");
    });
    run("CurseForge exact binding and post-review verification", [] {
        CurseForgeFixture fixture;
        fixture.files[0]["modId"] = 20;
        fixture.reply();
        auto missing = fixture.plan();
        check(missing["blocked_files"][0]["reason"] == "file_unavailable" && missing["files"].empty(),
            "a matching file ID under the wrong project cannot satisfy a reference");
        fixture.files[0]["modId"] = 10;
        fixture.reply();
        auto plan = fixture.plan();
        fixture.files[0]["hashes"][0]["value"] = std::string(40, 'b');
        fixture.reply();
        rejected([&] { apply_modpack(plan, fixture.task()); }, "plan_stale");
        fixture.unpublished();
        check(fixture.network.count(cf_first_url) == 0, "post-review hash changes blocked before download");
        fixture.files[0]["hashes"][0]["value"] = hash_bytes(fixture.resource, "sha1");
        fixture.reply();
        fixture.network.okay(cf_first_url, std::string(fixture.resource.size(), 'z'));
        rejected([&] { apply_modpack(plan, fixture.task()); }, "verification_failed");
        fixture.unpublished();
    });
    run("CurseForge declared conflicts and override path collisions block install", [] {
        CurseForgeFixture fixture;
        fixture.files[0]["dependencies"] = Json::array({{{"modId", 20}, {"relationType", 5}}});
        fixture.projects[1]["allowModDistribution"] = true;
        fixture.reply();
        auto request = fixture.request();
        request["optional_files"] = Json::array({"20/102"});
        auto plan = plan_modpack(request, fixture.task());
        check(plan["blocked_files"].size() == 1 && plan["blocked_files"][0]["reason"] == "declared_conflict",
            "declared conflict between selected exact files is surfaced for review");
        rejected([&] { apply_modpack(plan, fixture.task()); }, "modpack_files_blocked");
        fixture.unpublished();
        fixture.overrides.push_back({"overrides/Resourcepacks/fixture-resources.zip", "would replace verified file"});
        fixture.rewrite_archive();
        rejected([&] { fixture.plan(); }, "path_conflict");
        check(fixture.network.count(cf_first_url) == 0 && fixture.network.count(cf_second_url) == 0,
            "conflicts and path collisions cause no downloads");
    });
    run("CurseForge local JAR loader mismatch blocks publication", [] {
        CurseForgeFixture fixture;
        const std::string mod_url = "https://edge.forgecdn.net/files/1/1/fabric-fixture.jar";
        const auto mod = zip({{"fabric.mod.json", Json{{"schemaVersion", 1}, {"id", "cf_fixture"},
            {"version", "1.0"}, {"depends", {{"fabricloader", ">=0.16"}}}}.dump()}});
        fixture.files[0] = cf_file(101, 10, "fabric-fixture.jar", mod_url, mod);
        fixture.projects[0] = cf_project(10, 6, "mc-mods/fixture", true);
        fixture.network.okay(mod_url, mod);
        fixture.reply();
        auto plan = fixture.plan();
        check(plan["files"][0]["path"] == "mods/fabric-fixture.jar", "market mod is placed in the mods directory");
        rejected([&] { apply_modpack(plan, fixture.task()); }, "dependency_conflict");
        fixture.unpublished();
        check(fixture.network.count(mod_url) == 1, "downloaded mod is checked before publishing the instance");
    });
    run("fresh plan and complete isolated vanilla instance", [] {
        Fixture fixture; auto plan = fixture.plan();
        check(!fs::exists(fixture.root), "planning performs no installation writes");
        check(plan["files"].size() == 2 && plan["optional_files"].empty(), "optional defaults off");
        check(plan["skipped_server_count"] == 1 && plan["override_count"] == 3, "environment and override summary");
        check(fixture.network.count(manifest_url) == 0, "planning performs no network requests");
        auto result = apply_modpack(plan, fixture.task());
        check(result["committed"] == true, "completion explicitly reports committed publication");
        check(read_file(fixture.game() / "config/first.txt") == "client override", "client overrides replace downloaded and shared files");
        check(read_file(fixture.game() / "config/second.txt") == "second downloaded bytes", "required download installed");
        check(read_file(fixture.game() / "options.txt") == "fixture options", "shared overrides installed");
        check(!fs::exists(fixture.game() / "config/optional.txt") && !fs::exists(fixture.game() / "config/server.txt") &&
            !fs::exists(fixture.game() / "server.properties"), "optional and dedicated server files excluded");
        check(fixture.network.count(optional_url) == 0 && fixture.network.count(server_url) == 0, "excluded files never downloaded");
        check(read_file(fixture.version() / (fixture.id + ".jar")) == "tiny Minecraft client fixture", "real game installation publishes client");
        auto metadata = read_json(fixture.version() / (fixture.id + ".json"));
        check(metadata["id"] == fixture.id && metadata["_solarGameDirectory"] == fixture.game().u8string(), "published metadata uses final paths");
        check(metadata.dump().find(".solar-mrpack") == std::string::npos && result["java"].dump().find(".solar-mrpack") == std::string::npos,
            "published metadata and Java contain no hidden staging paths");
        check(result["checks"]["errors"].empty() && result["modpack"]["archive_sha512"] == plan["archive_sha512"], "compatibility check and receipt returned");
        auto scan = scan_instances({fixture.root});
        check(scan["instances"].size() == 1 && scan["warnings"].empty(), "only completed instance is discoverable");
        check(scan["instances"][0]["name"] == "Displayed fixture name" && scan["instances"][0]["isolated"] == true &&
            scan["instances"][0]["game_directory"] == fixture.game().u8string(), "display name and isolation survive scanning");
        check(read_json(fixture.game() / ".solar-modpack.json")["selected_optional_files"].empty(), "receipt records optional choice");
    });
    run("selected optional file and Fabric loader integration", [] {
        Fixture fixture;
        const std::string loader_url = "https://maven.fabricmc.net/net/fabricmc/fabric-loader/0.16.14/fabric-loader-0.16.14.jar";
        const std::string profile_url = "https://meta.fabricmc.net/v2/versions/loader/1.21.1/0.16.14/profile/json";
        const std::string loader_path = "net/fabricmc/fabric-loader/0.16.14/fabric-loader-0.16.14.jar";
        Json profile{{"id", "fabric-loader-0.16.14-1.21.1"}, {"inheritsFrom", "1.21.1"},
            {"mainClass", "net.fabricmc.loader.impl.launch.knot.KnotClient"},
            {"libraries", Json::array({Json{{"name", "net.fabricmc:fabric-loader:0.16.14"},
                {"downloads", {{"artifact", descriptor(loader_url, "fixture Fabric loader")}}}}})}};
        profile["libraries"][0]["downloads"]["artifact"]["path"] = loader_path;
        fixture.network.okay(profile_url, profile.dump()); fixture.network.okay(loader_url, "fixture Fabric loader");
        const auto jar = zip({{"fabric.mod.json", Json{{"schemaVersion", 1}, {"id", "fixture_demo"}, {"version", "1.0"},
            {"depends", {{"minecraft", "1.21.x"}, {"fabricloader", ">=0.16"}}}}.dump()}});
        const std::string mod_url = "https://cdn.modrinth.com/data/fixture/mod.jar";
        fixture.network.okay(mod_url, jar); fixture.document["dependencies"]["fabric-loader"] = "0.16.14";
        fixture.document["files"].push_back(pack_file("mods/demo.jar", mod_url, jar)); fixture.rewrite_archive();
        auto request = fixture.request(); request["optional_files"] = Json::array({"config/optional.txt"});
        auto plan = plan_modpack(request, fixture.task());
        check(plan["files"].size() == 4 && plan["optional_files"].size() == 1, "chosen optional and mod included");
        auto result = apply_modpack(plan, fixture.task());
        check(read_file(fixture.game() / "config/optional.txt") == "optional bytes", "explicit optional file installed");
        check(read_file(fixture.root / "libraries" / loader_path) == "fixture Fabric loader", "Fabric dependency promoted");
        check(result["checks"]["errors"].empty() && loader_components(result["metadata"]) == Json{{"fabric", "0.16.14"}}, "Fabric profile and local mod verified");
        auto scan = scan_instances({fixture.root});
        check(scan["instances"].size() == 1 && scan["instances"][0]["loader"] == "fabric", "Fabric instance discovered");
    });
    run("missing second file retains verified cache and retry reuses first", [] {
        Fixture fixture; auto plan = fixture.plan(); fixture.network.set(second_url, {404, "missing", ""});
        rejected([&] { apply_modpack(plan, fixture.task()); }, "http_404"); fixture.unpublished();
        check(fs::is_regular_file(fixture.cached(plan)), "verified first file retained after failure");
        check(fixture.network.count(first_url) == 1 && fixture.network.count(second_url) == 1, "only available downloads attempted");
        fixture.network.okay(second_url, "second downloaded bytes"); apply_modpack(plan, fixture.task());
        check(fixture.network.count(first_url) == 1 && fixture.network.count(second_url) == 2, "retry fetches only missing payload");
        check(fs::is_regular_file(fixture.version() / (fixture.id + ".json")), "retry publishes completed instance");
    });
    run("cancellation preserves verified cache for resume", [] {
        Fixture fixture; auto plan = fixture.plan(); auto task = fixture.task();
        task.progress = [&](const std::string &stage, std::int64_t done, std::int64_t) {
            if (stage == "下载并校验整合包文件" && done == 1) task.cancel->store(true);
        };
        rejected([&] { apply_modpack(plan, task); }, "cancelled"); fixture.unpublished();
        check(fs::is_regular_file(fixture.cached(plan)) && fixture.network.count(second_url) == 0, "cancelled install keeps only verified completed payload");
        apply_modpack(plan, fixture.task());
        check(fixture.network.count(first_url) == 1 && fixture.network.count(second_url) == 1, "resume skips completed download");
    });
    run("matching orphan transaction is recovered without losing its files", [] {
        Fixture fixture; auto original_plan = fixture.plan();
        const auto hash = original_plan.at("archive_sha512").get<std::string>();
        const auto base = fixture.root / ".solar-mrpack" / hash;
        const std::string token = "interrupted_transaction_01234567";
        atomic_write(base / "attempts" / fixture.id / "owner.json",
            Json{{"archive_sha512", hash}, {"id", fixture.id}, {"token", token}}.dump());
        atomic_write(fixture.game() / ".solar-modpack.json",
            Json{{"archive_sha512", hash}, {"id", fixture.id}, {"transaction", token}}.dump());
        atomic_write(fixture.game() / "config/manual-recovery.txt", "retain interrupted installation files");
        auto resumed_plan = fixture.plan();
        check(resumed_plan["archive_sha512"] == original_plan["archive_sha512"], "matching orphan permits a resumed plan");
        check(scan_instances({fixture.root})["instances"].empty(), "orphan game without version metadata remains unlisted");
        auto result = apply_modpack(resumed_plan, fixture.task());
        check(result["committed"] == true && result["modpack"]["transaction"] != token, "new transaction commits after recovery");
        check(read_file(fixture.game() / "config/first.txt") == "client override", "replacement instance contains verified new content");
        check(!fs::exists(fixture.game() / "config/manual-recovery.txt"), "orphan content is retained separately from new instance");
        unsigned recovered = 0;
        for (const auto &entry : fs::directory_iterator(base / "recovered")) {
            ++recovered;
            check(entry.is_directory() && read_file(entry.path() / "config/manual-recovery.txt") == "retain interrupted installation files",
                "original orphan files survive under recovered cache");
            check(read_json(entry.path() / ".solar-modpack.json")["transaction"] == token, "retained orphan receipt remains intact");
        }
        check(recovered == 1 && scan_instances({fixture.root})["instances"].size() == 1, "only newly committed instance is listed");
    });
    run("mismatched orphan ownership cannot recover an existing game directory", [] {
        for (int defect = 0; defect != 3; ++defect) {
            Fixture fixture; auto plan = fixture.plan(); const auto hash = plan.at("archive_sha512").get<std::string>();
            const auto base = fixture.root / ".solar-mrpack" / hash;
            const std::string token = "interrupted_transaction_01234567";
            Json owner{{"archive_sha512", hash}, {"id", fixture.id}, {"token", token}};
            Json receipt{{"archive_sha512", hash}, {"id", fixture.id}, {"transaction", token}};
            if (defect == 0) owner["id"] = "another-instance";
            if (defect == 1) owner["archive_sha512"] = std::string(128, '0');
            if (defect == 2) receipt["transaction"] = "unrelated_transaction_01234567";
            atomic_write(base / "attempts" / fixture.id / "owner.json", owner.dump());
            atomic_write(fixture.game() / ".solar-modpack.json", receipt.dump());
            atomic_write(fixture.game() / "keep.txt", "existing directory must remain untouched");
            rejected([&] { fixture.plan(); }, "instance_exists");
            rejected([&] { apply_modpack(plan, fixture.task()); }, "instance_exists");
            check(read_file(fixture.game() / "keep.txt") == "existing directory must remain untouched" &&
                read_json(fixture.game() / ".solar-modpack.json") == receipt, "unowned existing directory stays intact");
            check(!fs::exists(base / "recovered") && !fs::exists(fixture.version()), "unowned content is neither moved nor published");
            check(fixture.network.count(first_url) == 0, "mismatched ownership rejected before downloading");
        }
    });
    run("persistent empty lock files permit a new installation", [] {
        Fixture fixture; auto plan = fixture.plan(); const auto hash = plan.at("archive_sha512").get<std::string>();
        auto archive_lock = fixture.root / ".solar-mrpack" / hash / ".lock";
        auto instance_lock = fixture.root / ".solar-mrpack-locks" / (fixture.id + ".lock");
        atomic_write(archive_lock, ""); atomic_write(instance_lock, "");
        auto result = apply_modpack(plan, fixture.task());
        check(result["committed"] == true, "unheld persistent lock files do not block publication");
        check(fs::is_regular_file(archive_lock) && fs::file_size(archive_lock) == 0 &&
            fs::is_regular_file(instance_lock) && fs::file_size(instance_lock) == 0, "persistent locks remain ordinary empty files");
        check(scan_instances({fixture.root})["instances"].size() == 1, "lock-file reuse produces exactly one complete instance");
    });
    run("a second instance reuses shared files and keeps cache permissions intact", [] {
        Fixture fixture;
        const std::string library_url = "https://libraries.minecraft.net/fixture/library/1/library-1.jar";
        const std::string executable_url = "https://libraries.minecraft.net/fixture/tool/1/tool-1.bin";
        const std::string library_path = "fixture/library/1/library-1.jar";
        const std::string executable_path = "fixture/tool/1/tool-1.bin";
        const std::string asset = "shared resource bytes", library = "shared library bytes", executable = "shared executable bytes";
        const auto asset_hash = hash_bytes(asset, "sha1");
        const auto asset_url = "https://resources.download.minecraft.net/" + asset_hash.substr(0, 2) + "/" + asset_hash;
        const auto assets = Json{{"objects", {{"fixture/resource.txt", {{"hash", asset_hash}, {"size", asset.size()}}}}}}.dump();
        auto metadata = Json::parse(fixture.network.bytes(version_url));
        auto ordinary = descriptor(library_url, library), tool = descriptor(executable_url, executable);
        ordinary["path"] = library_path; tool["path"] = executable_path; tool["executable"] = true;
        metadata["libraries"] = Json::array({
            Json{{"name", "fixture:library:1"}, {"downloads", {{"artifact", ordinary}}}},
            Json{{"name", "fixture:tool:1"}, {"downloads", {{"artifact", tool}}}}});
        metadata["assetIndex"] = descriptor(assets_url, assets); metadata["assetIndex"]["id"] = "fixture-assets";
        const auto version = metadata.dump();
        fixture.network.okay(version_url, version);
        fixture.network.okay(manifest_url, Json{{"versions", Json::array({Json{{"id", "1.21.1"}, {"type", "release"},
            {"url", version_url}, {"sha1", hash_bytes(version, "sha1")}}})}}.dump());
        fixture.network.okay(assets_url, assets); fixture.network.okay(asset_url, asset);
        fixture.network.okay(library_url, library); fixture.network.okay(executable_url, executable);
        auto first_plan = fixture.plan(); auto first = apply_modpack(first_plan, fixture.task());
        const auto base = fixture.root / ".solar-mrpack" / first_plan.at("archive_sha512").get<std::string>();
        check(first["committed"] == true && !fs::exists(base / "game-root"), "successful import removes its duplicate prepared game tree");
        check(fs::is_regular_file(fixture.cached(first_plan)) && fs::is_regular_file(fixture.cached(first_plan, 1)),
            "successful import retains verified pack downloads");
        const auto tool_file = fixture.root / "libraries" / executable_path;
        // Permission repair in private preparation must not mutate the source
        // inode. A non-executable source makes an accidental hard link visible.
        fs::permissions(tool_file, fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
        const std::vector<fs::path> shared{fixture.root / "assets/indexes/fixture-assets.json",
            fixture.root / "assets/objects" / asset_hash.substr(0, 2) / asset_hash,
            fixture.root / "libraries" / library_path, tool_file};
        std::vector<std::string> hashes; std::vector<fs::perms> permissions;
        for (const auto &path : shared) { hashes.push_back(hash_file(path)); permissions.push_back(fs::status(path).permissions()); }
        auto request = fixture.request(); request["id"] = "fixture-second"; request["name"] = "Second fixture";
        auto second = apply_modpack(plan_modpack(request, fixture.task()), fixture.task());
        check(second["committed"] == true && !fs::exists(base / "game-root"), "warm import also removes its prepared game tree");
        check(fixture.network.count(first_url) == 1 && fixture.network.count(second_url) == 1, "warm import reuses verified pack downloads");
        check(fixture.network.count(assets_url) == 1 && fixture.network.count(asset_url) == 1 &&
            fixture.network.count(library_url) == 1 && fixture.network.count(executable_url) == 1,
            "warm import reuses shared asset indexes objects and libraries without refetching");
        for (std::size_t i = 0; i != shared.size(); ++i)
            check(hash_file(shared[i]) == hashes[i] && fs::status(shared[i]).permissions() == permissions[i],
                "warm import preserves source cache bytes and modes");
        check(read_file(fixture.game() / "config/first.txt") == "client override" &&
            read_file(fixture.root / "instances/fixture-second/config/second.txt") == "second downloaded bytes",
            "both isolated instances retain their own complete game content");
        check(scan_instances({fixture.root})["instances"].size() == 2, "only the two published instances are discoverable");
    });
    run("an empty legacy private game lock is cleared for resume", [] {
        Fixture fixture; auto plan = fixture.plan(); const auto hash = plan.at("archive_sha512").get<std::string>();
        const auto prepared = fixture.root / ".solar-mrpack" / hash / "game-root";
        const auto lock = prepared / "versions" / ("mrpack-" + hash.substr(0, 16)) / ".solar-install-lock";
        fs::create_directories(lock);
        auto result = apply_modpack(plan, fixture.task());
        check(result["committed"] == true && !fs::exists(prepared), "empty legacy lock does not block resumed publication");
        check(scan_instances({fixture.root})["instances"].size() == 1, "legacy lock recovery publishes exactly one instance");
    });
    run("nonempty or foreign legacy game locks are preserved and rejected", [] {
        for (int kind = 0; kind != 2; ++kind) {
            Fixture fixture; auto plan = fixture.plan(); const auto hash = plan.at("archive_sha512").get<std::string>();
            const auto prepared = fixture.root / ".solar-mrpack" / hash / "game-root";
            const auto lock = prepared / "versions" / ("mrpack-" + hash.substr(0, 16)) / ".solar-install-lock";
            if (kind == 0) atomic_write(lock / "foreign-owner.txt", "retain foreign lock contents");
            else atomic_write(lock, "foreign lock file");
            rejected([&] { apply_modpack(plan, fixture.task()); }, "unsafe_cache"); fixture.unpublished();
            check(kind == 0 ? read_file(lock / "foreign-owner.txt") == "retain foreign lock contents" : read_file(lock) == "foreign lock file",
                "foreign legacy lock is not removed or altered");
            check(fixture.network.count(client_url) == 0 && fixture.network.count(assets_url) == 0,
                "foreign legacy lock stops game preparation");
            check(fs::is_regular_file(fixture.cached(plan)), "failed legacy recovery retains verified pack payloads");
        }
    });
    run("cancelling before shared promotion leaves the prepared game unlisted", [] {
        Fixture fixture; auto plan = fixture.plan(); auto task = fixture.task(); bool reached_promotion = false;
        task.progress = [&](const std::string &stage, std::int64_t, std::int64_t) {
            if (stage == "准备独立游戏实例") { reached_promotion = true; task.cancel->store(true); }
        };
        rejected([&] { apply_modpack(plan, task); }, "cancelled"); fixture.unpublished();
        check(reached_promotion && fixture.network.count(client_url) == 1 && fixture.network.count(assets_url) == 1,
            "cancellation reaches publication stage after real game preparation");
        check(scan_instances({fixture.root})["instances"].empty(), "hidden prepared version is never discovered");
        check(fs::is_directory(fixture.root / ".solar-mrpack" / plan.at("archive_sha512").get<std::string>() / "game-root"),
            "failed publication retains prepared game files for resume");
        check(!fs::exists(fixture.root / "assets/indexes/fixture-assets.json"), "cancelled promotion publishes no shared index");
        auto result = apply_modpack(plan, fixture.task());
        check(result["committed"] == true && fixture.network.count(first_url) == 1 && fixture.network.count(second_url) == 1,
            "post-preparation cancellation resumes from verified payload cache");
    });
    run("changed archive invalidates reviewed plan", [] {
        Fixture fixture; auto plan = fixture.plan(); fixture.document["summary"] = "changed after review"; fixture.rewrite_archive();
        rejected([&] { apply_modpack(plan, fixture.task()); }, "plan_stale"); fixture.unpublished();
        check(!fs::exists(fixture.root) && fixture.network.count(first_url) == 0, "stale plan stops before cache writes and downloads");
    });
    run("tampered descriptor fields cannot redirect filesystem writes", [] {
        Fixture fixture; auto plan = fixture.plan(); auto escape = fixture.temp / "escape.txt";
        plan["directory"] = (fixture.temp / "unreviewed-game").u8string();
        plan["files"] = Json::array({Json{{"path", "../../escape.txt"}, {"sha512", std::string(128, '0')},
            {"size", 1}, {"urls", Json::array({"https://example.com/evil"})}}});
        plan["overrides"] = Json::array({Json{{"entry", "overrides/options.txt"}, {"path", "../../escape.txt"}}});
        plan["pack"]["minecraft"] = "unreviewed-version";
        apply_modpack(plan, fixture.task());
        check(!fs::exists(escape) && !fs::exists(fixture.temp / "unreviewed-game"), "untrusted descriptor destinations ignored");
        check(read_file(fixture.game() / "config/second.txt") == "second downloaded bytes", "archive descriptors recomputed at apply");
    });
    run("occupied target directories files and symlinks are never overwritten", [] {
        for (const auto *parent : {"versions", "instances"}) for (int kind = 0; kind < 3; ++kind) {
            Fixture fixture; auto plan = fixture.plan(); auto target = fixture.root / parent / fixture.id;
            fs::create_directories(target.parent_path());
            auto outside = fixture.temp / "existing-outside";
            if (kind == 0) { fs::create_directories(target); atomic_write(target / "keep.txt", "existing"); }
            if (kind == 1) atomic_write(target, "existing file");
            if (kind == 2) {
                fs::create_directories(outside); atomic_write(outside / "keep.txt", "existing");
                std::error_code error; fs::create_directory_symlink(outside, target, error);
                if (error) { std::cerr << "SKIP symlink fixture: " << error.message() << '\n'; continue; }
            }
            rejected([&] { apply_modpack(plan, fixture.task()); });
            rejected([&] { fixture.plan(); });
            check(fixture.network.count(first_url) == 0, "occupied target rejected before downloading");
            check(kind == 1 ? read_file(target) == "existing file" : read_file((kind == 2 ? outside : target) / "keep.txt") == "existing",
                "preexisting target retained exactly");
            check(!fs::is_regular_file(fixture.version() / (fixture.id + ".json")), "no version metadata published into occupied targets");
        }
    });
    run("shared game files are never replaced by differing bytes", [] {
        Fixture fixture; auto plan = fixture.plan(); auto index = fixture.root / "assets/indexes/fixture-assets.json";
        atomic_write(index, "existing unrelated asset index");
        rejected([&] { apply_modpack(plan, fixture.task()); }, "shared_file_conflict"); fixture.unpublished();
        check(read_file(index) == "existing unrelated asset index", "shared conflict leaves original intact");
        check(fs::is_regular_file(fixture.cached(plan)), "payload cache retained after shared conflict");
    });
    run("primary URL fallback and allowed GitHub asset redirect", [] {
        Fixture fixture;
        const std::string fallback = "https://github.com/fixture/project/releases/download/1/first.txt";
        const std::string asset = "https://release-assets.githubusercontent.com/fixture/first.txt";
        fixture.document["files"][0]["downloads"].push_back(fallback); fixture.rewrite_archive();
        fixture.network.set(first_url, {503, "unavailable", ""}); fixture.network.set(fallback, {302, "", asset});
        fixture.network.okay(asset, "first downloaded bytes");
        apply_modpack(fixture.plan(), fixture.task());
        check(fixture.network.count(first_url) == 1 && fixture.network.count(fallback) == 1 && fixture.network.count(asset) == 1,
            "fallback and whitelisted redirect fetched once");
        check(read_file(fixture.game() / "config/first.txt") == "client override", "fallback payload verified then layered");
    });
    run("untrusted redirect is rejected before connecting", [] {
        Fixture fixture; auto plan = fixture.plan(); const std::string denied = "https://example.com/evil";
        fixture.network.set(first_url, {302, "", denied}); fixture.network.okay(denied, "first downloaded bytes");
        rejected([&] { apply_modpack(plan, fixture.task()); }, "download_host"); fixture.unpublished();
        check(fixture.network.count(denied) == 0, "denied redirect never reaches transport");
    });
    run("redirect chains have a bounded request count", [] {
        Fixture fixture; auto plan = fixture.plan(); fixture.network.set(first_url, {302, "", "https://cdn.modrinth.com/loop/1"});
        for (int i = 1; i != 5; ++i) fixture.network.set("https://cdn.modrinth.com/loop/" + std::to_string(i),
            {302, "", "https://cdn.modrinth.com/loop/" + std::to_string(i + 1)});
        rejected([&] { apply_modpack(plan, fixture.task()); }, "redirect_limit"); fixture.unpublished();
        check(fixture.network.count(first_url) == 1 && fixture.network.count("https://cdn.modrinth.com/loop/3") == 1 &&
            fixture.network.count("https://cdn.modrinth.com/loop/4") == 0, "at most three redirect hops followed");
    });
    run("size SHA512 and SHA1 verification prevent publication", [] {
        for (int defect = 0; defect != 3; ++defect) {
            Fixture fixture;
            if (defect == 0) fixture.network.okay(first_url, "wrong length");
            if (defect == 1) fixture.network.okay(first_url, std::string(std::string("first downloaded bytes").size(), 'x'));
            if (defect == 2) {
                fixture.document["files"][0]["hashes"]["sha1"] = std::string(40, '0'); fixture.rewrite_archive();
            }
            auto plan = fixture.plan(); rejected([&] { apply_modpack(plan, fixture.task()); }, "verification_failed"); fixture.unpublished();
            check(!fs::exists(fixture.cached(plan)), "unverified download never enters reusable cache");
        }
    });
    run("corrupt override CRC fails before publication", [] {
        Fixture fixture;
        auto archive = read_file(fixture.archive); auto position = archive.find("client override");
        check(position != archive.npos, "override fixture payload found"); archive[position] ^= 1; atomic_write(fixture.archive, archive);
        auto plan = fixture.plan(); rejected([&] { apply_modpack(plan, fixture.task()); }, "crc_mismatch"); fixture.unpublished();
        check(fs::is_regular_file(fixture.cached(plan)), "verified payload cache survives override corruption");
    });
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
