// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <regex>
#include <set>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace solar::engine {
namespace {
constexpr uint64_t byte_limit=64ULL*1024*1024*1024;
constexpr size_t file_limit=100000;
[[noreturn]] void fail(const char *code,const char *message) {throw Error("instance_backup",code,message);}
bool within(const fs::path &parent,const fs::path &child) {
    auto p=fs::weakly_canonical(parent),c=fs::weakly_canonical(child);
    return std::mismatch(p.begin(),p.end(),c.begin(),c.end()).first==p.end();
}
void name(const std::string &s) {
    if(s.empty()||s.size()>180||s=="."||s==".."||s.back()=='.'||s.back()==' ')
        fail("backup_name","备份或新实例名称无效。");
    for(unsigned char c:s)if(c<32||std::string("/\\:*?\"<>|").find(char(c))!=std::string::npos)
        fail("backup_name","备份或新实例名称无效。");
    auto stem=s.substr(0,s.find('.'));for(auto &c:stem)c=char(std::toupper((unsigned char)c));
    if(stem=="CON"||stem=="PRN"||stem=="AUX"||stem=="NUL"||
        (stem.size()==4&&(stem.substr(0,3)=="COM"||stem.substr(0,3)=="LPT")&&stem[3]>='0'&&stem[3]<='9'))
        fail("backup_name","备份或新实例名称无效。");
}
void relative_name(const std::string &s) {
    if(s.empty()||s.size()>1600||s.find('\\')!=s.npos||fs::u8path(s).is_absolute())
        fail("backup_manifest","备份清单路径无效。");
    for(const auto &part:fs::u8path(s))name(part.u8string());
}
std::string fold(std::string s){for(auto &c:s)c=char(std::tolower((unsigned char)c));return s;}
fs::path directory(const fs::path &p) {
    if(!fs::is_directory(p)||fs::is_symlink(p))fail("backup_directory","请选择有效的普通目录。");
    return fs::canonical(p);
}
void idle(const fs::path &game,const Task &t) {
    t.check();
    if(!t.active_game_directory.empty()&&fs::weakly_canonical(game)==fs::weakly_canonical(fs::u8path(t.active_game_directory)))
        fail("backup_busy","实例正在运行，请退出游戏后备份或恢复。");
}
struct Stage {
    fs::path path;
    explicit Stage(const fs::path &parent):path(checked_path(parent,".secl-backup-stage-"+random_url_token())) {
        if(!fs::create_directory(path))fail("backup_io","无法创建备份暂存目录。");
    }
    ~Stage(){std::error_code ec;fs::remove_all(path,ec);}
};
// Match the operating-system byte-range locks used by Java's session.lock.
class WorldLock {
#ifdef _WIN32
    HANDLE h=INVALID_HANDLE_VALUE; OVERLAPPED range{};
#else
    int fd=-1;
#endif
public:
    explicit WorldLock(const fs::path &p) {
#ifdef _WIN32
        h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(h==INVALID_HANDLE_VALUE||!LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&range)) {
            if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);h=INVALID_HANDLE_VALUE;
            fail("backup_world_busy","存档正在使用或无法锁定，请关闭游戏后重试。");
        }
#else
        fd=open(p.c_str(),O_RDWR);struct flock lock{};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
        if(fd<0||fcntl(fd,F_SETLK,&lock)<0){if(fd>=0)close(fd);fd=-1;fail("backup_world_busy","存档正在使用或无法锁定，请关闭游戏后重试。");}
#endif
    }
    ~WorldLock(){
#ifdef _WIN32
        if(h!=INVALID_HANDLE_VALUE){UnlockFileEx(h,0,MAXDWORD,MAXDWORD,&range);CloseHandle(h);}
#else
        if(fd>=0)close(fd);
#endif
    }
};
struct File {fs::path path;std::string relative;uint64_t size;fs::file_time_type time;};
struct Snapshot {std::vector<File> files;std::vector<std::string> directories;std::vector<fs::path> world_locks;uint64_t size=0;};
bool excluded(const std::string &relative) {
    const auto p=fs::u8path(relative);auto first=fold(p.begin()->u8string()),base=fold(p.filename().u8string());
    if(base=="session.lock"||base==".solar-history.lock")return true;
    return first=="assets"||first=="libraries"||first=="java"||first=="java-managed"||first=="runtime"||first=="versions"||
        first==".secl-instance-backups"||first=="launcher_accounts.json"||first=="launcher_profiles.json"||
        first=="accounts.json"||first=="credentials.json"||first=="refresh_tokens.json";
}
Snapshot scan(const fs::path &root,const Task &t,bool game=false) {
    Snapshot result;std::set<std::string> names;size_t visited=0;
    for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator();++it) {
        t.check();if(++visited>file_limit)fail("backup_limit","实例备份最多支持 100000 个条目和 64 GiB 文件。");
        auto relative=it->path().lexically_relative(root).generic_u8string();relative_name(relative);
        auto status=it->symlink_status();if(fs::is_symlink(status))fail("backup_link","实例或备份包含符号链接或特殊文件，操作已停止。");
        auto base=it->path().filename().u8string();
        if(game&&(base==".solar-install-lock"||base==".solar-resource-update-lock"||base.rfind(".solar-work-",0)==0||base.rfind(".secl-backup-stage-",0)==0))
            fail("backup_busy","实例存在安装或更新操作，请结束后重试。");
        if(game&&base=="session.lock") {
            if(!fs::is_regular_file(status))fail("backup_link","实例或备份包含符号链接或特殊文件，操作已停止。");
            result.world_locks.push_back(it->path());
        }
        if(game&&excluded(relative)){if(it->is_directory())it.disable_recursion_pending();continue;}
        if(!names.insert(fold(relative)).second)fail("backup_manifest","备份包含大小写冲突的路径。");
        checked_path(root,relative);
        if(fs::is_directory(status)){result.directories.push_back(relative);continue;}
        if(!fs::is_regular_file(status))fail("backup_link","实例或备份包含符号链接或特殊文件，操作已停止。");
        auto size=it->file_size();if(size>byte_limit-result.size)fail("backup_limit","实例备份最多支持 100000 个条目和 64 GiB 文件。");
        result.size+=size;result.files.push_back({it->path(),relative,size,it->last_write_time()});
    }
    std::sort(result.files.begin(),result.files.end(),[](auto&a,auto&b){return a.relative<b.relative;});
    std::sort(result.directories.begin(),result.directories.end());return result;
}
void unchanged(const File &f) {
    if(!fs::is_regular_file(f.path)||fs::is_symlink(f.path)||fs::file_size(f.path)!=f.size||fs::last_write_time(f.path)!=f.time)
        fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
}
void verify_snapshot(const fs::path &root,const Snapshot &before,const Task&t) {
    auto after=scan(root,t,true);
    if(before.directories!=after.directories||before.files.size()!=after.files.size())
        fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    for(size_t i=0;i<before.files.size();++i) {
        if(before.files[i].relative!=after.files[i].relative)fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
        unchanged(before.files[i]);
    }
}
void room(const fs::path &parent,uint64_t bytes) {
    if(fs::space(parent).available<bytes+32*1024*1024)fail("backup_space","磁盘空间不足，无法完成实例备份或恢复。");
}
// Reserve a new directory exclusively. Publish its identifying document last;
// an existing empty directory is never replaced by filesystem::rename.
void publish(const fs::path &staged,const fs::path &target,const fs::path &marker) {
    if(!fs::create_directory(target))fail("backup_exists","目标已存在，请使用新的备份或实例名称。");
    struct Guard {fs::path path;~Guard(){if(!path.empty()){std::error_code ec;fs::remove_all(path,ec);}}} guard{target};
    std::vector<fs::path> moving;for(const auto &entry:fs::directory_iterator(staged))if(entry.path().filename()!=marker)moving.push_back(entry.path());
    for(const auto &entry:moving)fs::rename(entry,target/entry.filename());
    fs::rename(staged/marker,target/marker);guard.path.clear();
}
void copy(const File &source,const fs::path &target,const Task&t,uint64_t &done,uint64_t total) {
    unchanged(source);fs::create_directories(target.parent_path());
    std::ifstream in(source.path,std::ios::binary);std::ofstream out(target,std::ios::binary|std::ios::trunc);
    if(!in||!out)fail("backup_io","备份文件读取或写入失败。");
    std::array<char,131072> buffer{};uint64_t read=0;
    while(in){t.check();in.read(buffer.data(),buffer.size());auto n=in.gcount();
        if(!out.write(buffer.data(),n))fail("backup_io","备份文件读取或写入失败。");
        read+=n;done+=n;t.report("正在复制并校验实例文件",done,total);
    }
    out.close();if(!in.eof()||!out||read!=source.size)fail("backup_io","备份文件读取或写入失败。");unchanged(source);
}
File file(const fs::path &p,const std::string &relative) {
    if(!fs::is_regular_file(p)||fs::is_symlink(p))fail("backup_profile","版本配置或客户端 JAR 缺失或无效。");
    return {p,relative,fs::file_size(p),fs::last_write_time(p)};
}
fs::path storage(const Json &r) {
    auto root=directory(fs::u8path(r.at("instance").at("root").get<std::string>()));
    const auto chosen=r.value("directory","");auto path=chosen.empty()?checked_path(root,".secl-instance-backups"):fs::u8path(chosen);
    auto game=directory(fs::u8path(r.at("instance").at("game_directory").get<std::string>()));
    auto profile=checked_path(root,"versions/"+r.at("instance").at("id").get<std::string>());
    if(within(game,path)||within(profile,path))fail("backup_recursion","请选择实例游戏目录和版本目录以外的备份位置。");
    if(!fs::exists(path)&&chosen.empty())fs::create_directory(path);
    return directory(path);
}
Json manifest(const fs::path &backup) {
    directory(backup);auto document=checked_path(backup,"manifest.json");
    auto m=Json::parse(read_file(document,64*1024*1024),nullptr,false);
    if(!m.is_object()||m.value("format","")!="secl-instance-backup"||m.value("schema",0)!=1||
        !m.contains("files")||!m.at("files").is_array()||m.at("files").empty()||m.at("files").size()>file_limit||
        !m.contains("directories")||!m.at("directories").is_array()||m.at("directories").size()>file_limit)
        fail("backup_manifest","实例备份格式或清单无效。");
    name(m.at("source_id").get<std::string>());
    uint64_t total=0;std::set<std::string> seen;
    for(auto &entry:m.at("files")) {
        if(!entry.is_object()||!entry.contains("path")||!entry.at("path").is_string()||
            !entry.contains("size")||!entry.at("size").is_number_unsigned()||
            !std::regex_match(entry.value("sha256",""),std::regex("[a-f0-9]{64}")))
            fail("backup_manifest","实例备份格式或清单无效。");
        auto p=entry.at("path").get<std::string>();relative_name(p);
        if((p.rfind("game/",0)!=0&&p.rfind("profile/",0)!=0)||!seen.insert(fold(p)).second)
            fail("backup_manifest","实例备份格式或清单无效。");
        auto size=entry.at("size").get<uint64_t>();if(size>byte_limit-total)fail("backup_limit","实例备份最多支持 100000 个条目和 64 GiB 文件。");total+=size;
    }
    for(auto &entry:m.at("directories")) {
        if(!entry.is_string())fail("backup_manifest","实例备份格式或清单无效。");
        auto p=entry.get<std::string>();relative_name(p);
        if((p!="game"&&p!="profile"&&p.rfind("game/",0)!=0&&p.rfind("profile/",0)!=0)||!seen.insert(fold(p)).second)
            fail("backup_manifest","实例备份格式或清单无效。");
    }
    m["size"]=total;return m;
}
Json summary(const fs::path &path,const Json &m) {
    return {{"path",path.u8string()},{"id",m.at("source_id")},{"name",m.value("source_name",m.at("source_id").get<std::string>())},
        {"created",m.value("created",int64_t(0))},{"size",m.at("size")},{"file_count",m.at("files").size()},
        {"requires_shared_dependencies",true},{"problem",""}};
}
Json create(const Json&r,const Task&t) {
    const auto &instance=r.at("instance");auto id=instance.at("id").get<std::string>();name(id);
    auto root=directory(fs::u8path(instance.at("root").get<std::string>()));
    auto profile=directory(checked_path(root,"versions/"+id));auto game=directory(fs::u8path(instance.at("game_directory").get<std::string>()));
    if(game==root||within(game,root)||(game!=profile&&within(game,profile)))
        fail("backup_shared","此实例使用共享游戏目录，请先改为独立目录再备份。");
    idle(game,t);auto parent=storage(r);auto snapshot=scan(game,t,true);
    std::vector<std::unique_ptr<WorldLock>> locks;
    for(const auto &p:snapshot.world_locks) {t.check();locks.push_back(std::make_unique<WorldLock>(p));}
    auto resolved=load_version(root,id);auto initial=resolved.dump();
    auto jarid=resolved.value("jar",id);name(jarid);
    std::vector<File> profiles;std::set<std::string> chain;std::string current=id;
    for(int depth=0;;++depth) {
        name(current);if(depth>8||!chain.insert(current).second)fail("backup_profile","版本配置或客户端 JAR 缺失或无效。");
        auto original=file(checked_path(root,"versions/"+current+"/"+current+".json"),"profile/originals/"+current+".json");profiles.push_back(original);
        auto metadata=Json::parse(read_file(original.path,16*1024*1024));
        if(!metadata.contains("inheritsFrom"))break;current=metadata.at("inheritsFrom").get<std::string>();
    }
    profiles.push_back(file(checked_path(root,"versions/"+jarid+"/"+jarid+".jar"),"profile/client.jar"));
    for(auto pair:{std::pair<const char*,const char*>{".solar-instance.json","profile/settings.json"},
                   {".solar-history.json","profile/history.json"}}) {
        auto p=checked_path(profile,pair.first);
        if(!fs::exists(p)&&std::string(pair.first)==".solar-instance.json")p=checked_path(profile,".m2cl-instance.json");
        if(fs::exists(p))profiles.push_back(file(p,pair.second));
    }
    uint64_t total=snapshot.size;for(auto &f:profiles){if(f.size>byte_limit-total)fail("backup_limit","实例备份最多支持 100000 个条目和 64 GiB 文件。");total+=f.size;}
    if(snapshot.files.size()+profiles.size()+snapshot.directories.size()>file_limit)fail("backup_limit","实例备份最多支持 100000 个条目和 64 GiB 文件。");
    room(parent,total);Stage stage(parent);auto data=stage.path/"data";fs::create_directories(data/"game");fs::create_directories(data/"profile/originals");
    Json entries=Json::array();std::map<std::string,std::string> copied_hashes;uint64_t done=0;
    auto add=[&](const File&f,const std::string &relative){
        auto out=checked_path(data,relative);copy(f,out,t,done,total);
        auto hash=hash_file(out,"sha256",t.cancel);
        if(hash_file(f.path,"sha256",t.cancel)!=hash)fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
        entries.push_back({{"path",relative},{"size",f.size},{"sha256",hash}});copied_hashes[relative]=hash;
    };
    for(auto &d:snapshot.directories)fs::create_directories(checked_path(data,"game/"+d));
    for(auto &f:snapshot.files)add(f,"game/"+f.relative);
    for(auto &f:profiles)add(f,f.relative);
    atomic_write(data/"profile/version.json",resolved.dump(2));
    entries.push_back({{"path","profile/version.json"},{"size",fs::file_size(data/"profile/version.json")},{"sha256",hash_file(data/"profile/version.json","sha256",t.cancel)}});
    if(std::none_of(entries.begin(),entries.end(),[](const Json&e){return e.at("path")=="profile/settings.json";})) {
        atomic_write(data/"profile/settings.json","{}");entries.push_back({{"path","profile/settings.json"},{"size",uint64_t(2)},{"sha256",hash_file(data/"profile/settings.json","sha256",t.cancel)}});
    }
    verify_snapshot(game,snapshot,t);for(auto &f:profiles)unchanged(f);
    if(load_version(root,id).dump()!=initial)fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    // Rehash after all copies: detect changes even if size and mtime are restored.
    for(auto &f:snapshot.files) {
        if(hash_file(f.path,"sha256",t.cancel)!=copied_hashes.at("game/"+f.relative))fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    }
    for(auto &f:profiles) {
        if(hash_file(f.path,"sha256",t.cancel)!=copied_hashes.at(f.relative))fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    }
    auto actual=scan(data,t);Json dirs=actual.directories;
    Json m={{"format","secl-instance-backup"},{"schema",1},{"source_id",id},{"source_name",instance.value("name",id)},
        {"created",now()},{"minecraft",instance.value("version",resolved.value("_baseId",resolved.value("id",id)))},{"files",entries},{"directories",dirs},{"requires_shared_dependencies",true}};
    atomic_write(stage.path/"manifest.json",m.dump(2));t.check();idle(game,t);
    auto output=checked_path(parent,id.substr(0,60)+"-"+std::to_string(now())+"-"+random_url_token()+".secl-backup");
    if(fs::exists(output))fail("backup_exists","目标已存在，请使用新的备份或实例名称。");
    auto validated=manifest(stage.path);publish(stage.path,output,"manifest.json");return summary(output,validated);
}
void rewrite(Json &m,const std::string &id,const fs::path &game,const std::string &base_version,int depth=0) {
    if(!m.is_object()||depth>8)fail("backup_profile","版本配置或客户端 JAR 缺失或无效。");
    if(!m.contains("_baseId")&&!base_version.empty())m["_baseId"]=base_version;
    m["id"]=id;m["jar"]=id;m["_solarGameDirectory"]=game.u8string();m.erase("_m2clGameDirectory");m.erase("inheritsFrom");
    for(const auto &key:{"_solarVanilla","_m2clVanilla"})if(m.contains(key)&&m.at(key).is_object())rewrite(m[key],id,game,base_version,depth+1);
    for(const auto &key:{"_solarOptifine","_m2clOptifine"})if(m.contains(key)&&m.at(key).is_object()&&m.at(key).contains("base"))rewrite(m[key]["base"],id,game,base_version,depth+1);
}
Json restore(const Json&r,const Task&t) {
    auto backup=directory(fs::u8path(r.at("backup").get<std::string>()));auto m=manifest(backup);
    auto data=directory(checked_path(backup,"data"));auto actual=scan(data,t);
    if(actual.files.size()!=m.at("files").size()||Json(actual.directories)!=m.at("directories"))fail("backup_manifest","备份文件与清单不一致，未恢复任何实例。");
    std::map<std::string,File> files;for(auto &f:actual.files)files.emplace(f.relative,f);
    for(auto &e:m.at("files")) {
        auto it=files.find(e.at("path"));if(it==files.end()||it->second.size!=e.at("size").get<uint64_t>()||hash_file(it->second.path,"sha256",t.cancel)!=e.at("sha256"))
            fail("backup_checksum","备份文件校验失败，未恢复任何实例。");
    }
    for(const auto &needed:{"profile/version.json","profile/client.jar","profile/settings.json"})
        if(!files.count(needed))fail("backup_profile","版本配置或客户端 JAR 缺失或无效。");
    auto id=r.at("id").get<std::string>();name(id);
    if(id==m.at("source_id"))fail("backup_new_id","恢复必须使用不同的新实例标识。");
    auto root=directory(fs::u8path(r.at("root").get<std::string>()));auto versions=checked_path(root,"versions");
    auto target=checked_path(root,"versions/"+id);auto game=target/"game";
    if(fs::exists(target))fail("backup_exists","目标已存在，请使用新的备份或实例名称。");
    if(within(backup,target)||within(target,backup))fail("backup_recursion","请选择实例游戏目录和版本目录以外的备份位置。");
    if(r.contains("instance"))idle(fs::u8path(r.at("instance").at("game_directory").get<std::string>()),t);
    fs::create_directories(versions);room(versions,m.at("size").get<uint64_t>());Stage stage(versions);
    fs::create_directories(stage.path/"game");uint64_t done=0,total=m.at("size");
    for(auto &d:m.at("directories"))if(d.get<std::string>().rfind("game/",0)==0)fs::create_directories(checked_path(stage.path,d));
    for(auto &e:m.at("files")) {
        const auto relative=e.at("path").get<std::string>();const auto &f=files.at(relative);
        fs::path out;
        if(relative.rfind("game/",0)==0)out=checked_path(stage.path,relative);
        else if(relative=="profile/client.jar")out=stage.path/fs::u8path(id+".jar");
        else if(relative=="profile/history.json")out=stage.path/".solar-history.json";
        else continue;
        copy(f,out,t,done,total);
        if(hash_file(out,"sha256",t.cancel)!=e.at("sha256"))fail("backup_checksum","备份文件校验失败，未恢复任何实例。");
    }
    auto metadata=Json::parse(read_file(files.at("profile/version.json").path,16*1024*1024));
    rewrite(metadata,id,game,m.value("minecraft",metadata.value("_baseId",metadata.value("id",std::string()))));if(!metadata.contains("mainClass"))fail("backup_profile","版本配置或客户端 JAR 缺失或无效。");
    auto settings=Json::parse(read_file(files.at("profile/settings.json").path,4*1024*1024));
    if(!settings.is_object())fail("backup_manifest","实例备份格式或清单无效。");
    settings["isolated"]=true;settings["java"]={{"mode","auto"}};
    if(!settings.contains("display")||!settings["display"].is_object())settings["display"]=Json::object();
    settings["display"]["name"]=r.value("name",m.value("source_name",id)+" (restored)");settings["display"]["cover"]="";
    save_instance_settings(stage.path,settings);atomic_write(stage.path/fs::u8path(id+".json"),metadata.dump(2));
    // Inputs may be edited during restoration. Never publish that mixed snapshot.
    for(auto &e:m.at("files"))if(hash_file(files.at(e.at("path").get<std::string>()).path,"sha256",t.cancel)!=e.at("sha256"))
        fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    auto after=scan(data,t);
    if(after.directories!=actual.directories||after.files.size()!=actual.files.size())fail("backup_changed","源文件在操作期间发生变化，未发布结果，请关闭游戏后重试。");
    for(const auto &f:actual.files)unchanged(f);
    t.report("正在完成实例恢复",total,total);if(fs::exists(target))fail("backup_exists","目标已存在，请使用新的备份或实例名称。");publish(stage.path,target,fs::u8path(id+".json"));
    return {{"id",id},{"name",settings["display"]["name"]},{"path",target.u8string()},{"root",root.u8string()},
        {"game_directory",game.u8string()},{"requires_shared_dependencies",true},{"restored",true}};
}
} // namespace
Json instance_backups_command(const Json&r,const Task&t) {
    try {
        const auto op=r.at("op").get<std::string>();
        if(op=="instance.backups.create")return create(r,t);
        if(op=="instance.backups.restore")return restore(r,t);
        if(op=="instance.backups.list") {
            auto parent=storage(r);Json rows=Json::array();size_t visited=0;
            for(auto &e:fs::directory_iterator(parent)) {
                t.check();if(++visited>10000)fail("backup_limit","备份位置包含过多条目，请选择更具体的目录。");
                if(e.path().extension()!=".secl-backup")continue;
                try{rows.push_back(summary(e.path(),manifest(e.path())));}catch(const Error&error){
                    if(error.code=="cancelled")throw;
                    rows.push_back({{"path",e.path().u8string()},{"name",e.path().filename().u8string()},{"problem",error.what()}});
                }
            }
            std::sort(rows.begin(),rows.end(),[](const Json&a,const Json&b){return a.value("created",int64_t(0))>b.value("created",int64_t(0));});
            return {{"directory",parent.u8string()},{"items",rows}};
        }
        fail("backup_operation","不支持的实例备份操作。");
    }catch(const fs::filesystem_error&){fail("backup_io","备份文件读取或写入失败。");}
}
} // namespace solar::engine
