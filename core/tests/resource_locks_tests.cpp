// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <zlib.h>
using namespace solar;
using namespace solar::engine;
static int checks=0;
static void check(bool b,const char *m){++checks;if(!b)throw std::runtime_error(m);}
static void rejects(const std::function<void()> &f, const std::string &code) {
    bool rejected=false;
    try { f(); } catch(const Error &e) {rejected=e.code==code;}
    check(rejected,("expected error: "+code).c_str());
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
static Json version(std::string id, std::string project, std::string hash,
                    std::string name, std::string date="2026-11-01T00:00:00Z") {
    return {{"id",id},{"project_id",project},{"version_number",id},{"name",id},
      {"date_published",date},{"version_type","release"},{"game_versions",{"1.20.1"}},
      {"loaders",{"fabric"}},{"dependencies",Json::array()},
      {"files",Json::array({{{"filename",name},{"url","https://example.invalid/fixture"},
                           {"primary",true},{"size",100},{"hashes",{{"sha512",hash}}}}})}};
}

int main() {
    auto temp=fs::temp_directory_path()/("secl-lock-tests-"+random_url_token());
    try {
        auto game=temp/"game"; fs::create_directories(game);
        Json i{{"game_directory",game.u8string()},{"version","1.20.1"},{"loader","fabric"}};
        auto bytes=zip({{"pack.mcmeta","{\"pack\":{\"pack_format\":15}}"}});
        atomic_write(game/"resourcepacks/old.zip",bytes);
        auto hash=hash_bytes(bytes);
        Json local{{"filename","old.zip"},{"sha512",hash},{"name","Demo"},{"version","1"}};
        auto old=version("v1","project",hash,"old.zip","2026-10-01T00:00:00Z");
        auto next=version("v2","project",hash_bytes("new"),"new.zip");
        int calls=0;
        Transport transport=[&](const std::string &url,const std::string &,const std::string &,const std::string &) {
            ++calls;
            return HttpResponse{200,Json{{hash,url.find("/update")!=url.npos?next:old}}.dump()};
        };
        auto report=check_resource_updates(i,"resourcepack","release",{},transport);
        auto plan=plan_resource_updates(report,{"old.zip"});
        set_resource_locks(i,"resourcepack",Json::array({local}),true);
        check(fs::exists(game/".solar-resource-locks.json"),"lock is persisted per instance");
        calls=0;
        auto locked=check_resource_updates(i,"resourcepack","release",{},transport);
        check(calls==0 && locked["items"][0]["status"]=="locked","locked item never queried or marked latest");
        rejects([&]{plan_resource_updates(report,{"old.zip"});},"version_locked");
        rejects([&]{apply_resource_updates(plan);},"version_locked");
        check(read_file(game/"resourcepacks/old.zip")==bytes,"locked apply does not touch original");
        assert_resource_replacement_unlocked(i,"resourcepack",local,{{"sha512",hash}});
        check(resource_locks(i,"shader").empty(),"kinds do not share locks");
        auto second=i;second["game_directory"]=(temp/"second").u8string();
        check(resource_locks(second,"resourcepack").empty(),"instances do not share locks");
        Json mods{{"files",Json::array({{{"filename","new.jar"},{"sha512",hash_bytes("new")},{"replaces",local}}})}};
        set_resource_locks(i,"resourcepack",Json::array({local}),false);
        check(!resource_locks(i,"resourcepack").contains(hash),"unlock persisted");
        // A normal market install also obeys the lock, not just the updater.
        auto replacement=resource_files(Json::array({next}),true).at(0);
        replacement["filename"]="old.zip";
        auto market=plan_pack_install(i,replacement,"resourcepack");
        set_resource_locks(i,"resourcepack",Json::array({local}),true);
        rejects([&]{plan_pack_install(i,replacement,"resourcepack");},"version_locked");
        rejects([&]{apply_pack_plan(market);},"version_locked");
        check(read_file(game/"resourcepacks/old.zip")==bytes,"market replacement preserves locked bytes");
        set_resource_locks(i,"resourcepack",Json::array({local}),false);
        const auto updated_bytes=zip({{"pack.mcmeta","{\"pack\":{\"pack_format\":15,\"description\":\"new\"}}"}});
        next["files"][0]["hashes"]["sha512"]=hash_bytes(updated_bytes);
        next["files"][0]["size"]=updated_bytes.size();
        report=check_resource_updates(i,"resourcepack","release",{},transport);
        plan=plan_resource_updates(report,{"old.zip"});
        Task race;
        race.transport=[&](const std::string &,const std::string &,const std::string &,const std::string &) {
            set_resource_locks(i,"resourcepack",Json::array({local}),true);
            return HttpResponse{200,updated_bytes};
        };
        rejects([&]{apply_resource_updates(plan,race);},"version_locked");
        check(read_file(game/"resourcepacks/old.zip")==bytes && !fs::exists(game/"resourcepacks/new.zip"),
              "lock added during download blocks commit and leaves originals intact");
        // An indirect dependency upgrade cannot silently replace a locked mod.
        auto dep_game=temp/"dependency";fs::create_directories(dep_game);
        auto dep_instance=i;dep_instance["game_directory"]=dep_game.u8string();
        auto b_bytes=zip({{"fabric.mod.json","{\"schemaVersion\":1,\"id\":\"library_b\",\"version\":\"1\"}"}});
        atomic_write(dep_game/"mods/b.jar",b_bytes);
        auto bh=hash_bytes(b_bytes);
        auto b1=version("b1","b",bh,"b.jar");
        auto b2=version("b2","b",hash_bytes("b2"),"b2.jar");
        auto a=version("a1","a",hash_bytes("a"),"a.jar");
        a["dependencies"]=Json::array({{{"project_id","b"},{"version_id","b2"},{"dependency_type","required"}}});
        Transport graph=[&](const std::string &url,const std::string &,const std::string &,const std::string &) {
            if(url.find("version_files")!=url.npos) return HttpResponse{200,Json{{bh,b1}}.dump()};
            if(url.find("version/b2")!=url.npos) return HttpResponse{200,b2.dump()};
            throw std::runtime_error("Unexpected dependency fixture request: "+url);
        };
        auto dep_file=resource_files(Json::array({a})).at(0);
        auto before_lock=plan_mod_install(dep_instance,dep_file,{},graph);
        check(before_lock["files"].size()==2,"dependency upgrade fixture resolves both files");
        set_resource_locks(dep_instance,"mod",Json::array({{{"filename","b.jar"},{"sha512",bh}}}),true);
        rejects([&]{plan_mod_install(dep_instance,dep_file,{},graph);},"version_locked");
        rejects([&]{apply_mod_plan(before_lock,{},graph);},"version_locked");
        check(read_file(dep_game/"mods/b.jar")==b_bytes,"dependency upgrade preserves pinned library");
        a["dependencies"][0]["dependency_type"]="optional";
        auto optional_file=resource_files(Json::array({a})).at(0);
        auto optional_plan=plan_mod_install(dep_instance,optional_file,{},graph);
        auto skipped=select_dependencies(optional_plan,Json::array());
        assert_mod_version_locks(dep_instance,skipped);
        auto opted_in=select_dependencies(optional_plan,{"b"});
        rejects([&]{assert_mod_version_locks(dep_instance,opted_in);},"version_locked");

        assert_resource_replacement_unlocked(dep_instance,"mod",{{"filename","b.jar"},{"sha512",bh}},
            {{"source","curseforge"},{"sha1",hash_bytes(b_bytes,"sha1")}});
        // Same bytes renamed by enable/disable keep their lock, external
        // replacements with different bytes never inherit another file's lock.
        atomic_write(game/"mods/demo.jar",bytes);
        auto mod=local;mod["filename"]="demo.jar";
        set_resource_locks(i,"mod",Json::array({mod}),true);
        fs::rename(game/"mods/demo.jar",game/"mods/demo.jar.disabled");
        mod["filename"]="demo.jar.disabled";
        Json rows=Json::array({mod});annotate_resource_locks(i,"mod",rows);
        check(rows[0]["update_locked"]==true,"disable rename retains lock");
        mods["files"][0]["replaces"]=mod;
        rejects([&]{assert_mod_version_locks(i,mods);},"version_locked");
        mods["files"][0]["sha512"]=hash;
        assert_mod_version_locks(i,mods);
        atomic_write(game/"mods/demo.jar.disabled","external replacement");
        Json replaced=Json::array({{{"filename","demo.jar.disabled"},{"sha512",hash_bytes("external replacement")}}});
        annotate_resource_locks(i,"mod",replaced);
        check(replaced[0]["update_locked"]==false,"different content does not inherit lock");
        rejects([&]{set_resource_locks(i,"mod",Json::array({mod}),true);},"plan_stale");
        auto bad=local;bad["filename"]="../outside.zip";
        rejects([&]{set_resource_locks(i,"resourcepack",Json::array({bad}),true);},"invalid_lock_target");
        const auto previous_locks=resource_locks(i,"resourcepack");
        rejects([&]{set_resource_locks(i,"resourcepack",Json::array({local,bad}),true);},"invalid_lock_target");
        check(resource_locks(i,"resourcepack")==previous_locks,"failed batch leaves locks unchanged");
        atomic_write(game/".solar-resource-locks.json","invalid");
        rejects([&]{check_resource_updates(i,"resourcepack","release",{},transport);},"invalid_locks");
        fs::remove_all(temp);std::cout<<"PASS "<<checks<<" resource lock checks\n";return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<"\n";fs::remove_all(temp);return 1;}
}
