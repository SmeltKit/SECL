// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#else
#include <stdio.h>
#endif
#endif

namespace solar::engine {
namespace {
constexpr std::uintmax_t max_image_bytes = 64 * 1024 * 1024;
constexpr std::uint64_t max_pixels = 40 * 1024 * 1024;
constexpr size_t scan_limit = 4096;
[[noreturn]] void fail(const char *code, const char *message) { throw Error("screenshots", code, message); }
std::uint32_t be32(const unsigned char *p) { return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3]; }
std::uint32_t le32(const unsigned char *p) { return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
std::string lower(std::string s) { for(auto &c:s)c=char(std::tolower(static_cast<unsigned char>(c))); return s; }
fs::path game_path(const Json &instance) {
    const auto game=fs::u8path(instance.at("game_directory").get<std::string>());
    if(!fs::is_directory(game))fail("missing_directory","游戏目录不存在。");
    return fs::canonical(game);
}
fs::path image_path(const fs::path &game,const std::string &name) {
    if(name.empty() || name.size()>512 || fs::u8path(name).filename().u8string()!=name ||
       name.find_first_of("/\\:\0",0,4)!=name.npos)
        fail("invalid_file","截图文件名无效。");
    const auto path=checked_path(game,"screenshots/"+name);
    if(fs::is_symlink(path) || !fs::is_regular_file(path))fail("missing_file","截图已不存在或不是普通文件，请刷新列表。");
    const auto directory=fs::canonical(checked_path(game,"screenshots"));
    if(fs::canonical(path).parent_path()!=directory)fail("invalid_file","截图不能跳出截图目录。");
    return path;
}
// Replacements with a restored mtime still change inode/file ID or change time.
// Snapshots contain no raw image data and are always checked again before use.
std::string file_stamp(const fs::path &path) {
#ifdef _WIN32
    auto h=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                       nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(h==INVALID_HANDLE_VALUE)fail("unreadable_file","无法读取截图，请刷新列表。");
    struct Handle {HANDLE h;~Handle(){CloseHandle(h);}} guard{h};
    BY_HANDLE_FILE_INFORMATION info{};FILE_BASIC_INFO basic{};
    if(!GetFileInformationByHandle(h,&info) || !GetFileInformationByHandleEx(h,FileBasicInfo,&basic,sizeof(basic)) ||
       (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))
        fail("unreadable_file","无法读取截图，请刷新列表。");
    auto raw=std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string(info.nFileIndexHigh)+":"+
        std::to_string(info.nFileIndexLow)+":"+std::to_string(info.nFileSizeHigh)+":"+
        std::to_string(info.nFileSizeLow)+":"+std::to_string(basic.LastWriteTime.QuadPart)+":"+std::to_string(basic.ChangeTime.QuadPart);
#else
    struct stat s{};
    if(lstat(path.c_str(),&s)!=0 || !S_ISREG(s.st_mode))fail("unreadable_file","无法读取截图，请刷新列表。");
#ifdef __APPLE__
    auto m=s.st_mtimespec,c=s.st_ctimespec;
#else
    auto m=s.st_mtim,c=s.st_ctim;
#endif
    auto raw=std::to_string(s.st_dev)+":"+std::to_string(s.st_ino)+":"+std::to_string(s.st_size)+":"+
        std::to_string(m.tv_sec)+":"+std::to_string(m.tv_nsec)+":"+std::to_string(c.tv_sec)+":"+std::to_string(c.tv_nsec);
#endif
    return hash_bytes(fs::canonical(path).u8string()+"\n"+raw,"sha256");
}
struct Dimensions {std::uint32_t width=0,height=0;std::string format;};
Dimensions dimensions(const fs::path &path) {
    const auto size=fs::file_size(path);
    if(size<24 || size>max_image_bytes)fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    std::ifstream input(path,std::ios::binary);
    auto read=[&](std::uint64_t at,size_t count) {
        if(at>size || count>size-at)fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
        std::string result(count,'\0');input.clear();input.seekg(std::streamoff(at));
        if(!input.read(result.data(),std::streamsize(count)))fail("unreadable_file","无法读取截图，请刷新列表。");
        return result;
    };
    auto head=read(0,std::min<std::uintmax_t>(size,32));auto p=reinterpret_cast<const unsigned char*>(head.data());
    Dimensions out;
    const auto extension=lower(path.extension().u8string());
    if(head.substr(0,8)==std::string("\x89PNG\r\n\x1a\n",8) && extension==".png") {
        const auto header=read(8,25);auto h=reinterpret_cast<const unsigned char*>(header.data());
        if(be32(h)!=13 || header.substr(4,4)!="IHDR" ||
           be32(h+21)!=crc32(0,reinterpret_cast<const Bytef*>(header.data()+4),17))
            fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
        out={be32(h+8),be32(h+12),"PNG"};
        // Reject truncated/disguised files without decompressing large images.
        std::uint64_t at=33;bool data=false,ended=false;size_t chunks=0;
        while(at+12<=size && ++chunks<=10000) {
            auto chunk=read(at,8);auto length=be32(reinterpret_cast<const unsigned char*>(chunk.data()));
            if(length>size-at-12)break;
            auto type=chunk.substr(4,4);
            if(type=="IDAT" && length>0)data=true;
            if(type=="IEND") {ended=length==0 && at+12==size;break;}
            at+=12+length;
        }
        if(!data || !ended)fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    } else if(p[0]==255 && p[1]==216 && (extension==".jpg" || extension==".jpeg")) {
        std::uint64_t at=2;bool frame=false,scan=false;
        while(at+4<=size && at<1024*1024) {
            auto marker=read(at,2);if(static_cast<unsigned char>(marker[0])!=255)break;
            unsigned code=static_cast<unsigned char>(marker[1]);at+=2;
            if(code==255){--at;continue;}
            if(code==0xd9)break;
            if(code==0xd8 || (code>=0xd0 && code<=0xd7) || code==1)continue;
            auto bytes=read(at,2);auto length=(std::uint32_t(static_cast<unsigned char>(bytes[0]))<<8)|static_cast<unsigned char>(bytes[1]);
            if(length<2 || length>size-at)break;
            if((code>=0xc0 && code<=0xc3) || (code>=0xc5 && code<=0xc7) || (code>=0xc9 && code<=0xcb) || (code>=0xcd && code<=0xcf)) {
                if(length<8)break;auto f=read(at+2,5);auto b=reinterpret_cast<const unsigned char*>(f.data());
                if(b[0]!=8)break;
                out={std::uint32_t(b[3])*256+b[4],std::uint32_t(b[1])*256+b[2],"JPEG"};frame=true;
            }
            if(code==0xda){scan=true;break;}
            at+=length;
        }
        if(!frame || !scan || read(size-2,2)!=std::string("\xff\xd9",2))
            fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    } else if(head.substr(0,4)=="RIFF" && head.substr(8,4)=="WEBP" && extension==".webp" && le32(p+4)==size-8) {
        std::uint64_t at=12;bool found=false;
        while(at+8<=size && at<1024*1024) {
            auto h=read(at,8);auto length=le32(reinterpret_cast<const unsigned char*>(h.data()+4));
            if(length>size-at-8)break;
            if(h.substr(0,4)=="VP8L" && length>=5) {
                auto b=read(at+8,5);auto v=reinterpret_cast<const unsigned char*>(b.data());
                if(v[0]!=0x2f)break;auto bits=le32(v+1);
                out={(bits&0x3fff)+1,((bits>>14)&0x3fff)+1,"WebP"};found=true;break;
            }
            if(h.substr(0,4)=="VP8 " && length>=10) {
                auto b=read(at+8,10);auto v=reinterpret_cast<const unsigned char*>(b.data());
                if(b.substr(3,3)!=std::string("\x9d\x01\x2a",3))break;
                out={(std::uint32_t(v[6])|(std::uint32_t(v[7])<<8))&0x3fff,
                     (std::uint32_t(v[8])|(std::uint32_t(v[9])<<8))&0x3fff,"WebP"};found=true;break;
            }
            at+=8+length+(length%2);
        }
        if(!found)fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    } else fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    if(!out.width || !out.height || out.width>16384 || out.height>16384 ||
       std::uint64_t(out.width)*out.height>max_pixels)
        fail("unsupported_image","截图格式无效、文件过大或图片尺寸超过支持范围。");
    return out;
}
Json inspect(const fs::path &game,const std::string &name) {
    auto path=image_path(game,name);auto before=file_stamp(path);auto d=dimensions(path);
    auto time=fs::last_write_time(path);
    const auto modified=std::chrono::duration_cast<std::chrono::seconds>(
        time-fs::file_time_type::clock::now()+std::chrono::system_clock::now().time_since_epoch()).count();
    if(file_stamp(image_path(game,name))!=before)fail("file_changed","截图已被外部修改，请刷新列表后重试。");
    return {{"filename",name},{"path",path.u8string()},{"width",d.width},{"height",d.height},
            {"format",d.format},{"size",fs::file_size(path)},{"modified",modified},{"snapshot",before}};
}
// Same no-replace OS publication policy as modpack_install.cpp. A normal
// same-directory rename also supports removable filesystems without hard links.
void publish_export(const fs::path &stage,const fs::path &target) {
#ifdef _WIN32
    if(MoveFileExW(stage.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH))return;
    const auto error=GetLastError();
    if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS)
        fail("export_exists","导出文件已存在，请选择其他文件名。");
#elif defined(__APPLE__)
    if(renamex_np(stage.c_str(),target.c_str(),RENAME_EXCL)==0)return;
    if(errno==EEXIST || errno==ENOTEMPTY)fail("export_exists","导出文件已存在，请选择其他文件名。");
#else
    if(syscall(SYS_renameat2,AT_FDCWD,stage.c_str(),AT_FDCWD,target.c_str(),1)==0)return;
    if(errno==EEXIST || errno==ENOTEMPTY)fail("export_exists","导出文件已存在，请选择其他文件名。");
#endif
    fail("export_failed","无法导出截图，原图和已有目标文件均未更改。");
}
Json selected(const fs::path &game,const Json &request) {
    const auto &choice=request.at("file");
    auto current=inspect(game,choice.at("filename").get<std::string>());
    if(choice.value("snapshot","").empty() || current.at("snapshot")!=choice.at("snapshot"))
        fail("file_changed","截图已被外部修改，请刷新列表后重试。");
    return current;
}
}
Json screenshots_command(const Json &r,const Task &task) {
    task.check();const auto op=r.at("op").get<std::string>();const auto game=game_path(r.at("instance"));
    auto directory=checked_path(game,"screenshots");
    if(op=="screenshots.list") {
        const auto offset=r.value("offset",0),limit=r.value("limit",36);
        if(offset<0 || offset>int(scan_limit) || limit<1 || limit>96)fail("invalid_page","截图分页参数无效。");
        Json rows=Json::array();size_t visited=0,skipped=0;bool limited=false;
        if(fs::exists(directory)) {
            if(!fs::is_directory(directory))fail("missing_directory","截图目录不是文件夹。");
            for(const auto &entry:fs::directory_iterator(directory)) {
                task.check();if(++visited>scan_limit){limited=true;break;}
                std::error_code ec;auto status=entry.symlink_status(ec);
                if(ec || !fs::is_regular_file(status) || fs::is_symlink(status)){++skipped;continue;}
                try {rows.push_back(inspect(game,entry.path().filename().u8string()));}
                catch(const Error &e){if(e.code=="cancelled")throw;++skipped;}
                catch(const fs::filesystem_error &){++skipped;}
            }
        }
        std::sort(rows.begin(),rows.end(),[](const Json &a,const Json &b){return a["modified"]==b["modified"] ?
            a.at("filename").get<std::string>()<b.at("filename").get<std::string>() : a["modified"]>b["modified"];});
        Json page=Json::array();for(size_t n=size_t(offset);n<rows.size() && page.size()<size_t(limit);++n)page.push_back(rows[n]);
        return {{"items",page},{"total",rows.size()},{"offset",offset},{"limit",limit},
                {"has_more",size_t(offset)+page.size()<rows.size()},{"scan_limited",limited},{"skipped",skipped}};
    }
    if(op=="screenshots.open" && !r.contains("file")) {
        if(!fs::exists(directory))fail("missing_directory","截图目录尚不存在，拍摄截图后可在这里查看。");
        if(!fs::is_directory(directory))fail("missing_directory","截图目录不是文件夹。");
        open_browser(fs::canonical(directory).u8string());return {{"opened",true}};
    }
    auto row=selected(game,r);auto source=fs::u8path(row.at("path").get<std::string>());
    if(op=="screenshots.inspect")return row;
    if(op=="screenshots.open") {task.check();selected(game,r);open_browser(source.u8string());return {{"opened",true}};}
    if(op=="screenshots.remove") {
        if(!r.value("confirmed",false))fail("confirmation_required","请先确认将截图移到系统废纸篓。");
        task.check();selected(game,r);move_to_trash(source);return {{"removed",true}};
    }
    if(op=="screenshots.export") {
        const auto requested=fs::u8path(r.at("path").get<std::string>());
        if(requested.empty() || !requested.is_absolute() || !fs::is_directory(requested.parent_path()))
            fail("export_path","请选择有效的导出位置。");
        auto target=checked_path(requested.parent_path(),requested.filename().u8string());
        if(fs::exists(target))fail("export_exists","导出文件已存在，请选择其他文件名。");
        const auto stage=target.parent_path()/(".secl-screenshot-"+random_url_token());
        bool staged=false;
        try {
            // Copy only the selected source; no directory traversal or re-encoding.
            fs::copy_file(source,stage,fs::copy_options::none);staged=true;task.check();selected(game,r);
            if(fs::file_size(stage)!=row.at("size").get<std::uintmax_t>())fail("file_changed","截图已被外部修改，请刷新列表后重试。");
            task.report("正在导出截图",row.at("size").get<std::int64_t>(),row.at("size").get<std::int64_t>());
            task.check();selected(game,r);
            publish_export(stage,target);staged=false;
        } catch(...) {if(staged){std::error_code ec;fs::remove(stage,ec);}throw;}
        return {{"exported",true},{"path",target.u8string()}};
    }
    fail("unknown_operation","不支持的截图操作。");
}
} // namespace solar::engine
