// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <zlib.h>

using namespace solar;
using namespace solar::engine;
namespace {
int checks = 0, failures = 0;
void check(bool passed, const char *message) {
    ++checks;
    if (!passed) throw std::runtime_error(message);
}
void run(const char *name, const std::function<void()> &test) {
    try { test(); }
    catch (const std::exception &e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
void word(std::string &bytes, unsigned value, int count) {
    while (count--) { bytes.push_back(char(value & 255)); value >>= 8; }
}
std::string zip(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string bytes, central;
    for (auto &file : files) {
        auto offset = unsigned(bytes.size()), size = unsigned(file.second.size());
        auto crc = crc32(0, reinterpret_cast<const Bytef *>(file.second.data()), size);
        word(bytes, 0x04034b50, 4); word(bytes, 20, 2); word(bytes, 0, 2); word(bytes, 0, 2);
        word(bytes, 0, 4); word(bytes, crc, 4); word(bytes, size, 4); word(bytes, size, 4);
        word(bytes, unsigned(file.first.size()), 2); word(bytes, 0, 2); bytes += file.first + file.second;
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, crc, 4);
        word(central, size, 4); word(central, size, 4); word(central, unsigned(file.first.size()), 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 4); word(central, offset, 4); central += file.first;
    }
    auto offset = unsigned(bytes.size()); bytes += central;
    word(bytes, 0x06054b50, 4); word(bytes, 0, 2); word(bytes, 0, 2);
    word(bytes, unsigned(files.size()), 2); word(bytes, unsigned(files.size()), 2);
    word(bytes, unsigned(central.size()), 4); word(bytes, offset, 4); word(bytes, 0, 2);
    return bytes;
}
Json fabric(const std::string &id = "demo", const std::string &version = "1.2.3") {
    return {{"schemaVersion", 1}, {"id", id}, {"version", version}};
}
Json quilt(const std::string &id = "demo") {
    return {{"schema_version", 1}, {"quilt_loader", {{"id", id}, {"group", "org.example"}, {"version", "1.2.3"}}}};
}
Json inspect_json(const Json &value, const std::string &loader = "fabric") {
    return inspect_mod_bytes(zip({{loader + ".mod.json", value.dump()}}), "fixture.jar", loader);
}
bool warned(const Json &mod, const std::string &code) {
    for (auto &warning : mod.at("metadata_warnings")) if (warning.at("code") == code) return true;
    return false;
}
const char *forge_header = "modLoader='javafml'\nloaderVersion='[47,)'\nlicense='MIT'\n";
std::string forge_mod(const std::string &id, const std::string &version = "1.0") {
    return "[[mods]]\nmodId='" + id + "'\nversion='" + version + "'\n";
}
Json forge(const std::string &toml, const std::string &manifest = "", const std::string &loader = "forge") {
    auto path = loader == "neoforge" ? "META-INF/neoforge.mods.toml" : "META-INF/mods.toml";
    return inspect_mod_bytes(zip({{path, toml}, {"META-INF/MANIFEST.MF", manifest}}), "fixture.jar", loader);
}
Json jarjar(const std::string &path, const std::string &artifact = "library") {
    return {{"identifier", {{"group", "org.example"}, {"artifact", artifact}}},
            {"version", {{"range", "[1,2)"}, {"artifactVersion", "1.5"}}},
            {"path", path}, {"isObfuscated", false}};
}
} // namespace

int main() {
    run("Fabric clauses and optional hints", [] {
        auto doc = fabric();
        doc["depends"] = {{"minecraft", Json::array({"1.20.x", "1.21.x"})}, {"fabricloader", ">=0.16"}};
        doc["provides"] = Json::array({"alias"}); doc["breaks"] = {{"bad", "<2"}};
        doc["recommends"] = {{"helpful", "*"}}; doc["environment"] = "client";
        auto mod = inspect_json(doc);
        check(mod["known"], "Fabric recognized");
        check(mod["requirements"]["minecraft"].is_array(), "Fabric version alternatives retained");
        check(mod["provided_versions"]["alias"] == "1.2.3", "Fabric alias version inherits mod version");
        check(mod["dependency_clauses"].size() == 4, "dependency kinds preserved");
        check(mod["environment"] == "client", "physical client environment retained");
        check(mod["own_ids"] == mod["ids"], "root provider identities explicit");
    });
    run("Quilt aliases, groups, nested expressions and conditionals", [] {
        auto doc = quilt();
        doc["quilt_loader"]["provides"] = Json::array({"inherit", {{"id", "other.group:alias"}, {"version", "4.5.6"}}});
        doc["quilt_loader"]["depends"] = Json::array({
            "quilt_loader", Json::array({"choice_one", "choice_two"}),
            {{"id", "conditional"}, {"optional", true}, {"unless", Json::array({"replacement", "alternative"})},
             {"versions", {{"all", Json::array({">=1", Json{{"any", Json::array({"<2", ">=3"})}}})}}}}});
        doc["quilt_loader"]["breaks"] = Json::array({Json::array({"bad_one", "bad_two"}), {{"id", "bad"}, {"versions", "<1"}}});
        auto mod = inspect_json(doc, "quilt");
        check(mod["known"], "Quilt recognized");
        check(mod["provided_versions"]["alias"] == "4.5.6", "Quilt object alias custom version preserved");
        check(mod["provided_versions"]["other.group:alias"] == "4.5.6", "Quilt qualified alias preserved");
        check(mod["provided_versions"]["org.example:inherit"] == "1.2.3", "Quilt default provided group inherited");
        auto clauses = mod["dependency_clauses"];
        check(clauses[1]["expression"]["op"] == "any", "Quilt nested depends is disjunction");
        check(clauses[3]["expression"]["op"] == "all", "Quilt grouped breaks is conjunction");
        check(clauses[2]["expression"]["optional"] == true, "optional constraint retained");
        check(clauses[2]["expression"]["unless"]["op"] == "any", "unless alternatives retained");
        check(clauses[2]["expression"]["range"]["all"].size() == 2, "Quilt compound versions retained");
        check(mod["breaks"].is_object() && mod["breaks"]["bad"] == "<1", "Quilt breaks normalized to legacy map");
        check(!mod["requirements"].contains("conditional"), "conditional constraint not flattened to hard requirement");
    });
    run("Quilt duplicate constraints and unsupported semantics visible", [] {
        auto doc = quilt();
        doc["quilt_loader"]["depends"] = Json::array({{{"id", "range"}, {"versions", ">=1"}},
            {{"id", "range"}, {"versions", "<2"}}, {{"id", "future"}, {"plugin_field", true}}});
        doc["quilt_loader"]["required_features"] = Json::array({"filesystem_feature"});
        auto mod = inspect_json(doc, "quilt");
        check(mod["known"], "unmodeled plugin semantics preserve inspectable identity");
        check(mod["requirements"]["range"]["all"].size() == 2, "same-id Quilt clauses never overwrite");
        check(warned(mod, "unsupported_dependency_field"), "dependency extension surfaced");
        check(warned(mod, "unsupported_loader_feature"), "loader feature surfaced");
        doc["quilt_loader"]["depends"][0]["versions"] = Json::array({">=1", "<2"});
        check(warned(inspect_json(doc, "quilt"), "deprecated_quilt_version_array"), "deprecated OR arrays surfaced");
    });
    run("Real TOML syntax and manifest substitutions", [] {
        std::string toml = std::string(forge_header) +
            "[[mods]]\nmodId='demo'\nversion='${file.jarVersion}'\ndisplayName=\"Quoted \\\"title\\\"\"\n"
            "description='''\n[[mods]]\nmodId='fake'\n[[dependencies.fake]]\nmodId='missing'\n'''\n"
            "# [[mods]] modId='also_fake'\n"
            "[[dependencies.\"demo\"]]\nmodId='minecraft'\nmandatory=true\nversionRange='[1.20,1.21)'\n";
        auto mod = forge(toml, "Manifest-Version: 1.0\r\nImplementation-Version: 2.4.\r\n 6\r\n\r\nName: ignored\r\nImplementation-Version: 9\r\n");
        check(mod["known"], "valid TOML recognized");
        check(mod["own_ids"].size() == 1 && mod["own_ids"][0] == "demo", "headers inside descriptions/comments ignored");
        check(mod["name"] == "Quoted \"title\"", "TOML escaped strings decoded");
        check(mod["version"] == "2.4.6", "manifest continuation line resolved using main section");
        check(mod["requirements"].contains("minecraft"), "quoted TOML dependency table key parsed");
        check(mod["language_loader"]["version_source"] == "forge_major", "Forge language loader uses Forge major");
    });
    run("Forge properties, missing versions and invalid TOML", [] {
        auto mod = forge(std::string(forge_header) + "properties = {release='3.0'}\n" + forge_mod("demo", "${file.release}"));
        check(mod["known"] && mod["version"] == "3.0", "TOML file properties resolved");
        auto defaulted = forge(std::string(forge_header) + "[[mods]]\nmodId='demo'\n");
        check(defaulted["version"] == "1", "documented default mod version is 1");
        auto unresolved = forge(std::string(forge_header) + forge_mod("demo", "${file.jarVersion}"));
        check(warned(unresolved, "unresolved_placeholder"), "missing manifest version never treated as verified");
        auto broken = forge(std::string(forge_header) + forge_mod("demo") + "modId='duplicate'\n");
        check(!broken["known"].get<bool>() && broken["valid_archive"].get<bool>(), "duplicate TOML keys rejected while archive retained");
    });
    run("NeoForge side, optional dependencies, repeated IDs and FML", [] {
        auto toml = std::string("modLoader='javafml'\nloaderVersion='[4,)'\nlicense='MIT'\n") + forge_mod("demo") +
            "[[dependencies.demo]]\nmodId='neoforge'\nversionRange='[21,22)'\ntype='required'\n"
            "[[dependencies.demo]]\nmodId='library'\nversionRange='[1,)'\ntype='required'\n"
            "[[dependencies.demo]]\nmodId='library'\nversionRange='(,2)'\ntype='required'\n"
            "[[dependencies.demo]]\nmodId='server_api'\nside='SERVER'\ntype='required'\n"
            "[[dependencies.demo]]\nmodId='optional_api'\ntype='optional'\nversionRange='[2,3)'\n"
            "[[dependencies.demo]]\nmodId='incompatible'\ntype='incompatible'\n"
            "[[dependencies.demo]]\nmodId='discouraged'\ntype='discouraged'\n";
        auto mod = forge(toml, "", "neoforge");
        check(mod["known"], "NeoForge recognized");
        check(mod["language_loader"]["range"] == "[4,)" && mod["language_loader"]["version_source"] == "fml", "FML range not compared to NeoForge version");
        check(mod["requirements"]["library"]["all"].size() == 2, "multiple same-id constraints retained");
        check(!mod["requirements"].contains("server_api") && mod["server_dependencies"].size() == 1, "server-only dependency excluded for client");
        check(mod["dependency_clauses"].size() == 6, "optional dependency is retained and server clause omitted");
        check(mod["dependency_clauses"][3]["expression"]["optional"] == true, "optional dependency validates when present");
        check(mod["breaks"].contains("incompatible") && mod["conflicts"].contains("discouraged"), "negative dependency severities retained");
        auto legacy_path = inspect_mod_bytes(zip({{"META-INF/mods.toml", toml}}), "old_neo.jar", "neoforge");
        check(legacy_path["loader"] == "neoforge", "early NeoForge mods.toml recognized structurally");
    });
    run("Forge Java features and unsupported requirements", [] {
        auto mod = forge(std::string(forge_header) + forge_mod("demo") +
            "[features.demo]\njavaVersion='[17,)'\nopenGLVersion='[4.6,)'\n");
        check(mod["requirements"]["java"] == "[17,)", "Java feature range becomes explicit requirement");
        check(warned(mod, "unsupported_feature"), "hardware feature not silently accepted");
    });
    run("Nested candidates keep parent ownership and identities", [] {
        auto doc = fabric("parent"); doc["jars"] = Json::array({{{"file", "META-INF/jars/child.jar"}}});
        auto child = fabric("child"); child["provides"] = Json::array({"child_alias"});
        auto raw_child = zip({{"fabric.mod.json", child.dump()}, {"padding", std::string(2100000, 'x')}});
        auto mod = inspect_mod_bytes(zip({{"fabric.mod.json", doc.dump()}, {"META-INF/jars/child.jar", raw_child}}), "parent.jar", "fabric");
        check(mod["known"], "bounded nested jars larger than metadata limit supported");
        check(mod["own_ids"] == Json::array({"parent"}), "nested aliases not confused with parent identities");
        check(mod["ids"].size() == 3 && mod["components"].size() == 1, "legacy aggregate and rich candidate retained");
        check(mod["components"][0]["load_type"] == "if_possible", "Fabric nested candidate has documented load condition");
        check(mod["embedded_hashes"]["child"] == hash_bytes(raw_child), "nested identity tied to actual nested bytes");
    });
    run("JarJar actual nested mods and plain libraries", [] {
        auto nested = zip({{"META-INF/mods.toml", std::string(forge_header) + forge_mod("nested")}});
        auto library = zip({{"META-INF/MANIFEST.MF", "Manifest-Version: 1.0\nAutomatic-Module-Name: library\n\n"}, {"library.class", "binary"}});
        auto jars = Json{{"jars", Json::array({jarjar("META-INF/jarjar/mod.jar", "nested"), jarjar("META-INF/jarjar/lib.jar")})}};
        auto mod = inspect_mod_bytes(zip({{"META-INF/mods.toml", std::string(forge_header) + forge_mod("parent")},
            {"META-INF/jarjar/metadata.json", jars.dump()}, {"META-INF/jarjar/mod.jar", nested}, {"META-INF/jarjar/lib.jar", library}}), "jarjar.jar", "forge");
        check(mod["known"] && mod["components"].size() == 2, "both mod and plain-library JarJar children inspected");
        check(mod["provided_versions"].contains("nested"), "embedded Forge mod contributes declared IDs");
        check(mod["components"][1]["library"] == true && mod["components"][1]["own_ids"].empty(), "library coordinates do not invent mod IDs");
        check(mod["jarjar_libraries"][1]["sha512"] == hash_bytes(library), "JarJar artifact linked to actual bytes");
        check(warned(mod, "jarjar_selection"), "unmodeled cross-JAR artifact negotiation explicit");
        auto missing = inspect_mod_bytes(zip({{"META-INF/mods.toml", std::string(forge_header) + forge_mod("parent")}, {"META-INF/jarjar/metadata.json", jars.dump()}}), "missing.jar", "forge");
        check(!missing["known"].get<bool>(), "missing JarJar file cannot pass metadata validation");
    });
    run("Foreign nested descriptors cannot satisfy dependencies", [] {
        auto parent = fabric("parent"); parent["jars"] = Json::array({{{"file", "foreign.jar"}}});
        auto child = zip({{"META-INF/mods.toml", std::string(forge_header) + forge_mod("foreign")}});
        auto mod = inspect_mod_bytes(zip({{"fabric.mod.json", parent.dump()}, {"foreign.jar", child}}), "parent.jar", "fabric");
        check(mod["known"] && warned(mod, "foreign_nested_metadata"), "foreign nested descriptor produces explicit warning");
        check(!mod["provided_versions"].contains("foreign"), "Forge nested declaration cannot supply Fabric mod ID");
        check(mod["components"].empty() && mod["ignored_components"].size() == 1, "foreign descriptors preserved outside active candidate set");
    });
    run("Nested limits, ambiguous archives and broken descriptors", [] {
        auto raw = zip({{"fabric.mod.json", fabric().dump()}});
        for (int n = 0; n != 5; ++n) {
            auto parent = fabric("parent" + std::to_string(n)); parent["jars"] = Json::array({{{"file", "child.jar"}}});
            raw = zip({{"fabric.mod.json", parent.dump()}, {"child.jar", raw}});
        }
        check(!inspect_mod_bytes(raw, "deep.jar", "fabric")["known"].get<bool>(), "depth remains bounded");
        auto too_many = fabric(); too_many["jars"] = Json::array();
        for (int n = 0; n != 129; ++n) too_many["jars"].push_back({{"file", std::to_string(n) + ".jar"}});
        check(!inspect_json(too_many)["known"].get<bool>(), "nested count remains bounded");
        auto duplicate = inspect_mod_bytes(zip({{"fabric.mod.json", fabric("first").dump()}, {"fabric.mod.json", fabric("second").dump()}}), "duplicate.jar");
        check(!duplicate["known"].get<bool>(), "duplicate metadata archive paths rejected");
        auto oversized = fabric(); oversized["description"] = std::string(2 * 1024 * 1024, 'x');
        check(!inspect_json(oversized)["known"].get<bool>(), "metadata entry size remains bounded");
        auto multi = inspect_mod_bytes(zip({{"fabric.mod.json", fabric().dump()}, {"quilt.mod.json", "{broken"}}), "multi.jar", "fabric");
        check(multi["known"] && warned(multi, "invalid_loader_metadata"), "other malformed loader descriptor remains visible");
    });
    std::cout << checks << " metadata checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
