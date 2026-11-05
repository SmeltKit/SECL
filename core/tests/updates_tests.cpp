// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <zlib.h>
using namespace solar;
using namespace solar::engine;
static int checks=0;
static void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
static void rejects(const std::function<void()> &f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}check(rejected,"expected rejection");}
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
    auto temp=fs::temp_directory_path()/("secl-updates-tests-"+random_url_token());
    try {
        fs::create_directories(temp);
        Json i{{"game_directory",temp.u8string()},{"version","1.20.1"},{"loader","fabric"}};
        auto hash=hash_bytes("current");
        Json local{{"filename","old.jar"},{"sha512",hash},{"version","old"},{"name","Local name"},{"enabled",false}};
        auto old=version("v1","project",hash,"old.jar","2026-10-01T00:00:00Z");
        auto next=version("v2","project",hash_bytes("next"),"new.jar");
        auto evaluate=[&](Json current,Json candidate,std::string kind="mod",std::string channel="release"){
            return evaluate_resource_update(i,kind,channel,local,current,candidate);
        };
        check(evaluate(old,next)["status"]=="update","newer release detected");
        check(evaluate(old,next)["enabled"]==false,"disabled state retained");
        auto artifacts=next;
        auto source_file=artifacts["files"][0];source_file["filename"]="source.jar";source_file["primary"]=false;source_file["file_type"]="sources-jar";
        artifacts["files"].insert(artifacts["files"].begin(),source_file);
        check(evaluate(old,artifacts)["candidate"]["filename"]=="new.jar","primary jar preferred over sources");
        artifacts["files"][0].erase("file_type");artifacts["files"][1]["primary"]=false;
        check(evaluate(old,artifacts)["status"]=="unknown","ambiguous artifacts require manual choice");
        check(evaluate(old,old)["status"]=="current","same identity is not update");
        check(evaluate(next,old)["status"]=="unknown","hash association verified");
        auto newer=old;newer["date_published"]="2026-12-01T00:00:00Z";
        check(evaluate(newer,next)["status"]=="current","never downgrade by publication time");
        check(evaluate(Json(),next)["status"]=="unknown","unknown hash not up to date");
        auto beta=next;beta["version_type"]="beta";
        check(evaluate(old,beta)["status"]=="unknown","releases exclude beta");
        check(evaluate(old,beta,"mod","beta")["status"]=="update","beta explicit opt-in");
        auto alpha=next;alpha["version_type"]="alpha";
        check(evaluate(old,alpha,"mod","beta")["status"]=="unknown","beta excludes alpha");
        check(evaluate(old,alpha,"mod","alpha")["status"]=="update","alpha explicit opt-in");
        auto forge=old;forge["loaders"]={"forge"};
        check(evaluate(forge,next)["status"]=="incompatible","wrong installed loader not silently converted");
        auto wrong=next;wrong["project_id"]="other";
        check(evaluate(old,wrong)["status"]=="unknown","cross-project update rejected");
        wrong=next;wrong["game_versions"]={"1.21"};
        check(evaluate(old,wrong)["status"]=="unknown","wrong target game rejected");
        wrong=next;wrong["loaders"]={"forge"};
        check(evaluate(old,wrong)["status"]=="unknown","wrong target loader rejected");
        check(evaluate(old,Json())["status"]=="no_candidate","missing candidate not claimed latest");
        auto pack=next;pack["files"][0]["filename"]="next.zip";pack["loaders"]={"minecraft"};
        check(evaluate(old,pack,"resourcepack")["status"]=="update","packs do not use mod loader filter");
        auto quilt=i;quilt["loader"]="quilt";
        check(evaluate_resource_update(quilt,"mod","release",local,old,next)["status"]=="update","quilt fabric candidates allowed with caveat");
        rejects([&]{evaluate(old,next,"mod","bogus");});

        // Use real ZIP inventories and a deterministic transport to exercise
        // batch association, partial failure, and bounded network behavior.
        Json identities=Json::object(), updates=Json::object();
        for(int n=0;n<66;++n) {
            auto name="pack-"+std::to_string(n)+".zip";
            auto bytes=zip({{"pack.mcmeta",Json{{"pack",{{"pack_format",15},{"description",std::to_string(n)}}}}.dump()}});
            atomic_write(temp/"resourcepacks"/name,bytes);
            auto h=hash_bytes(bytes),project="p"+std::to_string(n);
            identities[h]=version("old"+std::to_string(n),project,h,name,"2026-10-01T00:00:00Z");
            updates[h]=version("new"+std::to_string(n),project,hash_bytes(project),"new-"+name);
        }
        int calls=0;
        Transport transport=[&](const std::string &url,const std::string &body,const std::string &token,const std::string &content){
            ++calls;check(token.empty(),"no account token sent to metadata service");
            auto request=Json::parse(body);
            if(calls==3)return HttpResponse{503,"outage"};
            if(url.find("/update")!=std::string::npos) {
                check(request.at("loaders").empty(),"pack loader filter is empty");
                check(request.at("version_types")==Json::array({"release"}),"release default filter");
                check(request.at("game_versions")==Json::array({"1.20.1"}),"game filter");
            }
            Json out=Json::object();
            for(auto &h:request.at("hashes"))out[h.get<std::string>()]=(url.find("/update")!=std::string::npos?updates:identities).at(h.get<std::string>());
            return HttpResponse{200,out.dump()};
        };
        auto report=check_resource_updates(i,"resourcepack","release",{},transport);
        check(calls==3,"no retries after outage");
        check(report["items"][0]["status"]=="update","successful chunk preserved");
        check(report["items"][65]["status"]=="failed","failed chunk explicitly marked");
        check(report["items"][65]["error"]["http_status"]==503,"HTTP code retained");
        auto first=report["items"][0].at("filename");
        auto plan=plan_resource_updates(report,Json::array({first}));
        check(plan["files"].size()==1,"selection scoped to chosen update");
        auto before=hash_file(temp/"resourcepacks"/first.get<std::string>());
        rejects([&]{apply_resource_updates(plan);}); // invalid download; original survives
        check(hash_file(temp/"resourcepacks"/first.get<std::string>())==before,"download failure preserves original");
        Task active;active.active_game_directory=temp.u8string();
        rejects([&]{plan_resource_updates(report,Json::array({first}),active);});
        rejects([&]{plan_resource_updates(report,Json::array({report["items"][65].at("filename")}));});
        rejects([&]{plan_resource_updates(report,Json::array());});
        atomic_write(temp/"resourcepacks/added.zip",zip({{"pack.mcmeta","{\"pack\":{\"pack_format\":15}}"}}));
        rejects([&]{plan_resource_updates(report,Json::array({first}));});
        rejects([&]{apply_resource_updates(plan);});
        check(hash_file(temp/"resourcepacks"/first.get<std::string>())==before,"stale plan leaves old file intact");

        // Selected roots form one graph, with shared dependencies installed once.
        auto empty=temp/"empty";fs::create_directories(empty);
        auto empty_i=i;empty_i["game_directory"]=empty.u8string();
        auto a=resource_files(Json::array({version("a2","a",hash_bytes("a"),"a.jar")}))[0];
        auto b=resource_files(Json::array({version("b2","b",hash_bytes("b"),"b.jar")}))[0];
        a["dependencies"]=Json::array({{{"project_id","b"},{"version_id",nullptr},{"dependency_type","required"}}});
        auto batch=plan_mod_installs(empty_i,Json::array({a,b}),{},"release");
        check(batch["files"].size()==2,"shared selected dependency not duplicated");
        check(batch["root_projects"].size()==2,"all selected roots retained");
        auto selected=select_dependencies(batch,Json::array());
        check(selected["files"].size()==2,"root updates cannot be accidentally omitted");
        b["release_type"]="beta";
        rejects([&]{plan_mod_installs(empty_i,Json::array({a,b}),{},"release");});
        b["release_type"]="release";
        a["dependencies"][0]["dependency_type"]="incompatible";
        rejects([&]{plan_mod_installs(empty_i,Json::array({a,b}),{},"release");});
        // Any added local jar after confirmation invalidates an install plan.
        atomic_write(empty/"mods/new.jar",zip({{"fabric.mod.json","{\"id\":\"local\",\"version\":\"1.0\",\"schemaVersion\":1}"}}));
        rejects([&]{apply_mod_plan(batch);});
        check(fs::exists(empty/"mods/new.jar"),"stale mod plan preserves added file");
        Json rollback{{"target",empty_i},{"files",Json::array()},{"conflicts",Json::array()},
            {"disable_files",{"new.jar","missing.jar"}}, {"expected_files",{{"new.jar",hash_file(empty/"mods/new.jar")}}},
            {"original_manifest",mod_inventory(empty)}};
        auto original_bytes=read_file(empty/"mods/new.jar");
        rejects([&]{apply_mod_plan(rollback);});
        check(read_file(empty/"mods/new.jar")==original_bytes && !fs::exists(empty/"mods/new.jar.disabled"),
              "late failure rolls back earlier file rename");
        fs::remove_all(temp);
        std::cout<<"PASS "<<checks<<" update checks\n"; return 0;
    } catch(const std::exception &e) {
        std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<"\n";fs::remove_all(temp);return 1;
    }
}
