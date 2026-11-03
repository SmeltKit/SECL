// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/api.h"
#include "solar/core.hpp"
#include "solar/engine.hpp"
#include "solar/accounts.hpp"
#include <map>
#include <cstring>
#include <fstream>
#include <mutex>
#include <regex>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
static constexpr Socket bad_socket = INVALID_SOCKET;
static void close_socket(Socket s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
static constexpr Socket bad_socket = -1;
static void close_socket(Socket s) { close(s); }
#endif
namespace solar {
class Core {
    std::mutex commands_, state_mutex_;
    std::thread login_thread_;
    std::atomic_bool cancelled_{false};
    Identity identity_;
    std::map<std::string,Identity> sessions_;
    Json accounts_=Json::array();
    std::string selected_account_;
    bool accounts_readable_=true;
    Json state_ = {{"busy", false},        {"stage", "idle"},           {"error", nullptr},
                   {"account", nullptr},   {"versions", Json::array()}, {"javas", Json::array()},
                   {"game_running", false}};
    std::int64_t game_ = 0;
    std::string root_ = default_game_root(), client_, java_, language_ = "system", source_ = "auto", appearance_="system", tone_="blue";
    void stage(const std::string &s) {
        std::lock_guard<std::mutex> g(state_mutex_);
        state_["stage"] = s;
    }
    void failure(const Error &e) {
        std::lock_guard<std::mutex> g(state_mutex_);
        state_["error"] = e.json();
        state_["busy"] = false;
        state_["stage"] = "error";
    }
    Json installed_javas(const std::string &extra = "", const fs::path &game_root = {}) {
        auto result = engine::java_catalog(Json::object(), {});
        auto extra_runtime=engine::probe_java(fs::u8path(extra.empty()?java_:extra));
        if(!extra_runtime.is_null() && std::none_of(result.begin(),result.end(),[&](const Json &j){return j.at("path")==extra_runtime.at("path");}))result.push_back(extra_runtime);
        auto version_dir=(game_root.empty()?fs::u8path(root_):game_root)/"versions";
        if(fs::is_directory(version_dir))for(auto &entry:fs::directory_iterator(version_dir)) {
            if(!entry.is_directory()||entry.is_symlink())continue;
            try {
                auto choice=engine::load_instance_settings(entry.path()).value("java",Json::object());
                if(choice.value("mode","auto")!="fixed")continue;
                auto path=choice.value("path","");
                if(path.empty()||std::any_of(result.begin(),result.end(),[&](const Json &j){return j.value("path","")==path;}))continue;
                auto runtime=engine::probe_java(fs::u8path(path));
                if(!runtime.is_null()&&std::none_of(result.begin(),result.end(),[&](const Json &j){return j.at("path")==runtime.at("path");}))result.push_back(runtime);
            }catch(const Error&){}
        }
        auto managed = engine::scan_java(
            {fs::u8path(user_data_root()) / "java-managed",
             (game_root.empty() ? fs::u8path(root_) : game_root) / "runtime"},
            false, {}, 12, 50000).at("runtimes");
        for (auto &j : managed) {
            bool seen = false;
            for (auto &known : result)
                if (known.at("path") == j.at("path")) seen = true;
            if (!seen) result.push_back(j);
        }
        return result;
    }
    void save() {
        Json config{{"client_id", client_}, {"game_root", root_}, {"java", java_}, {"language", language_}, {"download_source", source_},{"appearance",appearance_},{"tone",tone_}};
        auto dir = fs::u8path(user_data_root());
        fs::create_directories(dir);
        auto file = dir / "settings.json";
        auto temp = dir / "settings.json.tmp";
        std::ofstream f(temp, std::ios::binary | std::ios::trunc);
        if (!f.write(config.dump(2).data(), std::streamsize(config.dump(2).size())))
            throw Error("settings", "save_failed", "无法保存本地设置。");
        f.close();
        std::error_code ec;
        fs::rename(temp, file, ec);
        if (ec) {
            fs::remove(file, ec);
            ec.clear();
            fs::rename(temp, file, ec);
        }
        if (ec)
            throw Error("settings", "save_failed", "无法保存本地设置。");
    }
    void cancel() {
        cancelled_ = true;
        if (login_thread_.joinable())
            login_thread_.join();
        std::lock_guard<std::mutex> g(state_mutex_);
        state_["busy"] = false;
        if (state_["stage"] != "error")
            state_["stage"] = "idle";
    }
    Json account_view(const Identity &session) const {
        return {{"id","microsoft:"+session.uuid},{"name",session.name},{"uuid",session.uuid},
                {"owned",true},{"skin_url",session.skin_url}};
    }
    void publish_accounts() {
        Json visible=Json::array();
        for(auto record:accounts_) if(record.value("client_id","")==client_) {
            auto it=sessions_.find(record.value("id",""));
            record["needs_login"]=it==sessions_.end() || it->second.expires<=now()+90;
            visible.push_back(record);
        }
        state_["accounts"]=visible; state_["selected_account"]=selected_account_;
    }
    CredentialVault vault(const std::string &client) const {
        return CredentialVault(credential_namespace(fs::u8path(user_data_root()))+"."+client);
    }
    void commit_account(Identity session,bool remember) {
        const auto key="microsoft:"+session.uuid;
        Json previous;
        for(auto &record:accounts_)if(record.value("id","")==key)previous=record;
        Json warning=nullptr;
        bool remembered=false;
        try {
            if(remember) {
                if(!accounts_readable_)throw Error("accounts","invalid_store","账号文件无法读取，本次仅使用当前会话。");
                if(session.refresh_token.empty())throw Error("accounts","refresh_missing","未收到刷新凭据，本次仅使用当前会话。请重新登录并授权记住登录。");
                vault(session.client_id).write(key,session.refresh_token);remembered=true;
            } else if(!previous.is_null()&&previous.value("remembered",false))vault(previous.value("client_id",session.client_id)).remove(key);
        } catch(const Error &error){warning=error.json();}
        Json record={{"id",key},{"type","microsoft"},{"uuid",session.uuid},{"name",session.name},
                     {"client_id",session.client_id},{"skin_url",session.skin_url},{"remembered",remembered},{"needs_login",false}};
        auto next=accounts_;bool found=false;
        for(auto &entry:next)if(entry.value("id","")==key){entry=record;found=true;}
        if(!found)next.push_back(record);
        try {
            if(!accounts_readable_)throw Error("accounts","invalid_store","账号文件无法读取，未覆盖原文件；当前会话仍可使用。");
            save_accounts(fs::u8path(user_data_root()),next,key);
        } catch(const Error &error){warning=error.json();if(remembered){try{vault(session.client_id).remove(key);}catch(...){}for(auto &entry:next)if(entry.value("id","")==key)entry["remembered"]=false;}}
        std::lock_guard<std::mutex> g(state_mutex_);
        accounts_=next;selected_account_=key;sessions_[key]=session;identity_=session;
        state_["account"]=account_view(session);state_["account_warning"]=warning;
        state_["stage"]="ready";state_["busy"]=false;state_["error"]=nullptr;publish_accounts();
    }
    Identity refresh_account(const Json &record) {
        auto client=record.value("client_id","");
        if(client!=client_)throw Error("accounts","wrong_client","此账号属于其他应用身份，请重新登录。");
        auto secret=vault(client).read(record.at("id"));
        auto session=authenticate_refresh(client,secret,[this](const std::string &url,const std::string &body,const std::string &token,const std::string &type){
            if(cancelled_)throw Error("login","cancelled","登录已取消。");
            return https_request(url,body,token,type);
        },[this](std::string step){stage(step);});
        if(session.uuid!=record.at("uuid"))throw Error("accounts","identity_changed","刷新结果与所选账号不一致，请重新登录。");
        return session;
    }
    void select_account(const std::string &id,bool restore=false) {
        cancel();Json record;
        for(auto &entry:accounts_)if(entry.value("id","")==id&&entry.value("client_id","")==client_)record=entry;
        if(record.is_null()){if(restore)return;throw Error("accounts","missing_account","未找到此微软账号。");}
        if(!restore){save_accounts(fs::u8path(user_data_root()),accounts_,id);selected_account_=id;}
        auto existing=sessions_.find(id);
        if(existing!=sessions_.end()&&existing->second.expires>now()+90){
            std::lock_guard<std::mutex> g(state_mutex_);identity_=existing->second;state_["account"]=account_view(identity_);state_["error"]=nullptr;publish_accounts();
            return;
        }
        {std::lock_guard<std::mutex> g(state_mutex_);identity_={};state_["account"]=nullptr;publish_accounts();}
        if(!record.value("remembered",false)) {
            if(!restore)throw Error("accounts","login_required","此账号需要重新登录。");
            return;
        }
        cancelled_=false;{std::lock_guard<std::mutex> g(state_mutex_);state_["busy"]=true;state_["stage"]="microsoft";state_["error"]=nullptr;}
        login_thread_=std::thread([this,record]{try {auto session=refresh_account(record);if(!cancelled_)commit_account(session,true);}catch(const Error &e){if(!cancelled_)failure(e);}catch(...){if(!cancelled_)failure(Error("accounts","refresh_failed","账号刷新失败，请重新登录。"));}});
    }
    void forget_account(const std::string &id,bool erase) {
        cancel();auto next=Json::array();
        for(auto record:accounts_) {
            if(record.value("id","")==id){
                if(record.value("remembered",false))vault(record.at("client_id")).remove(id);
                if(erase)continue;record["remembered"]=false;record["needs_login"]=true;
            }
            next.push_back(record);
        }
        auto chosen=selected_account_==id?std::string():selected_account_;
        if(!accounts_readable_)throw Error("accounts","invalid_store","账号文件无法读取，原文件未修改。");
        save_accounts(fs::u8path(user_data_root()),next,chosen);
        std::lock_guard<std::mutex> g(state_mutex_);accounts_=next;selected_account_=chosen;sessions_.erase(id);
        if(identity_.uuid.empty()||"microsoft:"+identity_.uuid==id){identity_={};state_["account"]=nullptr;}
        state_["error"]=nullptr;publish_accounts();
    }

    void login(const std::string &locale,bool remember=false,const std::string &expected="") {
        if (!std::regex_match(client_,
                              std::regex("[0-9a-fA-F]{8}(-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}")))
            throw Error("configuration", "client_id_missing",
                        "请先配置 SECL 自己注册的微软 Application（client）ID。");
        cancel();
        cancelled_ = false;
        identity_ = {};
        {
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["busy"] = true;
            state_["account"] = nullptr;
            state_["error"] = nullptr;
            state_["stage"] = "browser";
        }
        const auto client = client_;
        login_thread_ = std::thread([this, client, locale, remember, expected] {
            Socket listener = bad_socket;
            try {
#ifdef _WIN32
                static const int winsock = []() {
                    WSADATA d{};
                    return WSAStartup(MAKEWORD(2, 2), &d);
                }();
                if (winsock)
                    throw Error("callback", "socket_init", "无法初始化本机回调网络。");
#endif
                listener = socket(AF_INET, SOCK_STREAM, 0);
                if (listener == bad_socket)
                    throw Error("callback", "socket_failed", "无法创建本机回调端口。");
                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                address.sin_port = 0;
                if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
                    listen(listener, 4) != 0)
                    throw Error("callback", "bind_failed", "无法监听本机登录回调。");
#ifdef _WIN32
                int size = sizeof(address);
#else
                socklen_t size = sizeof(address);
#endif
                if (getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) != 0)
                    throw Error("callback", "port_failed", "无法获取本机回调端口。");
                auto redirect = "http://localhost:" + std::to_string(ntohs(address.sin_port)) +
                                "/oauth/callback";
                auto state = random_url_token(), verifier = random_url_token();
                auto url =
                    "https://login.microsoftonline.com/consumers/oauth2/v2.0/"
                    "authorize?client_id=" +
                    url_encode(client) +
                    "&response_type=code&redirect_uri=" + url_encode(redirect) +
                    "&scope=" + url_encode(remember?"XboxLive.signin offline_access":"XboxLive.signin") +
                    "&response_mode=query&prompt=select_account&state=" + url_encode(state) +
                    "&code_challenge_method=S256&code_challenge=" + sha256_base64url(verifier);
                open_browser(url);
                auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
                std::string code;
                while (!cancelled_ && std::chrono::steady_clock::now() < deadline && code.empty()) {
                    fd_set reads;
                    FD_ZERO(&reads);
                    FD_SET(listener, &reads);
                    timeval tv{0, 150000};
                    if (select(int(listener) + 1, &reads, nullptr, nullptr, &tv) <= 0)
                        continue;
                    auto s = accept(listener, nullptr, nullptr);
                    if (s == bad_socket)
                        continue;
                    std::string incoming;
                    auto receive_end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    while (!cancelled_ && incoming.size() < 16384 &&
                           incoming.find("\r\n\r\n") == incoming.npos &&
                           std::chrono::steady_clock::now() < receive_end) {
                        fd_set ready;
                        FD_ZERO(&ready);
                        FD_SET(s, &ready);
                        timeval t{0, 100000};
                        if (select(int(s) + 1, &ready, nullptr, nullptr, &t) <= 0)
                            continue;
                        char b[2048];
                        auto n = recv(s, b, sizeof(b), 0);
                        if (n <= 0)
                            break;
                        incoming.append(b, size_t(n));
                    }
                    bool ok = false;
                    std::string browser_error;
                    try {
                        if (incoming.size() > 16384)
                            throw Error("callback", "too_large", "回调过大。");
                        code = callback_code(incoming, state);
                        ok = true;
                    } catch (const Error &e) {
                        if (e.code == "authorization_denied")
                            browser_error = e.what();
                    }
                    auto body = std::string("<!doctype html><meta charset=utf-8><title>SECL</title><h2>") +
                                (locale == "en"
                                     ? (ok ? "Authorization result received" : "Authorization incomplete or invalid callback")
                                     : locale == "zh_Hant"
                                           ? (ok ? "已收到授權結果" : "此回呼無效或授權未完成")
                                           : (ok ? "已收到授权结果" : "此回调无效或授权未完成")) +
                                (locale == "en" ? "</h2><p>Return to SECL to view the verification result.</p>"
                                 : locale == "zh_Hant" ? "</h2><p>請返回 SECL 查看驗證結果。</p>"
                                 : "</h2><p>请返回 SECL 查看验证结果。</p>");
                    auto response =
                        std::string(ok ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                        "Content-Type: text/html; charset=utf-8\r\nCache-Control: "
                        "no-store\r\nContent-Security-Policy: default-src "
                        "'none'\r\nConnection: close\r\nContent-Length: " +
                        std::to_string(body.size()) + "\r\n\r\n" + body;
                    send(s, response.data(), int(response.size()), 0);
                    close_socket(s);
                    if (!browser_error.empty())
                        throw Error("callback", "authorization_denied", browser_error);
                }
                close_socket(listener);
                listener = bad_socket;
                if (cancelled_)
                    return;
                if (code.empty())
                    throw Error("callback", "browser_timeout", "等待浏览器授权超时，请重新开始。");
                auto session = authenticate_code(
                    client, redirect, verifier, code,
                    [this](const std::string &url, const std::string &b, const std::string &t,
                           const std::string &ct) {
                        if (cancelled_)
                            throw Error("login", "cancelled", "登录已取消。");
                        return https_request(url, b, t, ct);
                    },
                    [this](std::string s) { stage(s); },remember);
                if (cancelled_)
                    return;
                if(!expected.empty() && expected!="microsoft:"+session.uuid)
                    throw Error("accounts","identity_changed","登录结果与所选账号不一致，请确认微软浏览器账号。");
                commit_account(session,remember);
            } catch (const Error &e) {
                if (listener != bad_socket)
                    close_socket(listener);
                if (!cancelled_)
                    failure(e);
            } catch (...) {
                if (listener != bad_socket)
                    close_socket(listener);
                if (!cancelled_)
                    failure(Error("login", "invalid_service_response",
                                  "认证数据格式异常，登录未完成。"));
            }
        });
    }

  public:
    Core() {
        try {
            auto p = fs::u8path(user_data_root()) / "settings.json";
            if (fs::is_regular_file(p)) {
                auto j = Json::parse(read_file(p, 16384));
                client_ = j.value("client_id", "");
                root_ = j.value("game_root", root_);
                java_ = j.value("java", "");
                appearance_=j.value("appearance","system");tone_=j.value("tone","blue");
                auto source = j.value("download_source", "auto");
                if (source == "auto" || source == "official" || source == "bmclapi") source_ = source;
                auto language = j.value("language", "system");
                if (language == "system" || language == "en" || language == "zh_Hans" || language == "zh_Hant")
                    language_ = language;
            }
        } catch (...) {
            state_["error"] =
                Error("settings", "invalid_settings", "本地设置无法读取，已使用默认值。").json();
        }
        try {auto saved=load_accounts(fs::u8path(user_data_root()));accounts_=saved.at("accounts");selected_account_=saved.value("selected","");}
        catch(const Error &e){accounts_readable_=false;state_["account_warning"]=e.json();}
        publish_accounts();
        state_["root"] = root_;
        state_["client_id"] = client_;
        state_["java"] = java_;
        state_["language"] = language_;
        state_["download_source"] = source_;state_["appearance"]=appearance_;state_["tone"]=tone_;
        state_["java_directory"] = (fs::u8path(user_data_root()) / "java-managed").u8string();
    }
    ~Core() { cancel(); }
    Json command(const Json &r) {
        std::lock_guard<std::mutex> guard(commands_);
        auto action = r.value("action", "poll");
        if (action == "poll") {
            if (game_) {
                auto exit = game_exit(game_);
                if (exit != -1) {
                    game_ = 0;
                    std::lock_guard<std::mutex> g(state_mutex_);
                    state_["game_running"] = false;
                    if (exit != 0)
                        state_["error"] = Error("launch", "game_exit_" + std::to_string(exit),
                                                "游戏进程退出，代码 " + std::to_string(exit) +
                                                    "。可查看游戏目录下的 logs/latest.log。")
                                              .json();
                }
            }
        } else if (action == "scan") {
            auto candidate = r.value("root", root_);
            auto versions = scan_versions(fs::u8path(candidate));
            auto javas = installed_javas(java_, fs::u8path(candidate));
            root_ = candidate;
            save();
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["root"] = root_;
            state_["versions"] = versions;
            state_["javas"] = javas;
            state_["error"] = nullptr;
        } else if (action == "detect_java") {
            auto j = installed_javas(r.value("java", java_));
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["javas"] = j;
            state_["error"] = nullptr;
        } else if (action == "configure") {
            cancel();
            auto next_client = r.value("client_id", client_);
            if (next_client != client_) {
                std::lock_guard<std::mutex> g(state_mutex_);
                identity_ = {};
                state_["account"] = nullptr;
            }
            auto language = r.value("language", language_);
            if (language != "system" && language != "en" && language != "zh_Hans" && language != "zh_Hant")
                throw Error("settings", "invalid_language", "不支持的界面语言。");
            auto source = r.value("download_source", source_);
            if (source != "auto" && source != "official" && source != "bmclapi")
                throw Error("settings", "invalid_source", "不支持的下载源。");
            auto appearance=r.value("appearance",appearance_),tone=r.value("tone",tone_);
            if(appearance!="system"&&appearance!="light"&&appearance!="dark")throw Error("settings","invalid_appearance","主题无效。");
            if(tone!="blue"&&tone!="forest"&&tone!="violet")throw Error("settings","invalid_tone","配色无效。");
            appearance_=appearance;tone_=tone;
            source_ = source;
            language_ = language;
            client_ = next_client;
            java_ = r.value("java", java_);
            auto instance = r.value("version", "");
            Json updated_versions;
            if (!instance.empty()) {
                auto directory = engine::checked_path(fs::u8path(root_), "versions/" + instance);
                load_version(fs::u8path(root_), instance);
                auto runtime = engine::probe_java(fs::u8path(java_));
                if (runtime.is_null()) throw Error("java", "java_unavailable", "所选 Java 无法运行。");
                auto settings = engine::load_instance_settings(directory);
                settings["java"] = {{"mode", "fixed"}, {"path", java_}};
                engine::save_instance_settings(directory, settings);
                updated_versions = scan_versions(fs::u8path(root_));
            }
            save();
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["client_id"] = client_;
            state_["java"] = java_;
            if (!updated_versions.is_null()) state_["versions"] = updated_versions;
            state_["language"] = language_;
            state_["download_source"] = source_;state_["appearance"]=appearance_;state_["tone"]=tone_;
            state_["java_directory"] = (fs::u8path(user_data_root()) / "java-managed").u8string();
            state_["error"] = nullptr; publish_accounts();
        } else if (action == "engine") {
            {
                std::lock_guard<std::mutex> g(state_mutex_);
                if (state_.value("busy", false))
                    throw Error("engine", "busy", "另一个核心操作正在进行。");
            }
            cancel();
            cancelled_ = false;
            auto request = r.at("request");
            {
                std::lock_guard<std::mutex> g(state_mutex_);
                state_["busy"] = true;
                state_["stage"] = "engine";
                state_["error"] = nullptr;
                state_["result"] = nullptr;
                state_["progress"] = Json::object();
            }
            login_thread_ = std::thread([this, request] {
                try {
                    engine::Task task;
                    task.cancel = engine::Cancel(&cancelled_, [](std::atomic_bool *) {});
                    task.source = request.value("source", source_);
                    { std::lock_guard<std::mutex> g(state_mutex_); for(auto &entry:sessions_){task.secrets.push_back(entry.second.token);task.secrets.push_back(entry.second.refresh_token);} }
                    task.time_zone = request.value("time_zone", "");
                    task.progress = [this](const std::string &message, std::int64_t done,
                                           std::int64_t total) {
                        std::lock_guard<std::mutex> g(state_mutex_);
                        state_["progress"] = {
                            {"message", message}, {"done", done}, {"total", total}};
                    };
                    auto result = engine::dispatch(request, task);
                    std::lock_guard<std::mutex> g(state_mutex_);
                    state_["result"] = result;
                    state_["busy"] = false;
                    state_["stage"] = "idle";
                } catch (const Error &e) {
                    if (!cancelled_)
                        failure(e);
                } catch (const std::exception &) {
                    if (!cancelled_)
                        failure(Error("engine", "operation_failed",
                                      "核心操作失败，配置或服务响应无效。"));
                }
            });
        } else if (action == "accounts.restore") {
            select_account(selected_account_,true);
        } else if (action == "accounts.select") {
            select_account(r.at("id"));
        } else if (action == "accounts.remove") {
            forget_account(r.at("id"),true);
        } else if (action == "accounts.signout") {
            forget_account(r.at("id"),false);
        } else if (action == "login")
            login(r.value("locale", language_),r.value("remember",false),r.value("expected_account",""));
        else if (action == "cancel") {
            cancel();
        } else if (action == "logout") {
            if(!identity_.uuid.empty())forget_account("microsoft:"+identity_.uuid,false);
            else {cancel();std::lock_guard<std::mutex> g(state_mutex_);identity_={};state_["account"]=nullptr;state_["error"]=nullptr;}
        } else if (action == "clear_error") {
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["error"] = nullptr;
        } else if (action == "launch") {
            if(identity_.expires<=now()+90 && !selected_account_.empty())
                for(auto &record:accounts_) if(record.value("id","")==selected_account_&&record.value("remembered",false)) {
                    cancelled_=false;auto refreshed=refresh_account(record);commit_account(refreshed,true);break;
                }
            Identity session;
            {
                std::lock_guard<std::mutex> g(state_mutex_);
                session = identity_;
            }
            if (game_)
                throw Error("launch", "already_running", "本次启动的游戏仍在运行。");
            auto chosen = r.value("java", java_);
            auto c = launch_command(fs::u8path(root_), r.value("version", ""), fs::u8path(chosen),
                                    session, platform());
            game_ = spawn_game(c);
            std::lock_guard<std::mutex> g(state_mutex_);
            state_["game_running"] = true;
            state_["error"] = nullptr;
        } else
            throw Error("ffi", "unknown_action", "不支持的核心操作。");
        std::lock_guard<std::mutex> g(state_mutex_);
        return state_;
    }
    void error(const Error &e) { failure(e); }
};
static Core &core() {
    static Core c;
    return c;
}
} // namespace solar
extern "C" unsigned int solar_abi_version(void) { return 1; }
extern "C" void solar_free(char *p) { std::free(p); }
extern "C" char *solar_command(const char *request) {
    using namespace solar;
    std::string result;
    try {
        if (!request || std::strlen(request) > 4 * 1024 * 1024)
            throw Error("ffi", "invalid_request", "核心请求无效。");
        auto r = Json::parse(request);
        if (!r.is_object())
            throw Error("ffi", "invalid_request", "核心请求无效。");
        result = core().command(r).dump();
    } catch (const Error &e) {
        core().error(e);
        result = core().command({{"action", "poll"}}).dump();
    } catch (...) {
        auto e = Error("core", "invalid_data", "本地配置或服务数据格式无效，操作未完成。");
        core().error(e);
        result = core().command({{"action", "poll"}}).dump();
    }
    auto p = static_cast<char *>(std::malloc(result.size() + 1));
    if (!p)
        return nullptr;
    std::memcpy(p, result.c_str(), result.size() + 1);
    return p;
}
