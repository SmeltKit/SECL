// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <fstream>
#include <iostream>
#include <zlib.h>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
using namespace solar;using namespace solar::engine;
static int checks=0;
static void check(bool good,const char*name){checks++;if(!good)throw std::runtime_error(name);}
static void rejects(const std::function<void()> &f,const std::string &code=""){bool caught=false;try{f();}catch(const Error&e){caught=code.empty() || e.code==code;if(!caught)std::cerr<<"Unexpected error: "<<e.code<<" "<<e.what()<<"\n";}check(caught,"expected failure");}
static void be(std::string&s,uint64_t v,int n){while(n--)s+=char(v>>(n*8));}
static std::string text(const std::string&s){std::string b;be(b,s.size(),2);return b+s;}
static std::string str(const std::string&name,const std::string&value){return std::string(1,8)+text(name)+text(value);}
static std::string integer(int type,const std::string&name,uint64_t value,int n){std::string b(1,char(type));b+=text(name);be(b,value,n);return b;}
static std::string compound(const std::string&name,const std::string&value){return std::string(1,10)+text(name)+value+std::string(1,0);}
static std::string metadata(){return compound("",compound("Data",str("LevelName","测试世界 · Test world")+integer(4,"LastPlayed",1793700000000ULL,8)+integer(3,"GameType",1,4)+integer(1,"hardcore",0,1)+compound("Version",str("Name","1.20.1")+integer(3,"Id",3465,4))));}
static void le(std::string&s,uint64_t v,int n){while(n--){s+=char(v&255);v>>=8;}}
static std::string zip(const std::vector<std::pair<std::string,std::string>>&files){std::string out,central;for(auto &f:files){auto offset=out.size();auto crc=crc32(0,(const Bytef*)f.second.data(),f.second.size());le(out,0x04034b50,4);le(out,20,2);le(out,0,2);le(out,0,2);le(out,0,4);le(out,crc,4);le(out,f.second.size(),4);le(out,f.second.size(),4);le(out,f.first.size(),2);le(out,0,2);out+=f.first+f.second;le(central,0x02014b50,4);le(central,20,2);le(central,20,2);le(central,0,2);le(central,0,2);le(central,0,4);le(central,crc,4);le(central,f.second.size(),4);le(central,f.second.size(),4);le(central,f.first.size(),2);le(central,0,2);le(central,0,2);le(central,0,2);le(central,0,2);le(central,0,4);le(central,offset,4);central+=f.first;}auto offset=out.size();out+=central;le(out,0x06054b50,4);le(out,0,4);le(out,files.size(),2);le(out,files.size(),2);le(out,central.size(),4);le(out,offset,4);le(out,0,2);return out;}
int main(){auto root=fs::temp_directory_path()/("secl-worlds-test-"+random_url_token());try{
 fs::create_directories(root/"game/saves/fixture");fs::create_directories(root/"other");auto game=root/"game",folder=game/"saves/fixture";
 Json instance={{"game_directory",game.u8string()},{"version","1.20.1"}},other={{"game_directory",(root/"other").u8string()},{"version","1.21"}};
 auto call=[&](const std::string&op,Json extra=Json::object(),const Task&t=Task{}){extra["op"]="worlds."+op;extra["instance"]=instance;return dispatch(extra,t);};
 auto data=metadata();auto gz=gzopen((folder/"level.dat").c_str(),"wb");gzwrite(gz,data.data(),data.size());gzclose(gz);
 atomic_write(folder/"region/r.0.0.mca",std::string(3*1024*1024,'x'));atomic_write(folder/"session.lock","lock");
 auto rows=call("list");check(rows.size()==1,"list one world");auto row=rows[0];check(row["name"]=="测试世界 · Test world","gzip NBT world name");check(row["version"]=="1.20.1" && row["mode"]==1 && row["last_played"]==1793700000000ULL,"NBT metadata");check(row["size"].get<uint64_t>()>3*1024*1024,"folder size");
 atomic_write(game/"saves/emoji/level.dat",compound("",compound("Data",str("LevelName",std::string("Emoji ")+"\xED\xA0\xBD\xED\xB8\x80"))));
 auto emoji=call("list");check(std::any_of(emoji.begin(),emoji.end(),[](const Json&r){return r["name"]=="Emoji 😀";}),"Java modified UTF-8 surrogate pairs");fs::remove_all(game/"saves/emoji");
 auto hash=hash_file(folder/"level.dat");
 call("export",{{"folder","fixture"},{"path",(root/"world.zip").u8string()}});auto archive=read_file(root/"world.zip");check(!zip_entry_bytes(archive,"level.dat").empty(),"exported ZIP readable independently");check(zip_entry_bytes(archive,"region/r.0.0.mca",4*1024*1024).size()==3*1024*1024,"streaming compression roundtrip");check(zip_entry_bytes(archive,"session.lock").empty(),"lock file excluded");
 call("import",{{"path",(root/"world.zip").u8string()},{"name","imported"}});check(hash_file(game/"saves/imported/level.dat")==hash,"ZIP import byte identical");
 call("copy",{{"folder","fixture"},{"name","copied"}});check(hash_file(game/"saves/copied/region/r.0.0.mca")==hash_file(folder/"region/r.0.0.mca"),"folder copy identical");
 call("copy",{{"folder","fixture"},{"name","cross"},{"target_instance",other}});check(fs::exists(root/"other/saves/cross/level.dat"),"cross instance copy");
 rejects([&]{call("copy",{{"folder","fixture"},{"name","copied"}});},"world_exists");check(hash_file(game/"saves/copied/level.dat")==hash,"collision leaves original unchanged");
 rejects([&]{call("copy",{{"folder","fixture"},{"name","../outside"}});},"world_name");
 rejects([&]{call("export",{{"folder","fixture"},{"path",(root/"world.zip").u8string()}});},"world_export_exists");check(read_file(root/"world.zip")==archive,"existing export unchanged");
 auto b=call("backup",{{"folder","fixture"}});auto backups=call("backups");check(backups.size()==1 && backups[0]["world"]=="fixture","backup manifest");
 call("restore",{{"archive",b["archive"]},{"name","restored"}});check(hash_file(game/"saves/restored/level.dat")==hash,"backup restored into new copy");
 atomic_write(root/"wrapper/one/world/level.dat",data);call("import",{{"path",(root/"wrapper").u8string()},{"name","wrapped"}});check(call("list").size()==5,"nested folder import");
 atomic_write(root/"wrapped.zip",zip({{"map/world/level.dat",data},{"map/world/data/example.dat","abc"}}));call("import",{{"path",(root/"wrapped.zip").u8string()},{"name","wrapped-zip"}});check(read_file(game/"saves/wrapped-zip/data/example.dat")=="abc","nested ZIP import");
 atomic_write(root/"bad.zip",zip({{"../escape","bad"},{"level.dat",data}}));rejects([&]{call("import",{{"path",(root/"bad.zip").u8string()},{"name","evil"}});});check(!fs::exists(game/"saves/evil") && !fs::exists(game/"escape"),"ZIP path traversal publishes nothing");
 atomic_write(root/"duplicate.zip",zip({{"level.dat",data},{"LEVEL.DAT",data}}));rejects([&]{call("import",{{"path",(root/"duplicate.zip").u8string()},{"name","duplicates"}});},"world_zip");
 auto bad=zip({{"level.dat",data}});bad[30+9]^=1;atomic_write(root/"crc.zip",bad);rejects([&]{call("import",{{"path",(root/"crc.zip").u8string()},{"name","bad-crc"}});},"world_crc");check(!fs::exists(game/"saves/bad-crc"),"CRC failure atomic");
 atomic_write(root/"multi.zip",zip({{"a/level.dat",data},{"b/level.dat",data}}));rejects([&]{call("import",{{"path",(root/"multi.zip").u8string()},{"name","multi"}});},"world_layout");
 Task running;running.active_game_directory=game.u8string();rejects([&]{call("backup",{{"folder","fixture"}},running);},"world_busy");rejects([&]{call("remove",{{"folder","fixture"}},running);},"world_busy");check(!call("list",Json::object(),running).empty(),"read-only list allowed during gameplay");
 running.active_game_directory=(root/"other").u8string();rejects([&]{call("copy",{{"folder","fixture"},{"name","busy"},{"target_instance",other}},running);},"world_busy");
 Task cancel;cancel.progress=[&](auto,auto,auto){cancel.cancel->store(true);};rejects([&]{call("copy",{{"folder","fixture"},{"name","cancelled"}},cancel);},"cancelled");check(!fs::exists(game/"saves/cancelled"),"cancelled copy not published");
 Task changed;bool edited=false;changed.progress=[&](auto,auto,auto){if(!edited){edited=true;atomic_write(folder/"new-file.dat","new");}};rejects([&]{call("copy",{{"folder","fixture"},{"name","changed"}},changed);},"world_changed");fs::remove(folder/"new-file.dat");
 atomic_write(game/"saves/damaged/level.dat","corrupt");auto damaged=call("list");check(std::any_of(damaged.begin(),damaged.end(),[](const Json&r){return r["folder"]=="damaged" && !r["problem"].get<std::string>().empty();}),"corrupt metadata still listed");call("export",{{"folder","damaged"},{"path",(root/"damaged.zip").u8string()}});check(zip_entry_bytes(read_file(root/"damaged.zip"),"level.dat")=="corrupt","damaged world still exportable");
#ifndef _WIN32
 fs::create_symlink(root/"world.zip",folder/"linked");rejects([&]{call("copy",{{"folder","fixture"},{"name","linked"}});},"world_link");fs::remove(folder/"linked");
 int ready[2],finish[2];pipe(ready);pipe(finish);auto pid=fork();if(pid==0){close(ready[0]);close(finish[1]);int fd=open((folder/"session.lock").c_str(),O_RDWR);struct flock l{};l.l_type=F_WRLCK;l.l_whence=SEEK_SET;char ok=fcntl(fd,F_SETLK,&l)==0?'y':'n';write(ready[1],&ok,1);read(finish[0],&ok,1);close(fd);_exit(0);}close(ready[1]);close(finish[0]);char ok;::read(ready[0],&ok,1);check(ok=='y',"external Java-style lock acquired");rejects([&]{call("backup",{{"folder","fixture"}});},"world_busy");::write(finish[1],"x",1);int status;waitpid(pid,&status,0);close(ready[0]);close(finish[1]);
#endif
 check(hash_file(folder/"level.dat")==hash,"source metadata always unchanged");
 for(auto&e:fs::directory_iterator(game))check(e.path().filename().u8string().rfind(".secl-world-stage-",0)!=0,"all stage directories cleaned");
 fs::remove_all(root);std::cout<<checks<<" world checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"World test failure: "<<e.what()<<"\nFixtures: "<<root<<"\n";return 1;}}
