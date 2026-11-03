// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include "solar/accounts.hpp"
#include <fstream>
#include <regex>
#include <set>
namespace solar::engine {
static std::string base64_bytes(const std::string &bytes) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; unsigned value = 0; int bits = -6;
    for (unsigned char c : bytes) {
        value = (value << 8) | c; bits += 8;
        while (bits >= 0) { out += alphabet[(value >> bits) & 63]; bits -= 6; }
    }
    if (bits > -6) out += alphabet[((value << 8) >> (bits + 8)) & 63];
    while (out.size() % 4) out += '=';
    return out;
}
static std::string image_bytes(const fs::path &archive, const std::string &entry) {
    if (entry.empty() || entry.size() > 1024 || entry.find(':') != entry.npos || entry.find('\\') != entry.npos) return {};
    try {
        checked_path(fs::temp_directory_path(), entry);
        auto bytes = fs::is_directory(archive) ? read_file(checked_path(archive, entry), 1024 * 1024)
                                               : jar_entry(archive, entry, 1024 * 1024);
        if (bytes.size() > 1024 * 1024) return {};
        bool png = bytes.size() >= 24 && bytes.substr(0,8) == std::string("\x89PNG\r\n\x1a\n",8);
        bool jpeg = bytes.size() >= 4 && (unsigned char)bytes[0] == 255 && (unsigned char)bytes[1] == 216;
        bool webp = bytes.size() >= 12 && bytes.substr(0,4)=="RIFF" && bytes.substr(8,4)=="WEBP";
        if (!png && !jpeg && !webp) return {};
        return base64_bytes(bytes);
    } catch (...) { return {}; }
}
Json instance_inventory(const Json &instance, const std::string &kind) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto mods = local_mods(game, instance.value("loader", ""));
    auto checks = check_mods(instance, mods, true, false);
    Json rows = kind == "mod" ? mods : local_packs(game, kind);
    for (auto &row : rows) {
        auto filename = row.value("filename", "");
        auto file = checked_path(game, (kind=="mod" ? "mods/" : kind=="shader" ? "shaderpacks/" : "resourcepacks/")+filename);
        row["icon_base64"] = image_bytes(file, kind=="mod" ? row.value("icon_path", "") : "pack.png");
        if (kind=="mod") {
            auto result = check_mods(instance, Json::array({row}), true, true);
            row["errors"] = result["errors"];
            row["warnings"] = result["warnings"];
        }
    }
    return {{"items", rows}, {"checks", checks}, {"support", shader_support(instance)}};
}
Json enrich_mod_inventory(const Json &instance, const Task &task) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto mods = local_mods(game, instance.value("loader", ""));
    Json hashes=Json::array();
    for (auto &m:mods) { if(hashes.size() >= 1024) break; hashes.push_back(m.at("sha512")); }
    if(hashes.empty()) return instance_inventory(instance,"mod");
    task.report("正在读取组件信息"); task.check();
    auto reply=https_request("https://api.modrinth.com/v2/version_files",
        Json{{"hashes",hashes},{"algorithm","sha512"}}.dump(),"","application/json");
    if(reply.status!=200) throw Error("mods","identity_lookup_failed","读取模组来源失败。",reply.status,"https://api.modrinth.com/v2/version_files");
    auto identities=Json::parse(reply.body); Json ids=Json::array();
    for(auto &entry:identities.items()) {
        auto project=entry.value().value("project_id","");
        if(std::regex_match(project,std::regex("[A-Za-z0-9]{1,64}")) && ids.size()<128) ids.push_back(project);
    }
    Json projects=Json::object();
    if(!ids.empty()) for(auto &project:fetch_json("https://api.modrinth.com/v2/projects?ids="+url_encode(ids.dump()),task))
        projects[project.at("id").get<std::string>()]=project;
    auto manifest=mod_inventory(game);
    for(auto &m:mods) if(identities.contains(m.at("sha512").get<std::string>())) {
        task.check(); auto file=checked_path(game,"mods/"+m.at("filename").get<std::string>());
        if(hash_file(file)!=m.at("sha512")) throw Error("mods","plan_stale","模组文件发生变化，请刷新后重试。");
        auto identity=identities.at(m.at("sha512").get<std::string>());
        auto key=m.at("filename").get<std::string>(); if(key.size()>9 && key.substr(key.size()-9)==".disabled") key.resize(key.size()-9);
        auto project=identity.value("project_id","");
        manifest[key]={{"sha512",m.at("sha512")},{"projectId",project},{"versionId",identity.value("id","")}};
        if(projects.contains(project)) {
            manifest[key]["icon_url"]=projects[project].value("icon_url",Json());
            manifest[key]["title"]=projects[project].value("title","");
        }
    }
    atomic_write(game/"mods/.solar-mods.json",manifest.dump(2));
    return instance_inventory(instance,"mod");
}
Json instance_logs(const Json &instance, const Task &task) {
    auto directory=fs::u8path(instance.at("game_directory").get<std::string>());
    Json files=Json::array();
    for(auto name:{"logs/latest.log",".solar-launch.log"}) {
        auto file=checked_path(directory,name); if(!fs::is_regular_file(file))continue;
        std::ifstream stream(file,std::ios::binary); stream.seekg(0,std::ios::end);
        auto length=stream.tellg(); auto start=std::max<std::streamoff>(0,std::streamoff(length)-512*1024);
        stream.seekg(start); std::string bytes((std::istreambuf_iterator<char>(stream)),{});
        if(start>0) {auto newline=bytes.find('\n'); if(newline!=bytes.npos)bytes.erase(0,newline+1);}
        GameLogDecoder decoder; auto text=decoder.append(bytes,true);
        files.push_back({{"name",name},{"text",clean_log(text,task.secrets)},{"truncated",start>0}});
    }
    return files;
}
Json shader_support_plan(const Json &instance,const Task &task) {
    auto loader=instance.value("loader","");
    if(loader!="fabric" && loader!="neoforge") throw Error("shaders","unsupported_support","此加载器不能使用 Iris 自动方案，请配置对应版本的光影组件。");
    auto files=resource_versions("iris",instance.value("version",""),loader,task);
    auto stable=std::find_if(files.begin(),files.end(),[](const Json &v){return v.value("release_type","")=="release";});
    if(stable==files.end()) throw Error("shaders","no_stable_support","没有找到适配此实例的正式版 Iris。");
    auto root=*stable; auto project=resource_project("iris",task);
    root["title"]=project.value("title","Iris"); root["icon_url"]=project.value("icon_url",Json());
    return plan_mod_install(instance,root,task);
}
} // namespace solar::engine

namespace solar::engine {
Json java_catalog(const Json &request,const Task &task) {
    auto index=fs::u8path(user_data_root())/"java-catalog.json";
    auto stored=read_json(index,Json::array()); if(!stored.is_array())stored=Json::array();
    auto remove=request.value("remove","");
    Json paths=Json::array();
    for(auto &p:stored) if(p.is_string()&&p!=remove) paths.push_back(p);
    for(auto &p:request.value("paths",Json::array())) if(p.is_string()&&p.get<std::string>().size()<4096&&paths.size()<1000 && std::find(paths.begin(),paths.end(),p)==paths.end())paths.push_back(p);
    if(request.contains("paths") || !remove.empty())atomic_write(index,paths.dump());
    auto result=detect_javas();
    for(auto &j:scan_java({fs::u8path(user_data_root())/"java-managed"},false,task,14,100000).at("runtimes"))result.push_back(j);
    for(auto &p:paths){task.check();auto runtime=probe_java(fs::u8path(p.get<std::string>()));if(!runtime.is_null())result.push_back(runtime);}
    Json unique=Json::array();std::set<std::string>seen;
    for(auto &runtime:result)if(seen.insert(runtime.at("path").get<std::string>()).second)unique.push_back(runtime);
    return unique;
}
Json java_scan_roots() {
    Json roots=Json::array();
#ifdef _WIN32
    for(char letter='C';letter<='Z';++letter){std::string drive(1,letter);drive+=":/";if(fs::is_directory(fs::u8path(drive)))roots.push_back(drive);}
#else
    roots.push_back("/");
#endif
    return roots;
}
} // namespace solar::engine
