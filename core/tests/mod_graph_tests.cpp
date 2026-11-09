// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <map>
#include <zlib.h>
using namespace solar;
using namespace solar::engine;
static int checks = 0;
static void check(bool value, const char *message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
static void rejects(const std::function<void()> &fn, const std::string &code) {
    try { fn(); }
    catch (const Error &error) { check(error.code == code, ("unexpected rejection: " + error.code).c_str()); return; }
    throw std::runtime_error("expected rejection: " + code);
}
static void word(std::string &out, unsigned value, int count) {
    while (count--) { out.push_back(char(value & 255)); value >>= 8; }
}
static std::string archive(const std::vector<std::pair<std::string, std::string>> &files) {
    std::string out, central;
    for (const auto &file : files) {
        const auto offset = out.size();
        const auto crc = crc32(0, reinterpret_cast<const Bytef *>(file.second.data()), unsigned(file.second.size()));
        word(out, 0x04034b50, 4); word(out, 20, 2); word(out, 0, 2); word(out, 0, 2); word(out, 0, 4);
        word(out, crc, 4); word(out, unsigned(file.second.size()), 4); word(out, unsigned(file.second.size()), 4);
        word(out, unsigned(file.first.size()), 2); word(out, 0, 2); out += file.first + file.second;
        word(central, 0x02014b50, 4); word(central, 20, 2); word(central, 20, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, crc, 4);
        word(central, unsigned(file.second.size()), 4); word(central, unsigned(file.second.size()), 4);
        word(central, unsigned(file.first.size()), 2); word(central, 0, 2); word(central, 0, 2);
        word(central, 0, 2); word(central, 0, 2); word(central, 0, 4); word(central, unsigned(offset), 4); central += file.first;
    }
    const auto offset = out.size(); out += central;
    word(out, 0x06054b50, 4); word(out, 0, 2); word(out, 0, 2);
    word(out, unsigned(files.size()), 2); word(out, unsigned(files.size()), 2);
    word(out, unsigned(central.size()), 4); word(out, unsigned(offset), 4); word(out, 0, 2);
    return out;
}
static std::string jar(const std::string &id) {
    return archive({{"fabric.mod.json", Json{{"schemaVersion", 1}, {"id", id}, {"version", "1.0"}}.dump()}});
}
static Json edge(const std::string &project, const std::string &type, const std::string &version = "") {
    return {{"project_id", project.empty() ? Json() : Json(project)},
            {"version_id", version.empty() ? Json() : Json(version)}, {"dependency_type", type}};
}
static Json version(const std::string &id, const std::string &project, Json dependencies = Json::array(),
                    const std::string &hash = "") {
    return {{"id", id}, {"project_id", project}, {"name", id}, {"version_number", id},
            {"version_type", "release"}, {"game_versions", {"1.20.1"}}, {"loaders", {"fabric"}},
            {"dependencies", dependencies},
            {"files", Json::array({{{"filename", id + ".jar"}, {"primary", true},
                {"url", "https://example.invalid/" + id + ".jar"},
                {"hashes", {{"sha512", hash.empty() ? hash_bytes(id) : hash}}}}})}};
}
struct Service {
    std::map<std::string, Json> versions;
    int calls = 0;
    Transport transport() {
        return [this](const std::string &url, const std::string &body, const std::string &token, const std::string &) {
            ++calls;
            check(token.empty(), "metadata request does not carry credentials");
            if (url.find("/version_files") != std::string::npos) {
                const auto request = Json::parse(body);
                Json identities = Json::object();
                for (const auto &hash : request.at("hashes"))
                    for (const auto &entry : versions)
                        for (const auto &file : entry.second.at("files"))
                            if (file.at("hashes").at("sha512") == hash) identities[hash.get<std::string>()] = entry.second;
                return HttpResponse{200, identities.dump()};
            }
            if (url.find("/versions?ids=") != std::string::npos) {
                Json all = Json::array(); for (const auto &entry : versions) all.push_back(entry.second);
                return HttpResponse{200, all.dump()};
            }
            for (const auto &entry : versions) {
                if (url == "https://api.modrinth.com/v2/version/" + entry.first)
                    return HttpResponse{200, entry.second.dump()};
                if (url.find("/project/" + entry.second.value("project_id", "") + "/version?") != std::string::npos)
                    return HttpResponse{200, Json::array({entry.second}).dump()};
            }
            return HttpResponse{404, "{}"};
        };
    }
    Json file(const std::string &id) const { return resource_files(Json::array({versions.at(id)}))[0]; }
};
static Json instance(const fs::path &folder) {
    return {{"game_directory", folder.u8string()}, {"version", "1.20.1"}, {"loader", "fabric"}};
}
static void install_fixture(const fs::path &folder, Service &service, const std::string &id,
                            const std::string &project, Json dependencies = Json::array(), bool enabled = true,
                            const std::string &mod_id = "") {
    const auto bytes = jar(mod_id.empty() ? project + id : mod_id);
    service.versions[id] = version(id, project, dependencies, hash_bytes(bytes));
    const auto filename = id + ".jar";
    atomic_write(folder / "mods" / (filename + (enabled ? "" : ".disabled")), bytes);
    auto manifest = mod_inventory(folder);
    manifest[filename] = {{"projectId", project}, {"versionId", id}, {"sha512", hash_bytes(bytes)}};
    atomic_write(folder / "mods/.solar-mods.json", manifest.dump());
}
int main() {
    const auto temp = fs::temp_directory_path() / ("secl-mod-graph-tests-" + random_url_token());
    try {
        fs::create_directories(temp);
        {
            Service service;
            service.versions["a1"] = version("a1", "a", Json::array({edge("b", "required"), edge("", "incompatible", "c1")}));
            service.versions["b1"] = version("b1", "b", Json::array({edge("c", "required")}));
            service.versions["c1"] = version("c1", "c");
            rejects([&] { plan_mod_install(instance(temp / "transitive"), service.file("a1"), {}, service.transport()); }, "version_conflict");
            check(service.calls == 2, "transitive version-only conflict checks newly chosen dependencies without retries");
        }
        {
            Service service;
            service.versions["a1"] = version("a1", "a", Json::array({edge("shared", "optional")}));
            service.versions["b1"] = version("b1", "b", Json::array({edge("shared", "required")}));
            service.versions["shared1"] = version("shared1", "shared", Json::array({edge("child", "required")}));
            service.versions["child1"] = version("child1", "child", Json::array({edge("leaf", "optional")}));
            service.versions["leaf1"] = version("leaf1", "leaf");
            auto candidate = [](const Json &graph, const std::string &project) {
                for (const auto &file : graph.at("files"))
                    if (file.value("project_id", "") == project) return file;
                for (const auto &reuse : graph.at("reuse"))
                    if (reuse.value("project_id", "") == project) return reuse.at("file");
                throw std::runtime_error("missing graph node " + project);
            };
            for (const auto &required_root : {std::string("b1"), std::string("shared1")}) {
                for (bool optional_first : {true, false}) {
                    const auto roots = optional_first ? Json::array({service.file("a1"), service.file(required_root)}) :
                        Json::array({service.file(required_root), service.file("a1")});
                    auto graph = plan_mod_installs(instance(temp / "promotion"), roots, {}, "", service.transport());
                    check(!candidate(graph, "shared").value("optional", true), "shared optional dependency is promoted by a later required/root path");
                    check(!candidate(graph, "child").value("optional", true), "required promotion propagates to required descendants in either root order");
                    check(candidate(graph, "leaf").value("optional", false), "promotion preserves genuinely optional descendant edges");
                    check(graph["optional"] == Json::array({"leaf"}), "required shared nodes are removed from optional choices");
                }
            }
            service.versions["child1"]["dependencies"].push_back(edge("a", "incompatible"));
            for (bool optional_first : {true, false}) {
                const auto roots = optional_first ? Json::array({service.file("a1"), service.file("b1")}) :
                    Json::array({service.file("b1"), service.file("a1")});
                rejects([&] { plan_mod_installs(instance(temp / "promotion-conflict"), roots, {}, "", service.transport()); }, "version_conflict");
            }
            service.versions["child1"]["dependencies"] = Json::array({edge("leaf", "optional")});
            const auto folder = temp / "promotion-reuse";
            install_fixture(folder, service, "shared1", "shared", Json::array({edge("child", "required")}));
            auto graph = plan_mod_installs(instance(folder), Json::array({service.file("a1"), service.file("b1")}), {}, "", service.transport());
            check(!candidate(graph, "shared").value("optional", true) &&
                  !candidate(graph, "child").value("optional", true),
                  "promotion also updates reused metadata and its required descendants");
        }
        {
            Service service;
            service.versions["a1"] = version("a1", "a", Json::array({edge("b", "optional")}));
            service.versions["b1"] = version("b1", "b", Json::array({edge("c", "required")}));
            service.versions["c1"] = version("c1", "c", Json::array({edge("a", "incompatible")}));
            auto plan = plan_mod_install(instance(temp / "optional"), service.file("a1"), {}, service.transport());
            check(plan["graph_conflicts"].size() == 1, "optional conflicting branch remains reviewable");
            check(plan["graph_conflicts"][0].value("optional", false), "optional transitive conflict is marked optional");
            auto off = select_dependencies(plan, Json::array());
            check(off["files"].size() == 1 && off["graph_conflicts"].empty(), "deselecting optional branch removes its transitive conflicts");
            auto on = select_dependencies(plan, Json::array({"b", "c"}));
            check(on["graph_conflicts"].size() == 1, "selected transitive reverse conflict is retained");
            on["conflicts"] = Json::array(); on["graph_conflicts"] = Json::array(); on["marketplace_graph"] = Json::array();
            for (auto &file : on["files"]) {
                file["dependencies"] = Json::array();
                file["embedded"] = true; // A top-level download cannot be relabeled as uncertain.
            }
            rejects([&] { apply_mod_plan(on, {}, service.transport()); }, "unresolved_conflict");
            check(!fs::exists(temp / "optional/mods/a1.jar"), "tampered plan is rejected before files are installed");
        }
        {
            Service service;
            const auto folder = temp / "reverse";
            install_fixture(folder, service, "a1", "a", Json::array({edge("", "incompatible", "b1")}));
            service.versions["b1"] = version("b1", "b");
            auto plan = plan_mod_install(instance(folder), service.file("b1"), {}, service.transport());
            check(plan["graph_conflicts"].size() == 1, "installed outgoing website-only version conflict is checked");
            check(plan["conflicts"][0].value("filename", "") == "a1.jar", "reverse edge offers explicit existing-file disable consent");
            plan["disable_files"] = {"a1.jar"};
            auto consent = select_dependencies(plan, Json::array());
            check(consent["graph_conflicts"].empty(), "explicit disable consent removes existing graph node");
            service.versions["a2"] = version("a2", "a");
            auto replacement = plan_mod_installs(instance(folder), Json::array({service.file("a2"), service.file("b1")}), {}, "", service.transport());
            check(replacement["graph_conflicts"].empty(), "replacing old version removes its outgoing incompatibility declaration");
            auto tampered = plan;
            tampered["disable_files"] = Json::array(); tampered["marketplace_existing"] = Json::array();
            tampered["conflicts"] = Json::array(); tampered["graph_conflicts"] = Json::array();
            rejects([&] { apply_mod_plan(tampered, {}, service.transport()); }, "unresolved_conflict");
            check(fs::exists(folder / "mods/a1.jar"), "fresh reverse-edge validation preserves installed file");
            atomic_write(folder / "mods/changed.jar", jar("changed"));
            const auto before = service.calls;
            rejects([&] { apply_mod_plan(consent, {}, service.transport()); }, "plan_stale");
            check(service.calls == before, "stale inventory is rejected before metadata or download requests");
        }
        {
            Service service;
            const auto folder = temp / "replacement-target";
            install_fixture(folder, service, "b1", "b");
            service.versions["a1"] = version("a1", "a", Json::array({edge("b", "incompatible", "b1")}));
            service.versions["b2"] = version("b2", "b");
            auto plan = plan_mod_installs(instance(folder), Json::array({service.file("a1"), service.file("b2")}), {}, "", service.transport());
            check(plan["graph_conflicts"].empty(), "version-specific declaration does not match replaced target version");
        }
        {
            Service service;
            const auto folder = temp / "disabled";
            install_fixture(folder, service, "a1", "a", Json::array({edge("b", "incompatible")}), false);
            service.versions["b1"] = version("b1", "b");
            auto plan = plan_mod_install(instance(folder), service.file("b1"), {}, service.transport());
            check(plan["graph_conflicts"].empty(), "disabled existing outgoing declaration is inactive");
            auto enable = plan_mod_install(instance(folder), service.file("a1"), {}, service.transport());
            // b is installed separately, so enabling reused a must consider b as well.
            install_fixture(folder, service, "b1", "b");
            enable = plan_mod_install(instance(folder), service.file("a1"), {}, service.transport());
            enable = select_dependencies(enable, Json::array(), Json::array(), Json::array({"a"}));
            check(enable["graph_conflicts"].size() == 1, "explicitly enabling a reused disabled file activates its outgoing declaration");
            enable["conflicts"] = Json::array(); enable["graph_conflicts"] = Json::array();
            rejects([&] { apply_mod_plan(enable, {}, service.transport()); }, "unresolved_conflict");
            service.versions["a2"] = version("a2", "a", Json::array({edge("b", "incompatible")}));
            auto update = plan_mod_install(instance(folder), service.file("a2"), {}, service.transport());
            check(update["files"][0].value("install_disabled", false) && update["graph_conflicts"].empty(),
                  "updating a disabled mod preserves disabled state and excludes its declaration");
            auto redownload = plan_mod_install(instance(folder), service.file("a1"), {}, service.transport());
            redownload = select_dependencies(redownload, Json::array(), Json::array({"a"}));
            check(redownload["files"][0].value("install_disabled", false) && redownload["graph_conflicts"].empty(),
                  "redownloading a disabled reused mod preserves disabled state");
        }
        {
            Service service;
            service.versions["a1"] = version("a1", "a", Json::array({edge("iris", "optional")}));
            service.versions["iris1"] = version("iris1", "iris");
            auto managed = instance(temp / "optional-managed");
            managed["optifine"] = {{"build", "fixture"}};
            auto plan = plan_mod_install(managed, service.file("a1"), {}, service.transport());
            check(plan["graph_conflicts"].size() == 1, "optional Iris conflict with managed OptiFine remains reviewable");
            auto off = select_dependencies(plan, Json::array());
            check(off["graph_conflicts"].empty() && off["conflicts"].empty(), "deselecting optional Iris clears managed component conflict");
            rejects([&] { plan_mod_install(managed, service.file("iris1"), {}, service.transport()); }, "managed_optifine");
            const auto folder = temp / "optional-rendering";
            install_fixture(folder, service, "of1", "optifabric", Json::array(), true, "optifabric");
            plan = plan_mod_install(instance(folder), service.file("a1"), {}, service.transport());
            check(plan["conflicts"].size() == 1 && plan["conflicts"][0].value("filename", "") == "of1.jar",
                  "optional Iris offers explicit disable card for external OptiFabric");
            off = select_dependencies(plan, Json::array());
            check(off["conflicts"].empty() && off["graph_conflicts"].empty(), "deselecting optional renderer removes its stale disable card");
            plan["disable_files"] = {"of1.jar"};
            auto consent = select_dependencies(plan, Json::array({"iris"}));
            check(consent["graph_conflicts"].empty(), "renderer conflict honors explicit disable consent");
        }
        {
            Service service;
            const auto folder = temp / "missing";
            install_fixture(folder, service, "a1", "a");
            service.versions["b1"] = version("b1", "b");
            int calls = 0;
            Transport incomplete = [&](const std::string &, const std::string &, const std::string &, const std::string &) {
                ++calls; return HttpResponse{200, "[]"};
            };
            rejects([&] { plan_mod_install(instance(folder), service.file("b1"), {}, incomplete); }, "missing_version");
            check(calls == 1, "incomplete installed metadata explicitly fails without retrying");
            service.versions["a1"]["files"][0]["hashes"]["sha512"] = hash_bytes("different file");
            rejects([&] { plan_mod_install(instance(folder), service.file("b1"), {}, service.transport()); }, "version_mismatch");
            check(service.calls == 1, "recorded website version is hash-verified against actual installed file");
        }
        {
            Service service;
            const auto folder = temp / "possible-nested";
            auto fabric = [](const std::string &id, const std::string &v, const std::string &name) {
                return Json{{"schemaVersion",1},{"id",id},{"version",v},{"name",name}};
            };
            const auto lib1 = archive({{"fabric.mod.json",fabric("lib","1.0","Local library one").dump()}});
            const auto lib2 = archive({{"fabric.mod.json",fabric("lib","2.0","Local library two").dump()}});
            const auto lib3 = archive({{"fabric.mod.json",fabric("lib","3.0","Local library three").dump()}});
            auto bridge_doc = fabric("bridge","1.0","Bridge");
            bridge_doc["jars"] = Json::array({{{"file","lib1.jar"}}});
            const auto bridge = archive({{"fabric.mod.json",bridge_doc.dump()},{"lib1.jar",lib1}});
            auto first_doc = fabric("first","1.0","First parent");
            first_doc["jars"] = Json::array({{{"file","bridge.jar"}},{{"file","lib3.jar"}}});
            const auto first = archive({{"fabric.mod.json",first_doc.dump()},{"bridge.jar",bridge},{"lib3.jar",lib3}});
            auto second_doc = fabric("second","1.0","Second parent");
            second_doc["jars"] = Json::array({{{"file","lib2.jar"}}});
            const auto second = archive({{"fabric.mod.json",second_doc.dump()},{"lib2.jar",lib2}});
            auto consumer_doc = fabric("consumer","1.0","Consumer");
            consumer_doc["depends"] = {{"lib","=2.0"}};
            const auto consumer = archive({{"fabric.mod.json",consumer_doc.dump()}});
            Json manifest = Json::object();
            auto add = [&](const std::string &id, const std::string &project, const std::string &bytes) {
                service.versions[id] = version(id,project,Json::array(),hash_bytes(bytes));
                atomic_write(folder/"mods"/(id+".jar"),bytes);
                manifest[id+".jar"] = {{"projectId",project},{"versionId",id},{"sha512",hash_bytes(bytes)}};
            };
            add("first1","first",first); add("second1","second",second); add("consumer1","consumer",consumer);
            atomic_write(folder/"mods/.solar-mods.json",manifest.dump());
            service.versions["lib-web-1"] = version("lib-web-1","library",Json::array({edge("consumer","incompatible")}),hash_bytes(lib1));
            service.versions["lib-web-1"]["version_number"] = "website-1.0";
            service.versions["lib-web-2"] = version("lib-web-2","library",Json::array(),hash_bytes(lib2));
            service.versions["lib-web-3"] = version("lib-web-3","library",Json::array(),hash_bytes(lib3));
            auto i = instance(folder);
            auto local = check_mods(i,local_mods(folder,"fabric"));
            check(local["errors"].empty() && local["selection_status"]=="satisfiable", "local CNF accepts one compatible embedded assignment");
            bool selected_two=false, selected_one=false;
            for (const auto &node : local["selection"]) if(node.value("id","")=="lib") {
                selected_two |= node.value("version","")=="2.0";
                selected_one |= node.value("version","")=="1.0";
            }
            check(selected_two && !selected_one, "local dependency forces nested v2 rather than unused web-conflicting v1");
            auto graph = plan_mod_install(i,service.file("consumer1"),{},service.transport());
            size_t nested_libraries=0;
            for (const auto &node:graph["marketplace_existing"]) if(node.value("project_id","")=="library") {
                ++nested_libraries; check(node.value("embedded",false) && node.value("possible",false), "verified nested identity is explicitly possible, not assumed loaded");
            }
            check(nested_libraries==3, "recursive component hashes retain same-ID candidates lost by flattened aggregates");
            check(graph["graph_conflicts"].empty() && graph["conflicts"].empty() && graph["graph_warnings"].size()==1,
                  "unused embedded source declaration is a nonblocking uncertainty without forced parent disable");
            check(graph["graph_warnings"][0]["message"].get<std::string>().find("website-1.0")!=std::string::npos,
                  "nested warning uses website version_number rather than the parent version");
            graph["graph_warnings"] = Json::array(); // Fresh apply must restore even a removed review warning.
            auto applied = apply_mod_plan(graph,{},service.transport());
            check(applied["installed"].empty() && applied["graph_warnings"].size()==1 && !applied["warnings"].empty(),
                  "apply preserves nested uncertainty warnings without rejecting satisfiable files");
            check(hash_file(folder/"mods/first1.jar")==hash_bytes(first), "nested uncertainty never disables or replaces its parent");
            service.versions["lib-web-1"]["dependencies"] = Json::array();
            service.versions["consumer1"]["dependencies"] = Json::array({edge("","incompatible","lib-web-1")});
            graph = plan_mod_install(i,service.file("consumer1"),{},service.transport());
            check(graph["graph_conflicts"].empty() && graph["graph_warnings"].size()==1 &&
                  graph["graph_warnings"][0].value("to_version","")=="lib-web-1",
                  "incoming version-only exclusion against a possible nested target is also uncertain");
            check(apply_mod_plan(graph,{},service.transport())["graph_warnings"].size()==1,
                  "reverse nested warning propagates through fresh apply");
            service.versions["consumer1"]["dependencies"] = Json::array({edge("first","incompatible")});
            graph = plan_mod_install(i,service.file("consumer1"),{},service.transport());
            check(graph["graph_warnings"].empty() && !graph["graph_conflicts"].empty(), "direct-file graph enforcement remains hard");
            rejects([&]{apply_mod_plan(graph,{},service.transport());},"unresolved_conflict");
        }
        {
            Service service;
            const auto folder = temp / "metadata-changed";
            install_fixture(folder, service, "a1", "a");
            service.versions["b1"] = version("b1", "b");
            auto plan = plan_mod_install(instance(folder), service.file("b1"), {}, service.transport());
            check(plan["graph_conflicts"].empty(), "reviewed graph initially has no declaration conflict");
            service.versions["a1"]["dependencies"] = Json::array({edge("b", "incompatible")});
            rejects([&] { apply_mod_plan(plan, {}, service.transport()); }, "unresolved_conflict");
            check(!fs::exists(folder / "mods/b1.jar"), "fresh changed website declarations are checked before download or mutation");
        }
        {
            const auto folder = temp / "curseforge";
            const auto base = jar("cfbase"), root = jar("cfroot");
            const auto pack = archive({{"pack.mcmeta", R"({"pack":{"pack_format":15,"description":"test"}})"}});
            constexpr std::uint32_t base_fingerprint = 1036310006;
            auto raw = [](int id, int project, const std::string &name, const std::string &bytes) -> Json {
                return {{"id", id}, {"modId", project}, {"gameId", 432}, {"fileName", name},
                        {"isAvailable", true}, {"fileStatus", 4}, {"releaseType", 1},
                        {"gameVersions", {"1.20.1", "Fabric"}}, {"fileLength", bytes.size()},
                        {"downloadUrl", "https://edge.forgecdn.net/files/0/" + std::to_string(id) + "/" + name},
                        {"hashes", {{{"algo", 1}, {"value", hash_bytes(bytes, "sha1")}}}}};
            };
            auto root_file = raw(101, 11, "root.jar", root);
            auto base_file = raw(102, 12, "base.jar", base);
            auto pack_file = raw(103, 13, "pack.zip", pack);
            root_file["dependencies"] = {{{"modId", 12}, {"relationType", 3}}};
            base_file["fileFingerprint"] = base_fingerprint;
            atomic_write(folder / "mods/base.jar", base);
            bool allowed = true;
            int downloads = 0;
            Task task;
            task.transport = [&](const std::string &url, const std::string &body, const std::string &token,
                                 const std::string &) -> HttpResponse {
                check(token.empty(), "mock transport does not get the CurseForge key");
                const std::string api = "https://api.curseforge.com/v1/";
                if (url == api + "fingerprints/432") {
                    check(Json::parse(body).at("fingerprints") == Json::array({base_fingerprint}),
                          "only the matching CurseForge fingerprint is sent for local lookup");
                    return {200, Json{{"data", {{"exactMatches", {{{"file", base_file}}}}}}}.dump()};
                }
                if (url == api + "mods/12/files/102") return {200, Json{{"data", base_file}}.dump()};
                if (url == api + "mods/101" || url == api + "mods/102")
                    throw std::runtime_error("a CurseForge file ID was used as a project ID");
                if (url.rfind(api + "mods/12/files?", 0) == 0)
                    return {200, Json{{"data", Json::array({base_file})}, {"pagination", {{"totalCount", 1}}}}.dump()};
                if (url == api + "mods/11/files/101") return {200, Json{{"data", root_file}}.dump()};
                if (url == api + "mods/13/files/103") return {200, Json{{"data", pack_file}}.dump()};
                for (const auto id : {11, 12, 13}) {
                    if (url != api + "mods/" + std::to_string(id)) continue;
                    return {200, Json{{"data", {{"id", id}, {"gameId", 432}, {"classId", id == 13 ? 12 : 6},
                                                {"allowModDistribution", id != 11 || allowed}}}}.dump()};
                }
                if (url == root_file.at("downloadUrl")) { ++downloads; return {200, root}; }
                if (url == pack_file.at("downloadUrl")) { ++downloads; return {200, pack}; }
                throw std::runtime_error("Unexpected CurseForge request: " + url);
            };
            const auto target = instance(folder);
            const auto selected = curseforge_file("11", "101", task);
            const auto plan = plan_mod_install(target, selected, task);
            check(plan.at("source") == "curseforge" && plan.at("files").size() == 1 &&
                  plan.at("reuse").size() == 1 && plan.at("reuse")[0].at("project_id") == "12",
                  "the checked local CurseForge dependency is reused");
            allowed = false;
            rejects([&] { apply_mod_plan(plan, task); }, "curseforge_unavailable");
            check(downloads == 0 && !fs::exists(folder / "mods/root.jar"),
                  "permission removed after preview blocks download and mutation");
            allowed = true;
            auto altered = plan;
            altered["files"][0]["url"] = "https://edge.forgecdn.net/files/0/999/altered.jar";
            apply_mod_plan(altered, task);
            check(downloads == 1 && read_file(folder / "mods/root.jar") == root && mod_inventory(folder).empty(),
                  "install refreshes the official URL and stores no CurseForge marketplace metadata");
            const auto resource = curseforge_file("13", "103", task);
            const auto pack_plan = plan_pack_install(target, resource, "resourcepack");
            apply_pack_plan(pack_plan, task);
            check(downloads == 2 && read_file(folder / "resourcepacks/pack.zip") == pack &&
                  !fs::exists(folder / ".solar-pack-metadata") &&
                  plan_pack_install(target, resource, "resourcepack").at("reuse") == true,
                  "CurseForge resource pack verifies SHA-1 and supports same-file reuse");
        }
        fs::remove_all(temp);
        std::cout << "PASS " << checks << " mod graph checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        fs::remove_all(temp);
        return 1;
    }
}
