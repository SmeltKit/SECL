// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/accounts.hpp"
#include "solar/engine.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <zlib.h>
using namespace solar;
using namespace solar::engine;
static int n = 0;
static void check(bool ok, const char *name) {
    ++n;
    if (!ok)
        throw std::runtime_error(name);
}
static void rejects(const std::function<void()> &f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "expected rejection");
}
static void word(std::string &s, unsigned value, int count) {
    while (count--) {
        s.push_back(char(value & 255));
        value >>= 8;
    }
}
static std::string zip(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string out, central;
    for (auto &f : files) {
        auto offset = out.size();
        auto crc =
            crc32(0, reinterpret_cast<const Bytef *>(f.second.data()), unsigned(f.second.size()));
        word(out, 0x04034b50, 4);
        word(out, 20, 2);
        word(out, 0, 2);
        word(out, 0, 2);
        word(out, 0, 4);
        word(out, crc, 4);
        word(out, unsigned(f.second.size()), 4);
        word(out, unsigned(f.second.size()), 4);
        word(out, unsigned(f.first.size()), 2);
        word(out, 0, 2);
        out += f.first + f.second;
        word(central, 0x02014b50, 4);
        word(central, 20, 2);
        word(central, 20, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, crc, 4);
        word(central, unsigned(f.second.size()), 4);
        word(central, unsigned(f.second.size()), 4);
        word(central, unsigned(f.first.size()), 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, unsigned(offset), 4);
        central += f.first;
    }
    auto offset = out.size();
    out += central;
    word(out, 0x06054b50, 4);
    word(out, 0, 4);
    word(out, unsigned(files.size()), 2);
    word(out, unsigned(files.size()), 2);
    word(out, unsigned(central.size()), 4);
    word(out, unsigned(offset), 4);
    word(out, 0, 2);
    return out;
}
static Json mod(const std::string &id, const Json &dependencies = Json::object()) {
    return {{"schemaVersion", 1},
            {"id", id},
            {"name", id},
            {"version", "1.2.3"},
            {"depends", dependencies}};
}
static void tar_gz(const fs::path &path, const std::string &name) {
    std::string h(512, '\0');
    std::copy(name.begin(), name.end(), h.begin());
    auto oct = [&](size_t at, size_t size, unsigned value) {
        std::string field(size, '\0');
        std::snprintf(field.data(), field.size(), "%0*o", int(size - 1), value);
        h.replace(at, size, field);
    };
    oct(100, 8, 0755);
    oct(108, 8, 0);
    oct(116, 8, 0);
    oct(124, 12, 7);
    oct(136, 12, 0);
    h.replace(148, 8, "        ");
    h[156] = '0';
    h.replace(257, 6, std::string("ustar\0", 6));
    unsigned sum = 0;
    for (unsigned char c : h)
        sum += c;
    oct(148, 8, sum);
    auto bytes = h + "payload" + std::string(505 + 1024, '\0');
#ifdef _WIN32
    auto file = gzopen_w(path.c_str(), "wb");
#else
    auto file = gzopen(path.c_str(), "wb");
#endif
    if (!file)
        throw std::runtime_error("gzip fixture");
    gzwrite(file, bytes.data(), unsigned(bytes.size()));
    gzclose(file);
}
int main(int argc, char **argv) {
    if (argc > 1 && std::string(argv[1]) == "-XshowSettings:properties") {
        std::cout << "java.version = 21.0.1\njava.runtime.version = "
                     "21.0.1+12\njava.vendor = Test fixture\nos.arch = "
                  << platform().arch << "\n";
        if (const auto *home = std::getenv("SOLAR_TEST_JAVA_HOME"))
            std::cout << "java.home = " << home << "\n";
        return 0;
    }
    auto temp = fs::temp_directory_path() / ("solar-engine-" + random_url_token());
    try {
        fs::create_directories(temp);
#ifdef _WIN32
        _putenv_s("SOLAR_DATA_DIR", (temp / "launcher-data").u8string().c_str());
#else
        setenv("SOLAR_DATA_DIR", (temp / "launcher-data").c_str(), 1);
#endif
        check(hash_bytes("abc", "sha256") ==
                  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "SHA256");
        check(hash_bytes("abc", "sha1") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA1");
        rejects([&] { checked_path(temp, "../escape"); });
        rejects([&] { checked_path(temp, "/absolute"); });
        auto z = zip({{"test.txt", "payload"}});
        check(zip_entry_bytes(z, "test.txt") == "payload", "ZIP roundtrip");
        auto broken = z;
        broken[38] = '!';
        rejects([&] { zip_entry_bytes(broken, "test.txt"); });
        atomic_write(temp / "safe.zip", z);
        extract_zip(temp / "safe.zip", temp / "output");
        check(read_file(temp / "output/test.txt") == "payload", "ZIP extraction");
        atomic_write(temp / "unsafe.zip", zip({{"../escape", "x"}}));
        rejects([&] { extract_zip(temp / "unsafe.zip", temp / "unsafe"); });
        tar_gz(temp / "runtime.tar.gz", "runtime/bin/example");
        extract_java_archive(temp / "runtime.tar.gz", temp / "java-output");
        check(read_file(temp / "java-output/runtime/bin/example") == "payload",
              "TAR.GZ Java archive roundtrip");
        tar_gz(temp / "bad-runtime.tar.gz", "../escape");
        rejects([&] { extract_java_archive(temp / "bad-runtime.tar.gz", temp / "bad-java"); });
        auto j = parse_java("java.version = 17.0.12\njava.runtime.version = 17.0.12+7\njava.vendor "
                            "= Eclipse Adoptium\nos.arch = arm64\n");
        check(j.at("major") == 17 && j.at("arch") == "aarch64" &&
                  j.at("runtime_version") == "17.0.12+7",
              "Java build properties");
        check(parse_java("openjdk version \"1.8.0_402\"").value("major", 0) == 8,
              "Java 8 legacy version");
        auto java_root = temp / "java aliases";
        auto java_home = java_root / "runtime";
        auto executable = platform().os == "windows" ? "java.exe" : "java";
        auto actual_java = java_home / "bin" / executable;
        auto wrapper_java = java_root / "wrapper" / executable;
        fs::create_directories(actual_java.parent_path());
        fs::create_directories(wrapper_java.parent_path());
        fs::copy_file(fs::u8path(argv[0]), actual_java);
        fs::copy_file(fs::u8path(argv[0]), wrapper_java);
        check(java_runtime_executable(wrapper_java, "java.home = " + java_home.u8string() + "\n") ==
                  fs::canonical(actual_java),
              "Java launcher resolves actual runtime home including spaces");
        check(java_runtime_executable(wrapper_java, "java.home = /missing/java/home\n") ==
                  fs::canonical(wrapper_java),
              "Unavailable reported home retains working executable");
#ifdef _WIN32
        _putenv_s("SOLAR_TEST_JAVA_HOME", java_home.u8string().c_str());
#else
        setenv("SOLAR_TEST_JAVA_HOME", java_home.c_str(), 1);
#endif
        auto scanned_java = scan_java({java_root}, false, Task{});
        check(scanned_java.at("runtimes").size() == 1 &&
                  scanned_java.at("runtimes")[0].at("path") == fs::canonical(actual_java).u8string(),
              "Runtime scan deduplicates launcher and actual Java executable");
#ifdef _WIN32
        _putenv_s("SOLAR_TEST_JAVA_HOME", "");
#else
        unsetenv("SOLAR_TEST_JAVA_HOME");
#endif
        check(version_matches(">=1.2.0 <2.0.0", "1.2.3") == 1, "Fabric range");
        check(version_matches("[1.0,2.0)", "2.0") == 0, "Maven upper bound");
        check(version_matches("^0.2.3", "0.3.0") == 0, "caret zero major");
        auto mem = recommend_memory(
            {{"total_mib", 16384}, {"available_mib", 10000}},
            {{"game_version", "1.20.1"}, {"enabled_mods", 80}, {"java_arch", "x64"}});
        check(mem["maximum_mib"] == 6144, "mod memory recommendation");
        check(recommend_memory({{"total_mib", 4096}, {"available_mib", 3000}},
                               {{"java_arch", "x86"}})["maximum_mib"] == 768,
              "32bit memory ceiling");
        rejects([] { validate_launch_options({{"jvm_arguments", "-Xmx8G"}}); });
        rejects([] { validate_launch_options({{"game_arguments", "--accessToken fake"}}); });
        Task source;
        source.time_zone = "Asia/Shanghai";
        auto candidates =
            download_candidates("https://resources.download.minecraft.net/ab/abcdef", source);
        check(candidates.size() == 2 && candidates[0].find("bangbang93") != std::string::npos,
              "China mirror preference");
        source.time_zone = "Europe/Paris";
        check(download_candidates("https://resources.download.minecraft.net/ab/abcdef", source)[0]
                      .find("resources.download.minecraft.net") != std::string::npos,
              "official elsewhere");
        source.cancel->store(true);
        rejects([&] { source.check(); });
        auto child = zip({{"fabric.mod.json", mod("nested").dump()}});
        auto main = mod("primary", {{"nested", ">=1.0.0"}, {"minecraft", "1.20.1"}});
        main["jars"] = Json::array({{{"file", "nested.jar"}}});
        auto bytes = zip({{"fabric.mod.json", main.dump()}, {"nested.jar", child}});
        auto metadata = inspect_mod_bytes(bytes, "primary.jar", "fabric");
        check(metadata["known"] == true && metadata["ids"].size() == 2 &&
                  metadata["provided_versions"]["nested"] == "1.2.3",
              "recursive embedded mod");
        Json instance{{"version", "1.20.1"},
                      {"loader", "fabric"},
                      {"loader_version", "0.16.10"},
                      {"java_major", 17},
                      {"game_directory", temp.u8string()}};
        check(check_mods(instance, Json::array({metadata}))["errors"].empty(),
              "embedded dependency reuse");
        check(!check_mods(instance, Json::array({metadata, metadata}))["errors"].empty(),
              "duplicate ID rejection");
        auto wrong = instance;
        wrong["loader"] = "neoforge";
        check(!check_mods(wrong, Json::array({metadata}))["errors"].empty(), "loader mismatch");
        atomic_write(temp / "mods/primary.jar", bytes);
        atomic_write(temp / "mods/.m2cl-mods.json",
                     Json{{"files", Json::array({{{"fileName", "primary.jar"},
                                                  {"sha512", hash_bytes(bytes)},
                                                  {"projectId", "legacy-project"},
                                                  {"versionId", "legacy-version"}}})}}
                         .dump());
        check(local_mods(temp, "fabric")[0]["project_id"] == "legacy-project",
              "M2CL inventory migration");
        set_mods_enabled(instance, {"primary.jar"}, false);
        check(fs::exists(temp / "mods/primary.jar.disabled"), "disable batch");
        set_mods_enabled(instance, {"primary.jar.disabled"}, true);
        check(fs::exists(temp / "mods/primary.jar"), "enable batch");
        atomic_write(temp / "resources.zip",
                     zip({{"pack.mcmeta", R"({"pack":{"pack_format":15,"description":"fixture"}})"},
                          {"assets/test.txt", "test"}}));
        check(inspect_pack(temp / "resources.zip", "resourcepack")["valid"] == true,
              "resource pack detection");
        atomic_write(temp / "shader.zip", zip({{"shaders/main.fsh", "#version 150\n"}}));
        check(inspect_pack(temp / "shader.zip", "shader")["valid"] == true, "shader detection");
        Json pack_file{{"filename", "resource-test.zip"}, {"project_id", "test-pack"},
                       {"version", "1.0"}, {"game_versions", Json::array({"1.20.1"})},
                       {"sha512", hash_file(temp / "resources.zip")},
                       {"url", "https://example.invalid/test.zip"}};
        auto pack_plan = plan_pack_install(instance, pack_file, "resourcepack");
        fs::create_directories(temp / "resourcepacks");
        fs::copy_file(temp / "resources.zip", temp / "resourcepacks/resource-test.zip");
        rejects([&] { apply_pack_plan(pack_plan); });
        pack_plan = plan_pack_install(instance, pack_file, "resourcepack");
        check(pack_plan.at("reuse") == true && apply_pack_plan(pack_plan).at("reused") == true,
              "Resource pack reuse avoids redownload");
        auto different_pack = pack_file;
        different_pack["sha512"] = std::string(128, '0');
        rejects([&] { apply_pack_plan(plan_pack_install(instance, different_pack, "resourcepack")); });
        auto wrong_pack = pack_file;
        wrong_pack["game_versions"] = Json::array({"1.19"});
        rejects([&] { plan_pack_install(instance, wrong_pack, "resourcepack"); });
        wrong_pack = pack_file;
        wrong_pack["filename"] = "../escaped.zip";
        rejects([&] { plan_pack_install(instance, wrong_pack, "resourcepack"); });
        rejects([&] { apply_mod_plan({{"conflicts", Json::array({{{"filename", "bad.jar"}}})}}); });
        Json nullable_root{{"project_id", "nullable-project"}, {"version_id", "nullable-version"},
                           {"filename", "nullable.jar"}, {"game_versions", Json::array({"1.20.1"})},
                           {"loaders", Json::array({"fabric"})},
                           {"dependencies", Json::array({{{"dependency_type", "required"},
                                                          {"project_id", nullptr},
                                                          {"version_id", nullptr}}})}};
        auto empty_instance = instance;
        empty_instance["game_directory"] = (temp / "empty-plan").u8string();
        check(plan_mod_install(empty_instance, nullable_root)["files"].size() == 1,
              "Modrinth nullable dependency IDs do not break planning");
        check(game_channel("1.20.1") == "release" && game_channel("24w14potato") == "special" &&
                  game_channel("1.20-pre1") == "snapshot",
              "separate game channels");
        check(maven_path("net.example:artifact:1.2.3:client@zip") ==
                  "net/example/artifact/1.2.3/artifact-1.2.3-client.zip",
              "Maven classifier extension");
        rejects([] { maven_path("../../bad:a:1"); });
        check(optifine_compatibility(
                  {{"game", "1.20.1"}, {"forge", "47.2.18"}},
                  {{"version", "1.20.1"}, {"loader", "forge"}, {"loader_version", "47.2.18"}})
                  .empty(),
              "exact Forge OptiFine pair");
        check(!optifine_compatibility({{"game", "1.20.1"}}, instance).empty(),
              "Fabric OptiFine gate");
        save_instance_settings(temp,
                               {{"launch", {{"auto_memory", false}, {"max_memory_mib", 4096}}}});
        check(load_instance_settings(temp)["launch"]["max_memory_mib"] == 4096,
              "per-instance settings");
        auto stale =
            Json{{"target", instance}, {"expected_files", {{"primary.jar", "not-current-hash"}}}};
        rejects([&] { apply_mod_plan(stale); });
        check(fs::exists(temp / "mods/primary.jar"), "stale plan leaves files intact");
        check(dispatch({{"op", "capabilities"}}).size() >= 35, "engine exposed over JSON");

        auto account = Json{{"id", "microsoft:0123456789abcdef0123456789abcdef"},
                            {"uuid", "0123456789abcdef0123456789abcdef"},
                            {"name", "TestPlayer"},
                            {"type", "microsoft"},
                            {"client_id", "00000000-0000-4000-8000-000000000001"},
                            {"access_token", "MUST_NOT_PERSIST"}};
        save_accounts(temp / "accounts", Json::array({account}), account.at("id"));
        auto saved = read_file(temp / "accounts/accounts.json");
        check(saved.find("MUST_NOT_PERSIST") == saved.npos, "account JSON strips secrets");
        check(load_accounts(temp / "accounts")["accounts"][0]["needs_login"] == true,
              "persisted account is not authenticated");
        account["type"] = "offline";
        rejects([&] { save_accounts(temp / "accounts", Json::array({account}), ""); });
        SecretRedactor redactor({"SECRET_TOKEN"});
        auto text = redactor.append("prefix SECRET_") + redactor.append("TOKEN suffix", true);
        check(text == "prefix [REDACTED] suffix", "streaming split-token redaction");
        GameLogDecoder decoder;
        auto log = decoder.append("<log4j:Event thread=\"main\" "
                                  "level=\"INFO\"><log4j:Message><![CDATA[hello");
        log += decoder.append("]]></log4j:Message></log4j:Event>", true);
        check(log.find("[main/INFO] hello") != log.npos, "incremental XML log decoding");
        auto game = temp / "game";
        atomic_write(game / "assets/indexes/test.json", R"({"objects":{}})");
        atomic_write(game / "versions/test/test.jar", "launch fixture only");
        Json vm{
            {"id", "test"},
            {"mainClass", "net.example.Test"},
            {"type", "release"},
            {"javaVersion", {{"majorVersion", 17}}},
            {"assetIndex", {{"id", "test"}}},
            {"libraries", Json::array()},
            {"arguments",
             {{"jvm", Json::array({"-cp", "${classpath}"})},
              {"game", Json::array({"--username", "${auth_player_name}", "--accessToken",
                                    "${auth_access_token}", "--gameDir", "${game_directory}"})}}}};
        atomic_write(game / "versions/test/test.json", vm.dump());
        save_instance_settings(game / "versions/test", {{"launch",
                                                         {{"auto_memory", false},
                                                          {"max_memory_mib", 2048},
                                                          {"min_memory_mib", 512},
                                                          {"custom_resolution", true},
                                                          {"width", 1280},
                                                          {"height", 720}}}});
        Identity identity;
        identity.name = "TestPlayer";
        identity.uuid = "0123456789abcdef0123456789abcdef";
        identity.token = "TEST_ONLY_NOT_A_REAL_TOKEN";
        identity.owned = true;
        identity.expires = now() + 3600;
        auto command =
            launch_command(game, "test", fs::canonical(fs::u8path(argv[0])), identity, platform());
        check(std::find(command.arguments.begin(), command.arguments.end(), "-Xmx2048M") !=
                      command.arguments.end() &&
                  std::find(command.arguments.begin(), command.arguments.end(), "-Xms512M") !=
                      command.arguments.end(),
              "per-instance heap options");
        check(std::find(command.arguments.begin(), command.arguments.end(), "1280") !=
                      command.arguments.end() &&
                  std::find(command.arguments.begin(), command.arguments.end(), identity.token) !=
                      command.arguments.end(),
              "verified launch argument expansion fixture");
        fs::remove_all(temp);
        std::cout << "PASS: " << n << " engine migration checks\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << n << ": " << e.what() << "\n";
        std::error_code ec;
        fs::remove_all(temp, ec);
        return 1;
    }
}
