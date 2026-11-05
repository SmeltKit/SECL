// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace solar::engine {
namespace {
constexpr uint64_t maxBytes = 64ULL * 1024 * 1024 * 1024;
constexpr size_t maxFiles = 100000;
[[noreturn]] void fail(const char *code, const char *message) { throw Error("worlds", code, message); }
void safe_name(const std::string &name) {
    if (name.empty() || name.size()>180 || name=="." || name==".." || name.back()=='.' || name.back()==' ')
        fail("world_name", "存档文件夹名称无效。");
    for (unsigned char c:name) if (c<32 || std::string("/\\:*?\"<>|").find(c)!=std::string::npos)
        fail("world_name", "存档文件夹名称无效。");
    auto stem=name.substr(0,name.find('.')); for(auto &c:stem)c=char(std::toupper((unsigned char)c));
    if(stem=="CON" || stem=="PRN" || stem=="AUX" || stem=="NUL" ||
       (stem.size()==4 && (stem.substr(0,3)=="COM" || stem.substr(0,3)=="LPT") && stem[3]>='0' && stem[3]<='9'))
        fail("world_name", "存档文件夹名称无效。");
}
fs::path game_path(const Json &instance) {
    auto p=fs::u8path(instance.at("game_directory").get<std::string>());
    if(!fs::is_directory(p))fail("world_directory", "游戏目录不存在。");
    return fs::canonical(p);
}
fs::path world_path(const fs::path &game,const std::string &name) {
    safe_name(name); return checked_path(game,"saves/"+name);
}
bool inside(const fs::path &parent,const fs::path &child) {
    auto p=fs::weakly_canonical(parent),c=fs::weakly_canonical(child);
    auto mismatch=std::mismatch(p.begin(),p.end(),c.begin(),c.end());return mismatch.first==p.end();
}
struct Stage {
    fs::path path;
    explicit Stage(const fs::path &parent):path(parent/(".secl-world-stage-"+random_url_token())) {fs::create_directories(path);}
    ~Stage(){std::error_code ec;fs::remove_all(path,ec);}
};
// Java FileLock uses OS byte-range locks. Mere existence of session.lock does
// not mean a world is open; an inactive world's old lock file is harmless.
class WorldLock {
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE; OVERLAPPED overlap{};
#else
    int file=-1;
#endif
public:
    explicit WorldLock(const fs::path &world) {
        auto p=checked_path(world,"session.lock"); if(!fs::exists(p))return;
#ifdef _WIN32
        file=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(file==INVALID_HANDLE_VALUE || !LockFileEx(file,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&overlap)) {
            if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);file=INVALID_HANDLE_VALUE;
            fail("world_busy", "存档正在使用或无法锁定，请关闭游戏后重试。");
        }
#else
        file=open(p.c_str(),O_RDWR);
        struct flock lock{};lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
        if(file<0 || fcntl(file,F_SETLK,&lock)<0){if(file>=0)close(file);file=-1;fail("world_busy", "存档正在使用或无法锁定，请关闭游戏后重试。");}
#endif
    }
    ~WorldLock(){
#ifdef _WIN32
        if(file!=INVALID_HANDLE_VALUE){UnlockFileEx(file,0,MAXDWORD,MAXDWORD,&overlap);CloseHandle(file);}
#else
        if(file>=0)close(file);
#endif
    }
};
void idle(const fs::path &game,const Task &t) {
    if(!t.active_game_directory.empty() && fs::equivalent(game,fs::u8path(t.active_game_directory)))
        fail("world_busy", "该实例的游戏正在运行，请退出游戏后管理存档。");
    t.check();
}
// NBT strings use Java modified UTF-8, including surrogate pairs for emoji.
std::string nbt_utf8(const std::string &input) {
    std::vector<uint32_t> points;
    for(size_t i=0;i<input.size();) {
        auto c=(unsigned char)input[i++];uint32_t v=c;int more=0;
        if(c>=0xc0 && c<=0xdf){v=c&31;more=1;}else if(c>=0xe0 && c<=0xef){v=c&15;more=2;}else if(c>=0xf0 && c<=0xf4){v=c&7;more=3;}else if(c>=0x80){points.push_back(0xfffd);continue;}
        if(i+more>input.size()){points.push_back(0xfffd);break;}
        bool valid=true;for(int k=0;k<more;k++){auto next=(unsigned char)input[i];if((next&0xc0)!=0x80){valid=false;break;}v=(v<<6)|(next&63);++i;}
        points.push_back(valid?v:0xfffd);
    }
    std::string out;
    for(size_t i=0;i<points.size();++i){auto v=points[i];
        if(v>=0xd800 && v<=0xdbff && i+1<points.size() && points[i+1]>=0xdc00 && points[i+1]<=0xdfff){v=0x10000+((v-0xd800)<<10)+(points[++i]-0xdc00);}
        else if(v>0x10ffff || (v>=0xd800 && v<=0xdfff))v=0xfffd;
        if(v<0x80)out+=char(v);else if(v<0x800){out+=char(0xc0|(v>>6));out+=char(0x80|(v&63));}
        else if(v<0x10000){out+=char(0xe0|(v>>12));out+=char(0x80|((v>>6)&63));out+=char(0x80|(v&63));}
        else{out+=char(0xf0|(v>>18));out+=char(0x80|((v>>12)&63));out+=char(0x80|((v>>6)&63));out+=char(0x80|(v&63));}
    }return out;
}
// Parse bounded Java NBT, retaining only small scalar metadata. Region files
// and unrelated arrays are skipped rather than materialized as JSON.
class Nbt {
    std::string b; size_t pos=0, nodes=0;
    uint64_t number(size_t n){if(n>b.size()-pos)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");uint64_t v=0;while(n--)v=(v<<8)|(unsigned char)b[pos++];return v;}
    void skip(uint64_t n){if(n>b.size()-pos)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");pos+=size_t(n);}
    std::string text(){auto n=number(2);if(n>b.size()-pos)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");auto s=b.substr(pos,n);pos+=n;return nbt_utf8(s);}
    void tag(int type,const std::string &path,Json &out,int depth) {
        if(depth>64 || ++nodes>1000000)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");
        static const std::set<std::string> keep={"Data/LevelName","Data/LastPlayed","Data/GameType","Data/hardcore","Data/Version/Name","Data/Version/Id","Data/DataVersion","Data/RandomSeed","Data/WorldGenSettings/seed"};
        bool wanted=keep.count(path);
        if(type>=1 && type<=4){size_t n=size_t(1)<<(type-1);auto v=number(n);if(wanted){if(n<8 && (v&(1ULL<<(n*8-1))))v|=(~0ULL)<<(n*8);out[path]=int64_t(v);}return;}
        if(type==5 || type==6){skip(type==5?4:8);return;}
        if(type==8){auto s=text();if(wanted)out[path]=s;return;}
        if(type==7 || type==11 || type==12){auto count=number(4);if(count>0x7fffffff)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");skip(count*(type==7?1:type==11?4:8));return;}
        if(type==9){int child=int(number(1));auto count=number(4);if(count>1000000 || (child==0 && count))fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");for(uint64_t i=0;i<count;i++)tag(child,"",out,depth+1);return;}
        if(type==10){for(;;){int child=int(number(1));if(!child)return;auto name=text();tag(child,path.empty()?name:path+"/"+name,out,depth+1);}}
        else fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");
    }
public:
    explicit Nbt(const fs::path &p) {
        auto input=read_file(p,16*1024*1024);
        if(input.size()>2 && (unsigned char)input[0]==31 && (unsigned char)input[1]==139) {
            z_stream z{};if(inflateInit2(&z,16+MAX_WBITS)!=Z_OK)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");
            z.next_in=(Bytef*)input.data();z.avail_in=uInt(input.size());int result=Z_OK;std::array<char,65536>buffer{};
            while(result==Z_OK){z.next_out=(Bytef*)buffer.data();z.avail_out=buffer.size();result=inflate(&z,Z_NO_FLUSH);b.append(buffer.data(),buffer.size()-z.avail_out);if(b.size()>16*1024*1024){inflateEnd(&z);fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");}}
            bool valid=result==Z_STREAM_END && z.avail_in==0;inflateEnd(&z);if(!valid)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");
        }else b=std::move(input);
    }
    Json parse(){if(number(1)!=10)fail("world_nbt", "存档信息无法读取，仍可导出原始文件。");text();Json out=Json::object();tag(10,"",out,0);return out;}
};
struct File {fs::path path;std::string relative;uint64_t size;fs::file_time_type time;};
std::vector<File> files(const fs::path &root,const Task &t,uint64_t &total) {
    std::vector<File> result;total=0;size_t visited=0;
    for(auto &entry:fs::recursive_directory_iterator(root)) {
        t.check();if(++visited>maxFiles)fail("world_limit", "存档体积或文件数量超过当前支持范围。");if(fs::is_symlink(entry.symlink_status()))fail("world_link", "存档中含符号链接，操作已停止。");
        if(entry.is_directory())continue;
        if(!entry.is_regular_file())fail("world_link", "存档中含特殊文件，操作已停止。");
        auto relative=entry.path().lexically_relative(root).generic_u8string();
        if(relative=="session.lock")continue;
        checked_path(root,relative);auto size=entry.file_size();total+=size;
        if(result.size()>=maxFiles || total>maxBytes)fail("world_limit", "存档体积或文件数量超过当前支持范围。");
        result.push_back({entry.path(),relative,size,entry.last_write_time()});
    }
    std::sort(result.begin(),result.end(),[](const File&a,const File&b){return a.relative<b.relative;});return result;
}
void verify_files(const fs::path &root,const std::vector<File> &expected,const Task &t) {
    uint64_t size;auto current=files(root,t,size);
    if(current.size()!=expected.size())fail("world_changed", "存档文件发生变化，请退出游戏后重试。");
    for(size_t i=0;i<current.size();i++)if(current[i].relative!=expected[i].relative || current[i].size!=expected[i].size || current[i].time!=expected[i].time)fail("world_changed", "存档文件发生变化，请退出游戏后重试。");
}
void unchanged(const File &f){if(!fs::is_regular_file(f.path) || fs::is_symlink(f.path) || fs::file_size(f.path)!=f.size || fs::last_write_time(f.path)!=f.time)fail("world_changed", "存档文件发生变化，请退出游戏后重试。");}
void room(const fs::path &p,uint64_t size){if(fs::space(p).available<size+16*1024*1024)fail("world_space", "磁盘空间不足。");}
void copy_world(const fs::path &source,const fs::path &dest,const Task &t) {
    uint64_t total;auto entries=files(source,t,total);room(dest.parent_path(),total);fs::create_directory(dest);
    std::array<char,131072>buffer{};uint64_t done=0;
    for(auto &f:entries) {
        unchanged(f);auto out=checked_path(dest,f.relative);fs::create_directories(out.parent_path());
        std::ifstream in(f.path,std::ios::binary);std::ofstream to(out,std::ios::binary);uint64_t read=0;
        while(in){t.check();in.read(buffer.data(),buffer.size());auto n=in.gcount();if(!to.write(buffer.data(),n))fail("world_write", "存档文件写入失败。");read+=n;done+=n;t.report("正在复制存档",done,total);}
        to.close();if(!in.eof() || !to || read!=f.size)fail("world_read", "存档文件读取失败。");unchanged(f);
    }
    verify_files(source,entries,t);
}
std::string b64(const std::string&b){static const char *a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string s;unsigned v=0;int bits=-6;for(unsigned char c:b){v=(v<<8)|c;bits+=8;while(bits>=0){s+=a[(v>>bits)&63];bits-=6;}}if(bits>-6)s+=a[((v<<8)>>(bits+8))&63];while(s.size()%4)s+='=';return s;}
Json info(const fs::path &p,const Task&t,bool size=true) {
    Json row={{"folder",p.filename().u8string()},{"name",p.filename().u8string()},{"version",""},{"last_played",0},{"mode",-1},{"hardcore",false},{"problem",""}};
    try{auto n=Nbt(checked_path(p,"level.dat")).parse();row["name"]=n.value("Data/LevelName",p.filename().u8string());row["version"]=n.value("Data/Version/Name","");row["last_played"]=n.value("Data/LastPlayed",int64_t(0));row["mode"]=n.value("Data/GameType",-1);row["hardcore"]=n.value("Data/hardcore",0)!=0;}catch(...){row["problem"]="存档信息无法读取，仍可导出原始文件。";}
    try{auto icon=read_file(checked_path(p,"icon.png"),1024*1024);if(icon.size()>=24 && icon.substr(0,8)==std::string("\x89PNG\r\n\x1a\n",8))row["icon_base64"]=b64(icon);}catch(...){}
    if(size)try{uint64_t total;files(p,t,total);row["size"]=total;}catch(const Error&e){if(e.code=="cancelled")throw;row["size"]=nullptr;row["problem"]=e.what();}
    return row;
}
void real_world(const fs::path &p){if(!fs::is_directory(p) || !fs::is_regular_file(checked_path(p,"level.dat")))fail("world_missing", "未找到 Java 版存档的 level.dat。");}
fs::path discover(const fs::path &root) {
    if(fs::is_regular_file(checked_path(root,"level.dat")))return root;
    std::vector<fs::path> found;size_t visited=0;
    for(auto it=fs::recursive_directory_iterator(root);it!=fs::recursive_directory_iterator();++it) {
        if(++visited>maxFiles)fail("world_limit", "存档体积或文件数量超过当前支持范围。");
        if(fs::is_symlink(it->symlink_status()))fail("world_link", "存档中含符号链接，操作已停止。");
        if(it.depth()>4){it.disable_recursion_pending();continue;}
        if(it->path().filename()=="level.dat" && it->is_regular_file())found.push_back(it->path().parent_path());
    }
    if(found.size()!=1)fail("world_layout", "请选择只包含一个世界的存档目录或 ZIP。");return found[0];
}
void publish(const fs::path &stage,const fs::path &target){if(fs::exists(target))fail("world_exists", "同名存档已存在，请换一个文件夹名称。");fs::create_directories(target.parent_path());fs::rename(stage,target);}
} // namespace
// ZIP IO is streaming and bounded. ZIP64 is rejected explicitly; folder import,
// copy and recovery remain available for worlds beyond the classic ZIP limits.
void export_world_zip(const fs::path &,const fs::path &,const Task &);
void import_world_zip(const fs::path &,const fs::path &,const Task &);
Json worlds_command(const Json &r,const Task&t) {
    auto op=r.at("op").get<std::string>();auto game=game_path(r.at("instance"));auto saves=checked_path(game,"saves");
    if(op=="worlds.open"){open_browser((fs::is_directory(saves)?saves:game).u8string());return {{"opened",true}};}
    if(op=="worlds.list") {
        Json rows=Json::array();if(fs::is_directory(saves))for(auto &e:fs::directory_iterator(saves)){
            t.check();if(e.is_directory() && !fs::is_symlink(e.symlink_status())){try{if(fs::is_regular_file(checked_path(e.path(),"level.dat")))rows.push_back(info(e.path(),t));}catch(const Error&e){if(e.code=="cancelled")throw;}}
        }
        std::sort(rows.begin(),rows.end(),[](const Json&a,const Json&b){return a["last_played"]>b["last_played"];});return rows;
    }
    auto backups=checked_path(game,"backups/secl");
    if(op=="worlds.backups") {
        Json rows=Json::array();if(fs::is_directory(backups))for(auto&e:fs::directory_iterator(backups)){
            t.check();if(e.is_regular_file() && e.path().extension()==".json" && !fs::is_symlink(e.symlink_status()))try{
                auto data=Json::parse(read_file(e.path(),16384));safe_name(data.at("archive"));auto archive=checked_path(backups,data.at("archive"));
                if(fs::is_regular_file(archive)){data["size"]=fs::file_size(archive);rows.push_back(data);}
            }catch(...){}
        }std::sort(rows.begin(),rows.end(),[](const Json&a,const Json&b){return a.value("created",int64_t(0))>b.value("created",int64_t(0));});return rows;
    }
    idle(game,t);
    if(op=="worlds.import" || op=="worlds.restore") {
        fs::path source;
        if(op=="worlds.restore"){auto archive=r.at("archive").get<std::string>();safe_name(archive);source=checked_path(backups,archive);}
        else source=fs::u8path(r.at("path").get<std::string>());
        if(fs::is_symlink(source))fail("world_link", "存档中含符号链接，操作已停止。");
        auto name=r.at("name").get<std::string>();auto target=world_path(game,name);if(fs::exists(target))fail("world_exists", "同名存档已存在，请换一个文件夹名称。");
        Stage stage(game);auto data=stage.path/"data";
        if(fs::is_directory(source)){auto world=discover(source);real_world(world);WorldLock lock(world);if(inside(world,stage.path))fail("world_layout", "不能把存档复制到自身目录中。");copy_world(world,data,t);}
        else{import_world_zip(source,stage.path/"unpacked",t);auto world=discover(stage.path/"unpacked");real_world(world);fs::rename(world,data);}
        real_world(data);t.check();publish(data,target);return info(target,t,false);
    }
    auto source=world_path(game,r.at("folder").get<std::string>());real_world(source);WorldLock lock(source);
    if(op=="worlds.remove"){move_to_trash(source);return {{"removed",true}};}
    if(op=="worlds.copy") {
        auto targetGame=r.contains("target_instance")?game_path(r.at("target_instance")):game;idle(targetGame,t);
        auto target=world_path(targetGame,r.at("name"));if(fs::exists(target))fail("world_exists", "同名存档已存在，请换一个文件夹名称。");
        Stage stage(targetGame);if(inside(source,stage.path))fail("world_layout", "不能把存档复制到自身目录中。");copy_world(source,stage.path/"data",t);t.check();publish(stage.path/"data",target);return info(target,t,false);
    }
    if(op=="worlds.export" || op=="worlds.backup") {
        bool backup=op=="worlds.backup";fs::path output;
        if(backup){fs::create_directories(backups);output=backups/("world-"+std::to_string(now())+"-"+random_url_token()+".zip");}
        else output=fs::u8path(r.at("path").get<std::string>());
        if(fs::exists(output))fail("world_export_exists", "导出文件已存在，请选择其他文件名。");
        if(!fs::is_directory(output.parent_path()) || inside(source,output))fail("world_export_path", "请选择存档目录以外的有效导出位置。");
        export_world_zip(source,output,t);
        if(backup){Json record={{"archive",output.filename().u8string()},{"world",source.filename().u8string()},{"name",info(source,t,false)["name"]},{"created",now()},{"version",r.at("instance").value("version","")}};
            try{atomic_write(output.parent_path()/(output.stem().u8string()+".json"),record.dump(2));}catch(...){std::error_code ec;fs::remove(output,ec);throw;}return record;}
        return {{"exported",true}};
    }
    fail("world_operation", "不支持的存档操作。");
}
// Keep ZIP writing alongside copy metadata checks without exposing file manifests.
void export_world_zip(const fs::path &source,const fs::path &output,const Task&t) {
    uint64_t total;auto entries=files(source,t,total);room(output.parent_path(),total+1024*1024);
    if(entries.size()>=65535 || total>=0xffffffffULL-32*1024*1024)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
    auto temp=output;temp+=".partial-"+random_url_token();
    auto word=[](std::ostream&o,uint64_t v,int n){while(n--){o.put(char(v&255));v>>=8;}};
    struct Entry{std::string name;uint32_t size,compressed,crc,offset;};std::vector<Entry> central;
    try{
        std::ofstream out(temp,std::ios::binary);std::array<char,131072>inbuf{},outbuf{};uint64_t done=0;
        for(auto&f:entries){t.check();unchanged(f);if(f.relative.size()>65535)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
            auto start=uint64_t(out.tellp());if(start>=0xffffffffULL)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
            word(out,0x04034b50,4);word(out,20,2);word(out,0x808,2);word(out,8,2);word(out,0,2);word(out,33,2);word(out,0,4);word(out,0,4);word(out,0,4);word(out,f.relative.size(),2);word(out,0,2);out<<f.relative;
            auto dataStart=uint64_t(out.tellp());std::ifstream in(f.path,std::ios::binary);uint64_t read=0;uLong crc=crc32(0,nullptr,0);z_stream z{};
            if(deflateInit2(&z,Z_DEFAULT_COMPRESSION,Z_DEFLATED,-MAX_WBITS,8,Z_DEFAULT_STRATEGY)!=Z_OK)fail("world_zip", "存档压缩失败。");
            try{int status=Z_OK;do{t.check();in.read(inbuf.data(),inbuf.size());auto n=in.gcount();if(!in.eof() && !in)fail("world_read", "存档文件读取失败。");read+=n;done+=n;crc=crc32(crc,(Bytef*)inbuf.data(),uInt(n));z.next_in=(Bytef*)inbuf.data();z.avail_in=uInt(n);
                do{z.next_out=(Bytef*)outbuf.data();z.avail_out=outbuf.size();status=deflate(&z,in.eof()?Z_FINISH:Z_NO_FLUSH);if(status<0)fail("world_zip", "存档压缩失败。");out.write(outbuf.data(),outbuf.size()-z.avail_out);}while(z.avail_out==0);
                t.report("正在导出存档",done,total);
            }while(status!=Z_STREAM_END);deflateEnd(&z);}catch(...){deflateEnd(&z);throw;}
            unchanged(f);if(read!=f.size)fail("world_changed", "存档文件发生变化，请退出游戏后重试。");auto compressed=uint64_t(out.tellp())-dataStart;
            if(compressed>=0xffffffffULL || !out)fail("world_write", "存档文件写入失败。");
            word(out,0x08074b50,4);word(out,crc,4);word(out,compressed,4);word(out,read,4);
            central.push_back({f.relative,uint32_t(read),uint32_t(compressed),uint32_t(crc),uint32_t(start)});
        }
        auto offset=uint64_t(out.tellp());
        for(auto&e:central){word(out,0x02014b50,4);word(out,20,2);word(out,20,2);word(out,0x808,2);word(out,8,2);word(out,0,2);word(out,33,2);word(out,e.crc,4);word(out,e.compressed,4);word(out,e.size,4);word(out,e.name.size(),2);word(out,0,2);word(out,0,2);word(out,0,2);word(out,0,2);word(out,0,4);word(out,e.offset,4);out<<e.name;}
        auto end=uint64_t(out.tellp());if(end>=0xffffffffULL)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
        word(out,0x06054b50,4);word(out,0,4);word(out,central.size(),2);word(out,central.size(),2);word(out,end-offset,4);word(out,offset,4);word(out,0,2);out.close();if(!out)fail("world_write", "存档文件写入失败。");
        verify_files(source,entries,t);t.check();publish(temp,output);
    }catch(...){std::error_code ec;fs::remove(temp,ec);throw;}
}
void import_world_zip(const fs::path &input,const fs::path &dest,const Task&t) {
    std::ifstream in(input,std::ios::binary);if(!in)fail("world_read", "存档文件读取失败。");
    auto read=[&](uint64_t offset,size_t n){std::string b(n,'\0');in.clear();in.seekg(std::streamoff(offset));if(!in.read(b.data(),n))fail("world_zip", "存档 ZIP 损坏或不受支持。");return b;};
    auto num=[](const std::string&b,size_t p,int n)->uint64_t{if(p+size_t(n)>b.size())fail("world_zip", "存档 ZIP 损坏或不受支持。");uint64_t v=0;for(int i=n-1;i>=0;--i)v=(v<<8)|(unsigned char)b[p+i];return v;};
    in.seekg(0,std::ios::end);auto length=uint64_t(in.tellg());if(length<22)fail("world_zip", "存档 ZIP 损坏或不受支持。");
    auto tail=read(length-std::min<uint64_t>(length,65557),size_t(std::min<uint64_t>(length,65557)));size_t end=std::string::npos;
    for(size_t p=tail.size()-22;;--p){if(num(tail,p,4)==0x06054b50 && p+22+num(tail,p+20,2)==tail.size()){end=p;break;}if(!p)break;}
    if(end==std::string::npos)fail("world_zip", "存档 ZIP 损坏或不受支持。");
    auto count=num(tail,end+10,2),centralSize=num(tail,end+12,4),offset=num(tail,end+16,4);
    if(count==65535 || offset==0xffffffffULL || centralSize==0xffffffffULL || length>=0xffffffffULL)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
    if(num(tail,end+4,2) || num(tail,end+6,2) || count!=num(tail,end+8,2) || offset+centralSize>length-tail.size()+end)fail("world_zip", "存档 ZIP 损坏或不受支持。");
    struct Entry{std::string name;uint64_t size,compressed,crc,offset;int method;};std::vector<Entry> entries;std::set<std::string>names;uint64_t cursor=offset,total=0;
    for(uint64_t i=0;i<count;++i){t.check();auto h=read(cursor,46);if(num(h,0,4)!=0x02014b50)fail("world_zip", "存档 ZIP 损坏或不受支持。");auto n=num(h,28,2);auto name=read(cursor+46,n);auto mode=(num(h,38,4)>>16)&0170000;
        if((num(h,8,2)&1) || (mode && mode!=0100000 && mode!=0040000) || name.empty() || name.find('\0')!=name.npos)fail("world_link", "存档压缩包包含不安全的条目。");
        checked_path(dest,name);for(const auto &part:fs::u8path(name))if(!part.empty())safe_name(part.u8string());std::string portable=name;for(auto &c:portable)c=char(std::tolower((unsigned char)c));if(!names.insert(portable).second)fail("world_zip", "存档压缩包含重复路径。");
        Entry e{name,num(h,24,4),num(h,20,4),num(h,16,4),num(h,42,4),int(num(h,10,2))};
        if(e.size==0xffffffffULL || e.compressed==0xffffffffULL || e.offset==0xffffffffULL)fail("world_zip64", "此存档需要 ZIP64，当前请使用文件夹复制。");
        if(e.method!=0 && e.method!=8)fail("world_zip", "存档 ZIP 损坏或不受支持。");total+=e.size;if(total>maxBytes)fail("world_limit", "存档体积或文件数量超过当前支持范围。");entries.push_back(e);
        cursor+=46+n+num(h,30,2)+num(h,32,2);if(cursor>offset+centralSize)fail("world_zip", "存档 ZIP 损坏或不受支持。");
    }
    room(dest.parent_path(),total);fs::create_directory(dest);uint64_t done=0;
    std::array<char,131072>ib{},ob{};
    for(auto&e:entries){t.check();if(e.name.back()=='/'){fs::create_directories(checked_path(dest,e.name));continue;}
        auto h=read(e.offset,30);if(num(h,0,4)!=0x04034b50 || num(h,8,2)!=uint64_t(e.method) || (num(h,6,2)&1))fail("world_zip", "存档 ZIP 损坏或不受支持。");
        auto start=e.offset+30+num(h,26,2)+num(h,28,2);if(start+e.compressed>offset || read(e.offset+30,num(h,26,2))!=e.name)fail("world_zip", "存档 ZIP 损坏或不受支持。");
        auto target=checked_path(dest,e.name);fs::create_directories(target.parent_path());std::ofstream out(target,std::ios::binary);in.clear();in.seekg(start);uint64_t remain=e.compressed,written=0;uLong crc=crc32(0,nullptr,0);z_stream z{};bool deflated=e.method==8;int status=Z_OK;
        if(deflated && inflateInit2(&z,-MAX_WBITS)!=Z_OK)fail("world_zip", "存档 ZIP 损坏或不受支持。");
        try{while(remain){t.check();auto n=size_t(std::min<uint64_t>(remain,ib.size()));if(!in.read(ib.data(),n))fail("world_read", "存档文件读取失败。");remain-=n;
            if(deflated){z.next_in=(Bytef*)ib.data();z.avail_in=n;do{z.next_out=(Bytef*)ob.data();z.avail_out=ob.size();status=inflate(&z,Z_NO_FLUSH);if(status!=Z_OK && status!=Z_STREAM_END)fail("world_zip", "存档 ZIP 损坏或不受支持。");auto made=ob.size()-z.avail_out;written+=made;if(written>e.size)fail("world_zip", "存档 ZIP 损坏或不受支持。");out.write(ob.data(),made);crc=crc32(crc,(Bytef*)ob.data(),made);if(status==Z_STREAM_END && (remain || z.avail_in))fail("world_zip", "存档 ZIP 损坏或不受支持。");}while(z.avail_in || z.avail_out==0);}
            else{written+=n;if(written>e.size)fail("world_zip", "存档 ZIP 损坏或不受支持。");out.write(ib.data(),n);crc=crc32(crc,(Bytef*)ib.data(),n);}
            if(!out)fail("world_write", "存档文件写入失败。");done+=n;t.report("正在导入存档",done,length);
        }if(deflated)inflateEnd(&z);}catch(...){if(deflated)inflateEnd(&z);throw;}
        out.close();if(!out || written!=e.size || crc!=e.crc || (deflated && status!=Z_STREAM_END))fail("world_crc", "存档 ZIP 校验失败，未导入任何世界。");
    }
}
} // namespace solar::engine
