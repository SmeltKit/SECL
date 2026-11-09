// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace solar::engine {Json instance_backups_command(const Json &,const Task &);}
using namespace solar;using namespace solar::engine;
static int checks=0;
static void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
static void rejects(const std::function<void()> &f,const std::string &code=""){
    bool caught=false;try{f();}catch(const Error&e){caught=code.empty()||e.code==code;if(!caught)std::cerr<<"Wrong error: "<<e.code<<" "<<e.what()<<"\n";}
    check(caught,"expected rejection");
}
static void clean_stages(const fs::path &parent) {
    for(auto&e:fs::directory_iterator(parent))check(e.path().filename().u8string().rfind(".secl-backup-stage-",0)!=0,"temporary backup directories cleaned");
}
int main(){auto work=fs::temp_directory_path()/("secl-instance-backup-test-"+random_url_token());try {
    auto root=work/"root",game=root/"instances/original",profile=root/"versions/original",store=work/"backups";
    fs::create_directories(game);fs::create_directories(profile);fs::create_directories(store);
    auto base=Json{{"id","1.20.1"},{"mainClass","net.minecraft.client.main.Main"},{"type","release"},
        {"libraries",Json::array()},{"arguments",{{"game",Json::array({"--version","${version_name}"})}}}};
    atomic_write(root/"versions/1.20.1/1.20.1.json",base.dump());
    atomic_write(root/"versions/1.20.1/1.20.1.jar","client fixture");
    base["_solarGameDirectory"]=game.u8string();
    auto metadata=Json{{"id","original"},{"inheritsFrom","1.20.1"},{"_solarGameDirectory",game.u8string()},
        {"_solarOptifine",{{"base",base},{"mode","vanilla"},{"version","HD_U_I6"}}}};
    atomic_write(profile/"original.json",metadata.dump());
    save_instance_settings(profile,{{"java",{{"mode","fixed"},{"path","/old/java/bin/java"}}},
        {"display",{{"name","Original"},{"cover","/old/cover.png"},{"favorite",true}}},
        {"launch",{{"auto_memory",false},{"max_memory_mib",4096}}},{"history",{{"launch_count",3}}}});
    atomic_write(profile/".solar-history.json",R"({"schema":1,"history":{"launch_count":9,"runtime_seconds":120,"last_launch":1794000000}})");
    atomic_write(game/".solar-resource-locks.json",R"({"schema":1,"mod":{"abc":{"version":"1"}}})");
    atomic_write(game/"mods/example.jar",std::string(3*1024*1024,'m'));
    atomic_write(game/"saves/world/level.dat","world fixture");atomic_write(game/"saves/world/session.lock","old lock");
    atomic_write(game/"launcher_accounts.json","do not back up");
    atomic_write(game/"libraries/shared.jar","shared dependency");
    fs::create_directories(game/"empty-folder");
    Json instance={{"id","original"},{"name","Original"},{"root",root.u8string()},{"path",profile.u8string()},
        {"game_directory",game.u8string()},{"version","1.20.1"},{"loader","vanilla"}};
    auto call=[&](const std::string &op,Json extras=Json::object(),const Task&t=Task{}) {
        extras["op"]="instance.backups."+op;if(!extras.contains("instance"))extras["instance"]=instance;
        return instance_backups_command(extras,t);
    };
    auto before=hash_file(game/"saves/world/level.dat");
    auto made=call("create",{{"directory",store.u8string()}});auto backup=fs::u8path(made.at("path").get<std::string>());
    check(backup.extension()==".secl-backup"&&fs::is_regular_file(backup/"manifest.json"),"honest directory backup format");
    auto manifest=Json::parse(read_file(backup/"manifest.json"));
    check(manifest.at("format")=="secl-instance-backup"&&manifest.at("schema")==1,"versioned backup manifest");
    check(fs::exists(backup/"data/profile/originals/original.json")&&fs::exists(backup/"data/profile/originals/1.20.1.json"),"original inheritance chain retained");
    check(fs::exists(backup/"data/profile/history.json")&&fs::exists(backup/"data/game/.solar-resource-locks.json"),"statistics and version locks backed up");
    check(!fs::exists(backup/"data/game/launcher_accounts.json")&&!fs::exists(backup/"data/game/libraries")&&!fs::exists(backup/"data/game/saves/world/session.lock"),"launcher accounts shared dependencies and transient world locks excluded");
    check(fs::is_directory(backup/"data/game/empty-folder"),"empty game folders retained");
    auto listed=call("list",{{"directory",store.u8string()}});check(listed.at("items").size()==1,"backup listed");
    check(made.at("requires_shared_dependencies")==true,"shared dependency requirement explicit");
    auto restored=call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","restored"},{"name","Recovered"}});
    auto target=root/"versions/restored",newgame=target/"game";
    check(restored.at("restored")==true&&fs::is_regular_file(target/"restored.jar"),"new independent profile published");
    check(read_file(target/"restored.jar")=="client fixture","inherited client JAR copied to new id");
    auto next=Json::parse(read_file(target/"restored.json"));
    check(next.at("id")=="restored"&&next.at("jar")=="restored"&&!next.contains("inheritsFrom"),"flattened profile cannot refer to old inheritance id");
    check(next.at("_baseId")=="1.20.1"&&next.at("_solarGameDirectory")==fs::canonical(newgame).u8string(),"game version retained and game path rewritten");
    check(next.at("_solarOptifine").at("base").at("id")=="restored"&&next.at("_solarOptifine").at("base").at("_solarGameDirectory")==fs::canonical(newgame).u8string(),"OptiFine removal base uses new instance");
    check(next.at("_solarOptifine").at("base").at("_baseId")=="1.20.1","removing OptiFine later retains the original Minecraft version");
    auto settings=load_instance_settings(target);
    check(settings.at("java").at("mode")=="auto"&&settings.at("isolated")==true,"machine-local Java reset and isolation forced");
    check(settings.at("display").at("name")=="Recovered"&&settings.at("display").at("cover")==""&&settings.at("display").at("favorite")==true,"name rewritten and nonportable external cover reset");
    check(settings.at("launch").at("max_memory_mib")==4096,"launch settings retained");
    check(read_file(target/".solar-history.json")==read_file(profile/".solar-history.json"),"separate history retained exactly");
    check(read_file(newgame/".solar-resource-locks.json")==read_file(game/".solar-resource-locks.json"),"resource version locks retained exactly");
    check(hash_file(game/"saves/world/level.dat")==before&&hash_file(newgame/"saves/world/level.dat")==before,"source world unchanged and new copy identical");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","restored"}});},"backup_exists");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","original"}});},"backup_new_id");
    fs::create_directory(root/"versions/empty-existing");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","empty-existing"}});},"backup_exists");
    check(fs::is_empty(root/"versions/empty-existing"),"an existing empty directory is never replaced or deleted");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","../escape"}});},"backup_name");
    rejects([&]{call("create",{{"directory",(game/"backups").u8string()}});},"backup_recursion");
    auto shared=instance;shared["game_directory"]=root.u8string();rejects([&]{call("create",{{"instance",shared},{"directory",store.u8string()}});},"backup_shared");
    Task running;running.active_game_directory=game.u8string();rejects([&]{call("create",{{"directory",store.u8string()}},running);},"backup_busy");
    Task cancel;cancel.progress=[&](auto,auto,auto){cancel.cancel->store(true);};
    rejects([&]{call("create",{{"directory",store.u8string()}},cancel);},"cancelled");
    check(call("list",{{"directory",store.u8string()}}).at("items").size()==1,"cancelled backup not published");
    Task changed;bool edited=false;changed.progress=[&](auto,auto,auto){if(!edited){edited=true;atomic_write(game/"changed.txt","change");}};
    rejects([&]{call("create",{{"directory",store.u8string()}},changed);},"backup_changed");fs::remove(game/"changed.txt");
    check(call("list",{{"directory",store.u8string()}}).at("items").size()==1,"changed source not published");
    auto payload=backup/"data/game/saves/world/level.dat";atomic_write(payload,"tampered");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","tampered"}});},"backup_checksum");
    check(!fs::exists(root/"versions/tampered"),"checksum failure publishes nothing");atomic_write(payload,"world fixture");
    atomic_write(backup/"data/game/extra-unverified.txt","extra");
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","extra"}});},"backup_manifest");
    fs::remove(backup/"data/game/extra-unverified.txt");
    auto raw=read_file(backup/"manifest.json");auto bad=manifest;bad["files"][0]["path"]="../outside";atomic_write(backup/"manifest.json",bad.dump());
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","escape"}});});check(!fs::exists(root/"versions/escape"),"unsafe manifest creates no target");
    atomic_write(backup/"manifest.json",raw);
    Task restore_cancel;restore_cancel.progress=[&](auto,auto,auto){restore_cancel.cancel->store(true);};
    rejects([&]{call("restore",{{"backup",backup.u8string()},{"root",root.u8string()},{"id","cancelled"}},restore_cancel);},"cancelled");
    check(!fs::exists(root/"versions/cancelled"),"cancelled restore publishes nothing");
#ifndef _WIN32
    fs::create_symlink(root/"versions/1.20.1/1.20.1.jar",game/"mods/link.jar");
    rejects([&]{call("create",{{"directory",store.u8string()}});},"backup_link");fs::remove(game/"mods/link.jar");
    int ready[2],finish[2];pipe(ready);pipe(finish);auto pid=fork();
    if(pid==0){close(ready[0]);close(finish[1]);int fd=open((game/"saves/world/session.lock").c_str(),O_RDWR);struct flock l{};l.l_type=F_WRLCK;l.l_whence=SEEK_SET;char ok=fcntl(fd,F_SETLK,&l)==0?'y':'n';write(ready[1],&ok,1);read(finish[0],&ok,1);close(fd);_exit(0);}
    close(ready[1]);close(finish[0]);char ok;::read(ready[0],&ok,1);check(ok=='y',"foreign Java-style world lock acquired");
    rejects([&]{call("create",{{"directory",store.u8string()}});},"backup_world_busy");::write(finish[1],"x",1);int status;waitpid(pid,&status,0);close(ready[0]);close(finish[1]);
#endif
    clean_stages(store);clean_stages(root/"versions");
    fs::remove_all(work);std::cout<<checks<<" instance backup checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"Instance backup test failure: "<<e.what()<<"\nFixtures: "<<work<<"\n";return 1;}}
