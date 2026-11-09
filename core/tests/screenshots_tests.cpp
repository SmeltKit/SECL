// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <fstream>
#include <zlib.h>
using namespace solar;
using namespace solar::engine;
static int checks=0;
static void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
static void rejects(const std::function<void()> &work,const std::string &code={}) {
    bool failed=false;try{work();}catch(const Error&e){failed=code.empty()||e.code==code;}
    check(failed,("expected rejection: "+code).c_str());
}
static void big(std::string&s,std::uint32_t n){for(int shift=24;shift>=0;shift-=8)s+=char(n>>shift);}
static void chunk(std::string&s,const std::string&type,const std::string&bytes){
    big(s,std::uint32_t(bytes.size()));auto data=type+bytes;s+=data;
    big(s,crc32(0,reinterpret_cast<const Bytef*>(data.data()),unsigned(data.size())));
}
static std::string png(unsigned w=8,unsigned h=6){
    std::string out("\x89PNG\r\n\x1a\n",8),header;big(header,w);big(header,h);header+=std::string("\x08\x02\0\0\0",5);
    chunk(out,"IHDR",header);std::string pixels;
    for(unsigned y=0;y<h;++y){pixels+='\0';for(unsigned x=0;x<w;++x)pixels+=std::string("\x24\x77\xb0",3);}
    uLongf length=compressBound(pixels.size());std::string compressed(length,'\0');
    check(compress2(reinterpret_cast<Bytef*>(compressed.data()),&length,reinterpret_cast<const Bytef*>(pixels.data()),pixels.size(),6)==Z_OK,"fixture compresses");
    compressed.resize(length);chunk(out,"IDAT",compressed);chunk(out,"IEND","");return out;
}
static std::string unhex(const std::string&s){std::string out;for(size_t n=0;n<s.size();n+=2)out+=char(std::stoul(s.substr(n,2),nullptr,16));return out;}
int main(){
    const auto temp=fs::temp_directory_path()/("secl-screenshots-tests-"+random_url_token());
    try{
        const auto game=temp/"game",shots=game/"screenshots";fs::create_directories(game);
        Json instance{{"game_directory",game.u8string()}},request{{"op","screenshots.list"},{"instance",instance}};
        auto empty=screenshots_command(request);check(empty["items"].empty() && empty["total"]==0,"missing screenshot directory is empty");
        check(!fs::exists(shots),"listing never creates directories");
        const auto bytes=png();atomic_write(shots/"older.png",bytes);atomic_write(shots/"newer.PNG",bytes);
        atomic_write(shots/"photo.jpg",unhex("ffd8ffe000104a46494600010100000100010000ffdb004300080606070605080707070909080a0c140d0c0b0b0c1912130f141d1a1f1e1d1a1c1c20242e2720222c231c1c2837292c30313434341f27393d38323c2e333432ffdb0043010909090c0b0c180d0d1832211c213232323232323232323232323232323232323232323232323232323232323232323232323232323232323232323232323232ffc00011080005000703012200021101031101ffc4001f0000010501010101010100000000000000000102030405060708090a0bffc400b5100002010303020403050504040000017d01020300041105122131410613516107227114328191a1082342b1c11552d1f02433627282090a161718191a25262728292a3435363738393a434445464748494a535455565758595a636465666768696a737475767778797a838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae1e2e3e4e5e6e7e8e9eaf1f2f3f4f5f6f7f8f9faffc4001f0100030101010101010101010000000000000102030405060708090a0bffc400b51100020102040403040705040400010277000102031104052131061241510761711322328108144291a1b1c109233352f0156272d10a162434e125f11718191a262728292a35363738393a434445464748494a535455565758595a636465666768696a737475767778797a82838485868788898a92939495969798999aa2a3a4a5a6a7a8a9aab2b3b4b5b6b7b8b9bac2c3c4c5c6c7c8c9cad2d3d4d5d6d7d8d9dae2e3e4e5e6e7e8e9eaf2f3f4f5f6f7f8f9faffda000c03010002110311003f00e0a8a28af6cf08ffd9"));
        atomic_write(shots/"lossless.webp",unhex("524946463800000057454250565038202c000000d001009d012a0700050001402625a00274ba01f80003b000fef6135ffe07e5d3863c3ff8b39188ecbc000000"));
        atomic_write(shots/"fake.png","not a screenshot");atomic_write(shots/"text.txt",bytes);
        fs::create_directories(shots/"nested");atomic_write(shots/"nested/hidden.png",bytes);
        fs::last_write_time(shots/"older.png",fs::file_time_type::clock::now()-std::chrono::hours(4));
        fs::last_write_time(shots/"newer.PNG",fs::file_time_type::clock::now()+std::chrono::hours(4));
        auto page=screenshots_command(request);
        check(page["total"]==4 && page["items"].size()==4,"only controlled real image formats are listed, without recursion");
        check(page["items"][0]["filename"]=="newer.PNG","newest file appears first");
        check(page["items"][0]["width"]==8 && page["items"][0]["height"]==6,"PNG dimensions decoded");
        check(page["items"][0]["size"]==bytes.size(),"file size is exact");
        for(auto&row:page["items"]){check(!row.contains("base64") && !row.contains("bytes"),"API never returns full image payload");
            if(row["format"]=="JPEG" || row["format"]=="WebP")check(row["width"]==7 && row["height"]==5,"JPEG and WebP dimensions decoded");}
        auto paged=request;paged["limit"]=2;
        auto first=screenshots_command(paged);paged["offset"]=2;auto second=screenshots_command(paged);
        check(first["items"].size()==2 && first["has_more"]==true && second["items"].size()==2 && second["has_more"]==false,"pagination has no overlap");
        check(first["items"][1]["filename"]!=second["items"][0]["filename"],"page boundary does not repeat items");
        auto invalid=request;invalid["limit"]=1000;rejects([&]{screenshots_command(invalid);},"invalid_page");
        auto row=page["items"][0];Json operation{{"instance",instance},{"file",row},{"op","screenshots.inspect"}};
        check(screenshots_command(operation)["snapshot"]==row["snapshot"],"preview revalidates the selected snapshot");
        operation["op"]="screenshots.export";operation["path"]=(temp/"export.png").u8string();screenshots_command(operation);
        check(read_file(temp/"export.png")==bytes,"export preserves image bytes exactly");
        rejects([&]{screenshots_command(operation);},"export_exists");check(read_file(temp/"export.png")==bytes,"existing destination is never overwritten");
        operation["path"]=(temp/"racing-output.png").u8string();
        Task racing;racing.progress=[&](const std::string&,std::int64_t,std::int64_t){atomic_write(temp/"racing-output.png","keep existing bytes");};
        rejects([&]{screenshots_command(operation,racing);},"export_exists");
        check(read_file(temp/"racing-output.png")=="keep existing bytes","publication race cannot overwrite a newly created target");
        check(read_file(shots/"newer.PNG")==bytes,"failed publication preserves source bytes");
        for(const auto&e:fs::directory_iterator(temp))check(e.path().filename().u8string().rfind(".secl-screenshot-",0)!=0,"failed publication cleans staging");
        operation["path"]=(temp/"cancelled-output.png").u8string();
        Task cancel_export;cancel_export.progress=[&](const std::string&,std::int64_t,std::int64_t){cancel_export.cancel->store(true);};
        rejects([&]{screenshots_command(operation,cancel_export);},"cancelled");
        check(!fs::exists(temp/"cancelled-output.png"),"late cancellation never publishes a screenshot export");
        operation["op"]="screenshots.remove";rejects([&]{screenshots_command(operation);},"confirmation_required");
        check(fs::exists(shots/"newer.PNG"),"unconfirmed removal preserves source");
        const auto old_time=fs::last_write_time(shots/"newer.PNG");
        atomic_write(shots/"replacement.png",bytes);fs::rename(shots/"replacement.png",shots/"newer.PNG");fs::last_write_time(shots/"newer.PNG",old_time);
        operation["confirmed"]=true;rejects([&]{screenshots_command(operation);},"file_changed");
        check(fs::exists(shots/"newer.PNG"),"same-size same-mtime replacement is not accidentally trashed");
        operation["op"]="screenshots.export";operation["path"]=(temp/"stale.png").u8string();rejects([&]{screenshots_command(operation);},"file_changed");
        check(!fs::exists(temp/"stale.png"),"stale source is not exported");
        operation["file"]={{"filename","../export.png"},{"snapshot","x"}};rejects([&]{screenshots_command(operation);});
        std::error_code ec;fs::create_symlink(temp/"export.png",shots/"linked.png",ec);
        if(!ec){operation["file"]={{"filename","linked.png"},{"snapshot","x"}};rejects([&]{screenshots_command(operation);});
            check(screenshots_command(request)["total"]==4,"symbolic links do not enter the gallery");}
        auto cancelled=Task{};cancelled.cancel->store(true);rejects([&]{screenshots_command(request,cancelled);},"cancelled");
        fs::remove_all(shots);fs::create_directories(temp/"outside");fs::create_directory_symlink(temp/"outside",shots,ec);
        if(!ec)rejects([&]{screenshots_command(request);});
        fs::remove(shots,ec);fs::create_directories(shots);
        for(size_t n=0;n<4100;++n){std::ofstream f(shots/(std::to_string(n)+".txt"));f<<"x";}
        check(screenshots_command(request)["scan_limited"]==true,"directory work is bounded");
        fs::remove_all(temp);std::cout<<checks<<" screenshot checks passed\n";return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';std::error_code ec;fs::remove_all(temp,ec);return 1;}
}
