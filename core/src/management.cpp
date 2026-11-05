// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include "solar/accounts.hpp"
#include "install_internal.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <mutex>
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
static std::string image_bytes(const fs::path &archive, const std::string &entry,
                               const std::string &hash = "", bool *verified = nullptr) {
    if (verified) *verified = false;
    if (entry.empty() || entry.size() > 1024 || entry.find(':') != entry.npos || entry.find('\\') != entry.npos) return {};
    try {
        checked_path(fs::temp_directory_path(), entry);
        std::string bytes;
        if (fs::is_directory(archive)) bytes = read_file(checked_path(archive, entry), 1024 * 1024);
        else {
            auto payload = read_file(archive, 512 * 1024 * 1024);
            if (!hash.empty() && hash_bytes(payload) != hash) return {};
            if (verified) *verified = true;
            bytes = zip_entry_bytes(payload,entry,1024*1024);
        }
        if (bytes.size() > 1024 * 1024) return {};
        bool png = bytes.size() >= 24 && bytes.substr(0,8) == std::string("\x89PNG\r\n\x1a\n",8);
        bool jpeg = bytes.size() >= 4 && (unsigned char)bytes[0] == 255 && (unsigned char)bytes[1] == 216;
        bool webp = bytes.size() >= 12 && bytes.substr(0,4)=="RIFF" && bytes.substr(8,4)=="WEBP";
        if (!png && !jpeg && !webp) return {};
        return base64_bytes(bytes);
    } catch (...) { return {}; }
}
static std::string inventory_icon(const fs::path &file, const std::string &entry, const std::string &hash) {
    // Hash-bound icons share the metadata scan's invalidation; never repeatedly
    // inflate the same archive just to repaint a list.
    static std::mutex mutex;
    static std::map<std::string,std::string> cache;
    static size_t bytes = 0;
    if (hash.empty()) return image_bytes(file,entry);
    if (entry.empty()) return {};
    auto key = hash + ":" + entry;
    { std::lock_guard<std::mutex> lock(mutex); auto it=cache.find(key); if(it!=cache.end())return it->second; }
    bool verified = false;
    auto image = image_bytes(file,entry,hash,&verified);
    if (!verified) return image;
    std::lock_guard<std::mutex> lock(mutex);
    if (cache.count(key)) return cache.at(key);
    while (!cache.empty() && (cache.size()>=256 || bytes+image.size()>16*1024*1024)) {
        bytes-=cache.begin()->second.size(); cache.erase(cache.begin());
    }
    cache[key]=image; bytes+=image.size(); return image;
}
Json instance_inventory(const Json &instance, const std::string &kind, bool fresh) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    auto mods = local_mods(game, instance.value("loader", ""), !fresh, true);
    auto checks = check_mods(instance, mods, true, false);
    Json rows = kind == "mod" ? mods : local_packs(game, kind);
    for (auto &row : rows) {
        auto filename = row.value("filename", "");
        auto file = checked_path(game, (kind=="mod" ? "mods/" : kind=="shader" ? "shaderpacks/" : "resourcepacks/")+filename);
        row["icon_base64"] = inventory_icon(file, kind=="mod" ? row.value("icon_path", "") : "pack.png", row.value("sha512", ""));
        if (kind == "mod") {
            row["errors"] = Json::array(); row["warnings"] = Json::array();
            Json issues = Json::array();
            for (auto &issue : checks.at("issues")) {
                auto files = issue.value("files", Json::array());
                if (std::find(files.begin(), files.end(), filename) == files.end()) continue;
                issues.push_back(issue);
                row[issue.value("severity", "warning") == "error" ? "errors" : "warnings"].push_back(issue.at("message"));
            }
            if (row.contains("source_problem")) {
                issues.push_back({{"severity","warning"},{"code","source_index_invalid"},
                    {"message",row["source_problem"]},{"files",Json::array({filename})}});
                row["warnings"].push_back(row["source_problem"]);
            }
            const bool enabled = row.value("enabled", true), known = row.value("known", false);
            row["detection"] = {
                {"local", known ? "recognized" : row.value("valid_archive", false) ? "unknown" : "invalid"},
                {"source", row.value("source_status", "not_checked")},
                {"compatibility", !enabled ? "disabled" : !row["errors"].empty() ? "conflict" :
                    !known || !row["warnings"].empty() ? "needs_review" : "no_known_conflict"},
                {"issues", issues}};
        }
    }
    return {{"items", rows}, {"checks", checks}, {"support", shader_support(instance, mods)}};
}
Json enrich_mod_inventory(const Json &instance, const Task &task) {
    auto game = fs::u8path(instance.at("game_directory").get<std::string>());
    try {
        auto mods = local_mods(game, instance.value("loader", ""));
        auto manifest = mod_inventory(game);
        const auto original_manifest = manifest;
        Json identities = Json::object(), ids = Json::array(); std::set<std::string> seen;
        for (size_t start = 0; start < mods.size(); start += 128) {
            task.report("正在读取组件信息", start, mods.size());
            Json hashes = Json::array();
            for (size_t n=start; n<std::min(start+128, mods.size()); ++n)
                if (!mods[n].value("sha512", "").empty()) hashes.push_back(mods[n].at("sha512"));
            if (hashes.empty()) continue;
            auto batch = post_json("https://api.modrinth.com/v2/version_files", {{"hashes",hashes},{"algorithm","sha512"}},task);
            if (!batch.is_object())
                throw Error("mods","invalid_identity","模组来源返回格式异常。");
            for (auto &hash : hashes) {
                auto key=hash.get<std::string>();
                if (!batch.contains(key)) continue;
                auto identity = batch.at(key);
                if (!identity.is_object() || !identity.value("files", Json::array()).is_array())
                    throw Error("mods","invalid_identity","模组来源返回格式异常。");
                identities[key] = identity;
                auto project = identity.value("project_id", "");
                if (std::regex_match(project,std::regex("[A-Za-z0-9]{1,64}")) && seen.insert(project).second) ids.push_back(project);
            }
        }
        Json projects = Json::object(), project_error;
        // Project titles/icons are optional: failed artwork must not discard a
        // successfully verified file identity.
        try {
            for (size_t start=0; start<ids.size(); start+=128) {
                Json chunk = Json::array();
                for (size_t n=start; n<std::min(start+128,ids.size()); ++n) chunk.push_back(ids[n]);
                auto response = fetch_json("https://api.modrinth.com/v2/projects?ids="+url_encode(chunk.dump()),task);
                if (!response.is_array()) throw Error("mods","invalid_identity","模组来源返回格式异常。");
                for (auto &project : response) {
                    if (!project.is_object() || !project.contains("id") || !project.at("id").is_string()) continue;
                    auto key = project.at("id").get<std::string>();
                    if (std::find(chunk.begin(),chunk.end(),key)==chunk.end()) continue;
                    if (project.contains("title") && !project.at("title").is_string()) continue;
                    if (project.contains("icon_url") && !project.at("icon_url").is_null() && !project.at("icon_url").is_string()) continue;
                    projects[key] = project;
                }
            }
        } catch (const Error &e) { task.check(); project_error = e.json(); }
        catch (const std::exception &) { task.check(); project_error = Error("mods","invalid_identity","模组来源返回格式异常。").json(); }
        for (auto &m : mods) {
            task.check(); auto hash = m.at("sha512").get<std::string>();
            auto file = checked_path(game,"mods/"+m.at("filename").get<std::string>());
            if (hash_file(file) != hash) throw Error("mods","plan_stale","模组文件发生变化，请刷新后重试。");
            auto key = m.at("filename").get<std::string>();
            if (key.size()>9 && key.substr(key.size()-9)==".disabled") key.resize(key.size()-9);
            if (!identities.contains(hash)) {
                // Keep a previously verified identity if a listing temporarily
                // omits it, while recording negative lookups for unknown files.
                if (m.value("source_status", "") != "matched")
                    manifest[key] = {{"sha512",hash},{"sourceStatus","unmatched"},{"sourceCheckedAt",now()}};
                continue;
            }
            const auto identity = identities.at(hash);
            bool exact = false;
            for (auto &f : identity.value("files", Json::array()))
                exact |= f.value("hashes",Json::object()).value("sha512","") == hash;
            auto project = identity.value("project_id", ""), version = identity.value("id", "");
            if (!exact || project.empty() || version.empty())
                throw Error("mods", "invalid_identity", "模组来源返回格式异常。");
            Json record{{"sha512",hash},{"projectId",project},{"versionId",version},
                {"sourceStatus","matched"},{"sourceCheckedAt",now()}};
            if (projects.contains(project)) {
                record["icon_url"] = projects[project].value("icon_url",Json());
                record["title"] = projects[project].value("title","");
            } else if (m.value("source_status", "") == "matched") {
                record["icon_url"] = m.value("icon_url",Json()); record["title"] = m.value("name","");
            }
            manifest[key] = record;
        }
        task.check();
        detail::Lock lock(game/"mods");
        auto current = local_mods(game,instance.value("loader",""));
        Json expected=Json::object(), actual=Json::object();
        for(auto &m:mods)expected[m.at("filename").get<std::string>()]=m.at("sha512");
        for(auto &m:current)actual[m.at("filename").get<std::string>()]=m.at("sha512");
        if (expected != actual || mod_inventory(game) != original_manifest)
            throw Error("mods","plan_stale","模组文件发生变化，请刷新后重试。");
        atomic_write(game/"mods/.solar-mods.json",manifest.dump(2));
        auto result = instance_inventory(instance,"mod");
        if (!project_error.is_null()) result["source_error"] = project_error;
        return result;
    } catch (const Error &e) {
        task.check();
        auto result = instance_inventory(instance,"mod");
        result["source_error"] = e.json();
        for (auto &row : result["items"]) if (row["detection"].value("source", "") != "matched")
            row["detection"]["source"] = "failed";
        return result;
    } catch (const std::exception &) {
        task.check();
        auto result = instance_inventory(instance,"mod");
        result["source_error"] = Error("mods","invalid_identity","模组来源返回格式异常。").json();
        for (auto &row : result["items"]) if (row["detection"].value("source", "") != "matched")
            row["detection"]["source"] = "failed";
        return result;
    }
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
