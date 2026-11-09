// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "install_internal.hpp"
#include "modpack_internal.hpp"
#include <fstream>
#include <optional>
#include <cerrno>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#else
#include <stdio.h>
#endif
#endif

namespace solar::engine {
namespace {
void require_pack(bool okay,const std::string &code,const std::string &message) {
    if(!okay)throw Error("modpack",code,message);
}
bool present(const fs::path &path) {
    std::error_code ec;auto status=fs::symlink_status(path,ec);
    return !ec && status.type()!=fs::file_type::not_found;
}
// Cache names use 128 bits of the digest to leave room for nested game paths on
// Windows. Receipts and payload verification still use the complete SHA-512.
std::string cache_key(const std::string &hash) { return hash.substr(0,32); }

fs::path legacy_cache_file(fs::path root,const std::string &relative) {
#ifdef _WIN32
    const auto native=root.make_preferred().native();
    if(native.rfind(L"\\\\?\\",0)!=0)
        root=native.rfind(L"\\\\",0)==0 ? L"\\\\?\\UNC\\"+native.substr(2) : L"\\\\?\\"+native;
#endif
    // Only look up locally constructed legacy names, without following links
    // into locations outside the cache. Legacy data is never written here.
    for(const auto &part:fs::u8path(relative)) {
        root/=part;
#ifdef _WIN32
        const auto attributes=GetFileAttributesW(root.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))return {};
#else
        std::error_code error;
        const auto status=fs::symlink_status(root,error);
        if(error || !fs::exists(status) || fs::is_symlink(status))return {};
#endif
    }
    return root;
}
std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;
}
void instance_id(const std::string &id) {
    auto stem=lower(id.substr(0,id.find('.')));
    require_pack(std::regex_match(id,std::regex("[A-Za-z0-9][A-Za-z0-9_.-]{0,95}")) && id.back()!='.' &&
        stem!="con" && stem!="prn" && stem!="aux" && stem!="nul" &&
        !std::regex_match(stem,std::regex("(com|lpt)[0-9]")),"invalid_id","整合包实例 ID 无效。");
}
// OS-held locks disappear on process exit, so a crash does not leave an
// unbreakable directory lock in front of resumable work.
class ImportLock {
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    int file=-1;
#endif
public:
    explicit ImportLock(const fs::path &path) {
        fs::create_directories(path.parent_path());
        require_pack(!fs::is_symlink(path),"unsafe_cache","整合包缓存包含无效文件。");
#ifdef _WIN32
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        require_pack(file!=INVALID_HANDLE_VALUE,"instance_locked","此整合包或目标实例已有安装操作进行中。");
#else
        file=::open(path.c_str(),O_CREAT|O_RDWR|O_NOFOLLOW,0600);
        if(file<0 || flock(file,LOCK_EX|LOCK_NB)!=0) {
            if(file>=0)::close(file);file=-1;
            throw Error("modpack","instance_locked","此整合包或目标实例已有安装操作进行中。");
        }
#endif
    }
    ~ImportLock(){
#ifdef _WIN32
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
#else
        if(file>=0)::close(file);
#endif
    }
};
bool rename_exclusive(const fs::path &from,const fs::path &to) {
#ifdef _WIN32
    return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
#elif defined(__APPLE__)
    return renamex_np(from.c_str(),to.c_str(),RENAME_EXCL)==0;
#else
    return syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),1)==0;
#endif
}
Json orphan_owner(const fs::path &root,const std::string &id,const std::string &hash) {
    try {
        if(present(checked_path(root,"versions/"+id)))return Json();
        auto game=checked_path(root,"instances/"+id);
        if(!fs::is_directory(game) || fs::is_symlink(game))return Json();
        auto receipt=read_json(checked_path(game,".solar-modpack.json"));
        if(receipt.value("archive_sha512","")!=hash || receipt.value("id","")!=id)return Json();
        const auto suffix="/attempts/"+id+"/owner.json";
        for(const auto &path:{checked_path(root,".solar-mrpack/"+cache_key(hash)+suffix),
                             legacy_cache_file(root,".solar-mrpack/"+hash+suffix)}) {
            if(path.empty() || !present(path))continue;
            auto owner=read_json(path);
            auto token=owner.value("token","");
            if(owner.value("archive_sha512","")==hash && owner.value("id","")==id &&
               std::regex_match(token,std::regex("[A-Za-z0-9_-]{16,128}")) && receipt.value("transaction","")==token)return owner;
        }
    } catch(const std::exception&) {}
    return Json();
}
void vacant(const fs::path &root,const std::string &id,const std::string &hash="") {
    require_pack(!present(checked_path(root,"versions/"+id)) &&
        (!present(checked_path(root,"instances/"+id)) || (!hash.empty() && !orphan_owner(root,id,hash).is_null())),
        "instance_exists","目标实例或游戏目录已存在，请使用其他名称。");
}
void verify_payload(const fs::path &path,const Json &file,const Task &t) {
    require_pack(fs::is_regular_file(path) && !fs::is_symlink(path) && fs::file_size(path)==file.at("size").get<std::uint64_t>() &&
        (!file.contains("sha512") || hash_file(path,"sha512",t.cancel)==file.at("sha512")) &&
        hash_file(path,"sha1",t.cancel)==file.at("sha1"),
        "verification_failed","整合包文件大小或哈希校验失败。");
}
bool valid_payload(const fs::path &path,const Json &file,const Task &t) {
    if(path.empty())return false;
    try { verify_payload(path,file,t);return true; }
    catch(const Error&) { t.check();return false; }
}
// Cache publication is atomic and never replaces a differing shared game file.
// That prevents an imported pack from changing another instance's libraries.
void copy_verified(const fs::path &from,const fs::path &to,const Task &t,const std::string &expected_hash={}) {
    t.check();require_pack(fs::is_regular_file(from) && !fs::is_symlink(from),"unsafe_cache","整合包缓存包含无效文件。");
    const auto hash=hash_file(from,"sha512",t.cancel);const auto size=fs::file_size(from);
    require_pack(expected_hash.empty() || hash==expected_hash,"verification_failed","整合包文件哈希校验失败。");
    if(present(to)) {
        require_pack(fs::is_regular_file(to) && !fs::is_symlink(to) && fs::file_size(to)==size && hash_file(to,"sha512",t.cancel)==hash,
            "shared_file_conflict","共享游戏文件与整合包要求不同，未覆盖已有文件。");return;
    }
    fs::create_directories(to.parent_path());
    auto temp=to.parent_path()/(".tmp-"+random_url_token().substr(0,24));
    try {
        std::ifstream input(from,std::ios::binary);std::ofstream output(temp,std::ios::binary|std::ios::trunc);
        char bytes[65536];
        while(input) {t.check();input.read(bytes,sizeof(bytes));output.write(bytes,input.gcount());}
        output.close();
        require_pack(input.eof() && bool(output) && hash_file(temp,"sha512",t.cancel)==hash,"cache_copy_failed","无法复制已校验的整合包文件。");
        fs::permissions(temp,fs::status(from).permissions());
        t.check();
        // No-replace rename works on external filesystems without hard links.
        if(!rename_exclusive(temp,to)) {
            require_pack(present(to) && fs::is_regular_file(to) && !fs::is_symlink(to) && hash_file(to,"sha512",t.cancel)==hash,
                "cache_commit_failed","无法提交整合包文件，目标可能已被占用。");
        }
        std::error_code cleanup;fs::remove(temp,cleanup);
    } catch(...) {std::error_code ec;fs::remove(temp,ec);throw;}
}
void promote_shared(const fs::path &from,const fs::path &root,const Task &t) {
    size_t count=0;
    for(const auto &name:{"libraries","assets","runtime"}) {
        auto base=checked_path(from,name);if(!fs::exists(base))continue;
        for(const auto &entry:fs::recursive_directory_iterator(base)) {
            t.check();require_pack(++count<=200000,"cache_limit","游戏缓存文件数量超过限制。");
            require_pack(!entry.is_symlink(),"unsafe_cache","整合包缓存包含无效文件。");
            if(entry.is_directory())continue;
            auto filename=entry.path().filename().u8string();
            if(filename.find(".part-")!=filename.npos || filename.find(".tmp-")!=filename.npos)continue;
            require_pack(entry.is_regular_file(),"unsafe_cache","整合包缓存包含无效文件。");
            copy_verified(entry.path(),checked_path(root,entry.path().lexically_relative(from).generic_u8string()),t);
        }
    }
}
void replace_literal(Json &value,const std::string &from,const std::string &to) {
    if(value.is_string()) {
        auto text=value.get<std::string>();size_t at=0;
        while((at=text.find(from,at))!=std::string::npos){text.replace(at,from.size(),to);at+=to.size();}
        value=std::move(text);
    } else if(value.is_array() || value.is_object())for(auto &item:value)replace_literal(item,from,to);
}
Json pack_summary(const Json &info) {
    Json out=Json::object();for(const auto &key:{"format","name","version_id","summary","minecraft","loader","loader_version"})out[key]=info.at(key);return out;
}

fs::path supplement_directory(const Json &request) {
    if (!request.contains("supplement_directory")) return {};
    const auto &value = request["supplement_directory"];
    require_pack(value.is_string(), "invalid_supplement", "请选择有效的整合包补充文件目录。");
    const auto directory = value.get<std::string>();
    if (directory.empty()) return {};
    require_pack(directory.find('\0') == directory.npos, "invalid_supplement", "请选择有效的整合包补充文件目录。");
    const auto path = fs::u8path(directory);
    std::error_code ec;
    require_pack(path.is_absolute() && fs::is_directory(path, ec), "invalid_supplement",
        "请选择有效的整合包补充文件目录。");
    auto resolved = fs::canonical(path, ec);
    require_pack(!ec, "invalid_supplement", "无法读取整合包补充文件目录。");
    return resolved;
}

Json curseforge_plan_files(const Json &refs, const Json &overrides, const fs::path &supplement,
                          Json &blocked, std::uint64_t &download_size, const Task &task) {
    if (!refs.empty()) task.report("查询 CurseForge 整合包文件");
    std::map<std::string, Json> resolved;
    for (auto &descriptor : curseforge_pack_files(refs, task))
        resolved.emplace(descriptor.at("id").get<std::string>(), std::move(descriptor));
    Json files = Json::array();
    std::uint64_t total = 0;
    for (const auto &ref : refs) {
        task.check();
        const auto id = ref.at("file_id").get<std::string>();
        const auto project = ref.at("project_id").get<std::string>();
        const auto found = resolved.find(id);
        const auto missing = Json::object();
        const auto &file = found == resolved.end() ? missing : found->second;
        const auto title = file.value("title", "");
        const auto filename = file.value("filename", "");
        const auto website = file.value("website_url", "");
        const auto size = file.value("size", std::int64_t(-1));
        const auto hash = file.value("sha1", "");
        const auto kind = file.value("project_type", "");
        auto reject = [&](const std::string &reason) {
            blocked.push_back({{"project_id", project}, {"file_id", id}, {"title", title}, {"filename", filename},
                {"website_url", website}, {"client", ref.at("client")}, {"reason", reason}});
        };
        if (file.value("project_id", "") != project || !file.value("available", false)) {
            reject("file_unavailable");
            continue;
        }
        std::string destination;
        try {
            destination = curseforge_modpack_destination(kind, filename);
        } catch (const Error &e) {
            reject(e.code);
            continue;
        }
        if (hash.empty()) {
            reject("missing_checksum");
            continue;
        }
        if (size <= 0 || size > 2LL * 1024 * 1024 * 1024) {
            reject("invalid_file_size");
            continue;
        }
        bool supplemental = false;
        const auto url = file.value("url", "");
        if (!file.value("downloadable", false) || !curseforge_download_url(url)) {
            if (supplement.empty() || !present(supplement / fs::u8path(filename))) {
                reject("manual_download_required");
                continue;
            }
            try {
                verify_payload(checked_path(supplement, filename), file, task);
            } catch (const Error &) {
                task.check();
                reject("supplement_mismatch");
                continue;
            } catch (const fs::filesystem_error &) {
                reject("supplement_mismatch");
                continue;
            }
            supplemental = true;
        }
        total += size;
        require_pack(total <= 16ULL * 1024 * 1024 * 1024, "archive_limit", "整合包文件总大小超过 16 GiB 的限制。");
        if (!supplemental) download_size += size;
        files.push_back({{"project_id", project}, {"file_id", id}, {"title", title}, {"filename", filename},
            {"website_url", website}, {"client", ref.at("client")}, {"path", destination}, {"size", size}, {"sha1", hash},
            {"supplemental", supplemental}, {"urls", supplemental ? Json::array() : Json::array({url})}});
    }
    validate_curseforge_modpack_paths(files, overrides);

    // The archive pins exact files. Never substitute or recursively add
    // project dependencies outside that list. Check declared conflicts
    // between known selected files in addition to the later local JAR check.
    std::map<std::string, const Json *> projects;
    for (const auto &file : files) projects.emplace(file.at("project_id").get<std::string>(), &file);
    std::set<std::pair<std::string, std::string>> conflicts;
    for (const auto &file : files) {
        const auto project = file.at("project_id").get<std::string>();
        const auto id = file.at("file_id").get<std::string>();
        for (const auto &dependency : resolved.at(id).at("dependencies")) {
            if (dependency.at("dependency_type") != "incompatible") continue;
            const auto other = dependency.at("project_id").get<std::string>();
            const auto conflicting = projects.find(other);
            if (other == project || conflicting == projects.end() ||
                !conflicts.insert(std::minmax(project, other)).second) continue;
            auto issue = file;
            issue.erase("urls");
            issue["reason"] = "declared_conflict";
            const auto title = conflicting->second->value("title", "");
            issue["conflict_title"] = title.empty() ? other : title;
            blocked.push_back(std::move(issue));
        }
    }
    return files;
}

Json curseforge_file_identities(const Json &files) {
    Json out = Json::array();
    for (const auto &file : files) {
        Json identity;
        for (const auto &field : {"project_id", "file_id", "path", "sha1", "size", "supplemental"})
            identity[field] = file.at(field);
        out.push_back(std::move(identity));
    }
    return out;
}
}
Json plan_modpack(const Json &request,const Task &t) {
    t.report("读取整合包安装信息");
    auto info=inspect_modpack(fs::u8path(request.at("path").get<std::string>()),t);
    if (request.contains("archive_sha512"))
        require_pack(request.at("archive_sha512")==info.at("archive_sha512"),
            "plan_stale","整合包文件已变化，请重新查看安装计划。");
    const bool curseforge = info.at("format") == "curseforge";
    auto root=fs::weakly_canonical(fs::absolute(fs::u8path(request.at("root").get<std::string>())));
    require_pack(!request.at("root").get<std::string>().empty() && (!present(root) || fs::is_directory(root)),"invalid_root","请选择有效的整合包安装目录。");
    auto id=request.at("id").get<std::string>();instance_id(id);vacant(root,id,info.at("archive_sha512"));
    auto name=detail::trim(request.value("name",info.at("name").get<std::string>()));
    require_pack(!name.empty() && name.size()<=512 && name.find_first_of("\r\n\t") == name.npos,"invalid_name","整合包实例名称无效。");
    auto optional=request.value("optional_files",Json::array());
    require_pack(optional.is_array() && optional.size()<=10000,"invalid_selection","整合包可选文件选择无效。");
    std::set<std::string> selected;
    for(const auto &file:optional)require_pack(file.is_string() && selected.insert(file.get<std::string>()).second,"invalid_selection","整合包可选文件选择无效。");
    Json files=Json::array(),chosen=Json::array();std::uint64_t size=0;
    for(const auto &file:info.at("files")) {
        const auto mode=file.at("client").get<std::string>(),path=file.at("path").get<std::string>();
        if(mode=="optional" && selected.erase(path))chosen.push_back(path);
        else if(mode!="required")continue;
        files.push_back(file);
        if (!curseforge) size+=file.at("size").get<std::uint64_t>();
    }
    require_pack(selected.empty(),"invalid_selection","选择了不属于此整合包的可选文件。");
    Json blocked = Json::array();
    auto supplement = curseforge ? supplement_directory(request) : fs::path();
    if (curseforge) files = curseforge_plan_files(files, info.at("overrides"), supplement, blocked, size, t);
    Json plan{{"path",info.at("path")},{"archive_sha512",info.at("archive_sha512")},
        {"root",root.u8string()},{"id",id},{"name",name},{"directory",checked_path(root,"instances/"+id).u8string()},
        {"pack",pack_summary(info)},{"files",files},{"optional_files",chosen},
        {"skipped_server_count",info.at("server_files")},{"override_count",info.at("override_count")},
        {"total_download_bytes",size},{"resumable",!curseforge},{"blocked_files",std::move(blocked)}};
    if (!supplement.empty()) plan["supplement_directory"] = supplement.u8string();
    // Optional explicit Java comes only from user/native request, never archive.
    if(request.contains("java"))plan["java"]=request.at("java");
    return plan;
}
Json apply_modpack(const Json &reviewed,const Task &t) {
    auto plan=plan_modpack(reviewed,t);
    require_pack(reviewed.at("archive_sha512")==plan.at("archive_sha512"),"plan_stale","整合包文件已变化，请重新查看安装计划。");
    const bool curseforge = plan.at("pack").at("format") == "curseforge";
    require_pack(plan.at("blocked_files").empty(), "modpack_files_blocked", "整合包仍有需要处理的文件，请重新查看安装计划。");
    if (curseforge)
        require_pack(curseforge_file_identities(reviewed.at("files")) == curseforge_file_identities(plan.at("files")),
            "plan_stale", "CurseForge 文件信息已变化，请重新查看安装计划。");
    const auto supplement = curseforge ? supplement_directory(plan) : fs::path();
    // Recompute all paths, hashes, optional choices and overrides from the
    // archive. Cached or edited descriptor fields never authorize filesystem IO.
    auto root=fs::u8path(plan.at("root").get<std::string>());
    auto id=plan.at("id").get<std::string>(),hash=plan.at("archive_sha512").get<std::string>();
    auto base=checked_path(root,".solar-mrpack/"+cache_key(hash));
    ImportLock lock(checked_path(base,".lock"));
    ImportLock reservation(checked_path(root,".solar-mrpack-locks/"+id+".lock"));
    auto orphan=orphan_owner(root,id,hash);
    if(!orphan.is_null()) {
        auto retained=checked_path(base,"recovered/"+id+"-"+random_url_token());
        fs::create_directories(retained.parent_path());
        require_pack(rename_exclusive(checked_path(root,"instances/"+id),retained),"recovery_failed","无法恢复中断的整合包安装，请检查目标目录。");
    }
    vacant(root,id);
    auto work=checked_path(base,"attempts/"+id),game=checked_path(work,"game"),version=checked_path(work,"version");
    if(present(work)) {
        auto marker=read_json(checked_path(work,"owner.json"));
        require_pack(marker.value("archive_sha512","")==hash && marker.value("id","")==id,"cache_owner","未完成的安装目录不属于此整合包。");
        fs::remove_all(work);
    }
    fs::create_directories(game);
    const auto token=random_url_token();
    atomic_write(work/"owner.json",Json{{"archive_sha512",hash},{"id",id},{"token",token}}.dump());
    std::set<std::string> written;
    auto destination=[&](const std::string &relative) {
        auto target=checked_path(game,relative);
        require_pack(!present(target) || written.count(relative),"path_alias","整合包路径在当前文件系统上发生重名，未覆盖文件。");
        written.insert(relative);return target;
    };
    size_t done=0;
    static const Json hosts=Json::array({"cdn.modrinth.com","github.com","raw.githubusercontent.com","gitlab.com",
        "release-assets.githubusercontent.com","objects.githubusercontent.com","github-releases.githubusercontent.com"});
    for(const auto &file:plan.at("files")) {
        t.report("下载并校验整合包文件",done,plan.at("files").size());
        if (curseforge) {
            const auto to = destination(file.at("path"));
            if (file.at("supplemental") == true) {
                const auto from = checked_path(supplement, file.at("filename"));
                verify_payload(from, file, t);
                copy_verified(from, to, t);
            } else {
                auto descriptor = file;
                descriptor["source"] = "curseforge";
                descriptor["url"] = file.at("urls").at(0);
                download(descriptor, to, t);
            }
            verify_payload(to, file, t);
            ++done;
            t.report("下载并校验整合包文件",done,plan.at("files").size());
            continue;
        }
        const auto payload_hash=file.at("sha512").get<std::string>();
        auto cache=checked_path(base,"downloads/"+cache_key(payload_hash));
        auto source=cache;
        bool valid=valid_payload(cache,file,t);
        if(!valid) {
            auto legacy=legacy_cache_file(root,".solar-mrpack/"+hash+"/downloads/"+payload_hash);
            if(valid_payload(legacy,file,t)) { source=std::move(legacy);valid=true; }
        }
        if(!valid) {
            std::optional<Error> last;
            size_t attempts=0;
            for(const auto &url:file.at("urls")) {
                if(attempts++>=3)break;
                try {
                    auto descriptor=file;descriptor["url"]=url;descriptor["allowed_hosts"]=hosts;
                    download(descriptor,cache,t);verify_payload(cache,file,t);valid=true;break;
                } catch(const Error &e){t.check();last=e;}
            }
            if(!valid)throw *last;
        }
        auto to=destination(file.at("path"));copy_verified(source,to,t,payload_hash);
        ++done;t.report("下载并校验整合包文件",done,plan.at("files").size());
    }
    t.report("应用整合包配置");
    auto info=inspect_modpack(fs::u8path(plan.at("path").get<std::string>()),t);
    require_pack(info.at("archive_sha512")==hash,"plan_stale","整合包文件已变化，请重新查看安装计划。");
    auto archive=read_file(fs::u8path(plan.at("path").get<std::string>()),256*1024*1024);
    require_pack(hash_bytes(archive)==hash,"plan_stale","整合包文件已变化，请重新查看安装计划。");
    for(const auto &entry:info.at("overrides")) {
        t.check();auto to=destination(entry.at("path"));
        auto bytes=zip_entry_bytes(archive,entry.at("entry"),256*1024*1024);
        require_pack(bytes.size()==entry.at("size"),"override_invalid","整合包配置文件损坏。");
        atomic_write(to,bytes);
    }
    archive.clear();archive.shrink_to_fit();
    t.report("安装整合包所需的游戏与加载器");
    auto manifest=game_versions(t),entry=Json();
    for(const auto &candidate:manifest.at("versions"))if(candidate.value("id","")==info.at("minecraft")) {entry=candidate;break;}
    require_pack(!entry.is_null(),"game_unavailable","官方版本列表中没有此整合包要求的游戏版本。");
    auto prepared=checked_path(base,"game-root");const auto prepared_id="mrpack-"+hash.substr(0,16);
    Json request{{"root",prepared.u8string()},{"id",prepared_id},{"directory",game.u8string()},
        {"version_entry",entry},{"replace_existing",true},{"loader",info.at("loader")}};
    if(info.at("loader")!="vanilla")request["loader_version"]=info.at("loader_version");
    if(plan.contains("java"))request["java"]=plan.at("java");
    // This exact private preparation is exclusively held by ImportLock. An
    // empty legacy install lock left by a dead process cannot own live work.
    auto legacy_lock=checked_path(prepared,"versions/"+prepared_id+"/.solar-install-lock");
    if(present(legacy_lock)) {
        require_pack(fs::is_directory(legacy_lock) && fs::is_empty(legacy_lock),"unsafe_cache","整合包缓存包含无效文件。");
        fs::remove(legacy_lock);
    }
    auto preparation=t;preparation.game_cache_root=root.u8string();
    auto installed=install_game(request,preparation);
    auto metadata=installed.at("metadata"),java=installed.at("java");
    t.report("检查整合包依赖");
    Json instance{{"game_directory",game.u8string()},{"version",info.at("minecraft")},{"loader",info.at("loader")},
        {"loader_version",info.at("loader_version")},{"java_major",java.value("major",0)}};
    auto checks=check_mods(instance,local_mods(game,info.at("loader")),true,false);
    if(!checks.at("errors").empty()) {
        std::string details;size_t n=0;for(const auto &error:checks.at("errors")){if(n++>=3)break;details+=(details.empty()?"":"\n")+error.get<std::string>();}
        throw Error("modpack","dependency_conflict","整合包包含依赖冲突："+details);
    }
    t.report("准备独立游戏实例");
    promote_shared(prepared,root,t);
    const auto final_game=checked_path(root,"instances/"+id),final_version=checked_path(root,"versions/"+id);
    replace_literal(metadata,game.u8string(),final_game.u8string());
    replace_literal(metadata,prepared.u8string(),root.u8string());
    replace_literal(java,prepared.u8string(),root.u8string());
    auto runtime=probe_java(fs::u8path(java.at("path").get<std::string>()));
    require_pack(runtime.is_object() && runtime.value("major",0)==java.value("major",0),"java_unavailable","发布后的 Java 无法运行，请重试安装。");
    java=runtime;
    metadata["id"]=id;metadata["_solarGameDirectory"]=final_game.u8string();
    fs::create_directories(version);
    copy_verified(checked_path(prepared,"versions/"+prepared_id+"/"+prepared_id+".jar"),version/(id+".jar"),t);
    atomic_write(version/(id+".json"),metadata.dump(2));
    save_instance_settings(version,{{"isolated",true},{"display",{{"name",plan.at("name")}}},{"java",{{"mode","auto"}}}});
    Json receipt{{"format",info.at("format")},{"name",info.at("name")},{"version_id",info.at("version_id")},
        {"archive_sha512",hash},{"id",id},{"transaction",token},{"selected_optional_files",plan.at("optional_files")},{"imported_at",now()}};
    atomic_write(game/".solar-modpack.json",receipt.dump(2));
    t.check();vacant(root,id);
    fs::create_directories(final_game.parent_path());fs::create_directories(final_version.parent_path());
    bool game_moved=false;
    try {
        require_pack(rename_exclusive(game,final_game),"instance_exists","目标实例或游戏目录已存在，请使用其他名称。");game_moved=true;
        // Metadata publication is last: an incomplete game is not listed as a
        // launchable instance. Cancellation is deliberately not checked between
        // these two local renames so the commit cannot be interrupted halfway.
        require_pack(!present(final_version),"instance_exists","目标实例或游戏目录已存在，请使用其他名称。");
        require_pack(rename_exclusive(version,final_version),"instance_exists","目标实例或游戏目录已存在，请使用其他名称。");
    } catch(...) {
        if(game_moved)rename_exclusive(final_game,game);
        throw;
    }
    Json result{{"id",id},{"root",root.u8string()},{"directory",final_game.u8string()},{"java",java},{"metadata",metadata},
        {"modpack",receipt},{"committed",true},{"warnings",checks.at("warnings")},{"checks",checks}};
    std::error_code ec;fs::remove_all(work,ec);
    // A successful publication already owns verified game caches in root.
    // Avoid retaining a second full game/Java tree for every imported pack.
    fs::remove_all(prepared,ec);
    return result;
}
} // namespace solar::engine
