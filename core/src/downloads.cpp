// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "install_internal.hpp"
#include "download_resume.hpp"
#include "secl_build_config.hpp"
#include <curl/curl.h>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
namespace solar::engine {
static std::string zone() {
    const char *t = std::getenv("TZ");
    if (t && *t)
        return t;
#ifdef _WIN32
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    if (GetDynamicTimeZoneInformation(&info) != TIME_ZONE_ID_INVALID &&
        std::wstring(info.TimeZoneKeyName) == L"China Standard Time")
        return "Asia/Shanghai";
    return "";
#else
    std::error_code ec;
    auto p = fs::read_symlink("/etc/localtime", ec).string();
    auto start = p.find("zoneinfo/");
    return start == p.npos ? "" : p.substr(start + 9);
#endif
}
std::vector<std::string> download_candidates(const std::string &url, const Task &task) {
    std::smatch m;
    if (!std::regex_match(url, m, std::regex(R"(https://([A-Za-z0-9.-]+)(/[^#]*)?)")))
        throw Error("download", "invalid_url", "仅接受有效的 HTTPS 下载地址。");
    auto host = m[1].str(), path = m[2].str();
    std::string prefix;
    bool mapped = false;
    if (host == "piston-meta.mojang.com" || host == "piston-data.mojang.com" ||
        host == "launchermeta.mojang.com" || host == "launcher.mojang.com")
        mapped = true;
    else if (host == "resources.download.minecraft.net") {
        mapped = true;
        prefix = "/assets";
    } else if (host == "libraries.minecraft.net") {
        mapped = true;
        prefix = "/libraries";
    } else if (host == "maven.minecraftforge.net" || host == "maven.fabricmc.net") {
        mapped = true;
        prefix = "/maven";
    } else if (host == "meta.fabricmc.net") {
        mapped = true;
        prefix = "/fabric-meta";
    }
    if (!mapped)
        return {url};
    auto mirror = "https://bmclapi2.bangbang93.com" + prefix + path;
    if (task.source == "official")
        return {url};
    if (task.source == "bmclapi")
        return {mirror};
    bool first = task.source == "auto-mirror-first" ||
                 (task.source != "auto-official-first" &&
                  (task.time_zone.empty() ? zone() : task.time_zone) == "Asia/Shanghai");
    return first ? std::vector<std::string>{mirror, url} : std::vector<std::string>{url, mirror};
}
struct Curl {
    CURL *c = nullptr;
    Curl() {
        static int init = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (init != 0 || (c = curl_easy_init()) == nullptr)
            throw Error("network", "curl_init", "网络初始化失败。");
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS, long(CURLPROTO_HTTPS));
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "SECL/0.5.0");
    }
    ~Curl() { curl_easy_cleanup(c); }
};
struct Headers {
    curl_slist *list = nullptr;
    ~Headers() { curl_slist_free_all(list); }
    void append(const std::string &value) {
        auto *next = curl_slist_append(list, value.c_str());
        if (!next)
            throw Error("network", "curl_init", "无法创建网络请求。");
        list = next;
    }
};

static const char *curseforge_key() {
    const auto *env = std::getenv("SECL_CURSEFORGE_API_KEY");
    return env && *env ? env : secl_embedded_curseforge_key;
}

bool detail::curseforge_configured() { return *curseforge_key() != '\0'; }
bool detail::curseforge_key_embedded() { return *secl_embedded_curseforge_key != '\0'; }

static void authenticate_curseforge(Curl &curl, Headers &headers) {
    const std::string key = curseforge_key();
    if (key.empty())
        throw Error("curseforge", "curseforge_key_missing", "此构建未配置 CurseForge 访问。");
    if (key.size() > 512 || std::any_of(key.begin(), key.end(), [](unsigned char c) {
            return c < 33 || c > 126;
        }))
        throw Error("curseforge", "curseforge_key_invalid", "CurseForge 访问密钥格式无效。");
    headers.append("x-api-key: " + key);
    curl_easy_setopt(curl.c, CURLOPT_HTTPHEADER, headers.list);
    curl_easy_setopt(curl.c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl.c, CURLOPT_PROXY, "");
}

static const Json &curseforge_cdn_hosts() {
    static const auto hosts = Json::array({"edge.forgecdn.net", "mediafilez.forgecdn.net"});
    return hosts;
}

static void perform(Curl &curl, const std::string &url, const Task &task, bool redirect_response = false) {
    task.check();
    curl_easy_setopt(curl.c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl.c, CURLOPT_XFERINFODATA, &task);
    curl_easy_setopt(
        curl.c, CURLOPT_XFERINFOFUNCTION,
        +[](void *ctx, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) -> int {
            auto &t = *static_cast<const Task *>(ctx);
            if (t.cancel && t.cancel->load())
                return 1;
            try {
                if (t.progress)
                    t.progress("下载文件", done, total);
            } catch (...) {
                return 1;
            }
            return 0;
        });
    auto e = curl_easy_perform(curl.c);
    long status = 0;
    curl_easy_getinfo(curl.c, CURLINFO_RESPONSE_CODE, &status);
    task.check();
    if (e != CURLE_OK)
        throw Error("download", "curl_" + std::to_string(e),
                    e == CURLE_OPERATION_TIMEDOUT
                        ? "网络请求超时，请手动重试。"
                        : std::string("下载失败：") + curl_easy_strerror(e),
                    int(status), url);
    if (status >= 300 && status < 400 && redirect_response) return;
    if (status < 200 || status >= 300)
        throw Error("download", "http_" + std::to_string(status),
                    "下载服务返回 HTTP " + std::to_string(status), int(status), url);
}
static std::string read_data(Curl &curl, const std::string &url, const Task &task, size_t limit) {
    struct Buffer {
        std::string bytes;
        size_t limit;
    };
    Buffer buffer{{}, limit};
    curl_easy_setopt(curl.c, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(curl.c, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(
        curl.c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
            auto &v = *static_cast<Buffer *>(ctx);
            auto n = a * b;
            if (n > v.limit || v.bytes.size() > v.limit - n)
                return 0;
            v.bytes.append(p, n);
            return n;
        });
    perform(curl, url, task);
    return buffer.bytes;
}

Json fetch_curseforge_json(const std::string &path, const Task &task, const Json &body) {
    if (path.rfind("/v1/", 0) != 0 || path.find('#') != path.npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) {
            return c <= 32 || c >= 127 || c == '\\';
        }))
        throw Error("curseforge", "invalid_url", "CurseForge 请求地址无效。");
    const auto url = "https://api.curseforge.com" + path;
    constexpr size_t limit = 32 * 1024 * 1024;
    std::string bytes;
    task.check();
    if (task.transport) {
        auto response = task.transport(url, body.is_null() ? "" : body.dump(), "", "application/json");
        task.check();
        if (response.status < 200 || response.status >= 300)
            throw Error("curseforge", "http_" + std::to_string(response.status),
                        "CurseForge API 请求失败。", response.status, url);
        if (response.body.size() > limit)
            throw Error("curseforge", "response_limit", "CurseForge 响应超过大小限制。");
        bytes = std::move(response.body);
    } else {
        Headers headers;
        Curl curl;
        authenticate_curseforge(curl, headers);
        headers.append("Accept: application/json");
        std::string payload;
        if (!body.is_null()) {
            headers.append("Content-Type: application/json");
            payload = body.dump();
            curl_easy_setopt(curl.c, CURLOPT_POSTFIELDS, payload.c_str());
            curl_easy_setopt(curl.c, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(payload.size()));
        }
        curl_easy_setopt(curl.c, CURLOPT_HTTPHEADER, headers.list);
        bytes = read_data(curl, url, task, limit);
    }
    auto result = Json::parse(bytes, nullptr, false);
    if (!result.is_object())
        throw Error("curseforge", "invalid_response", "CurseForge 返回的元数据无法解析。", 0, url);
    return result;
}

std::string fetch_data(const std::string &url, const Task &task, size_t limit) {
    std::optional<Error> last;
    for (auto &candidate : download_candidates(url, task)) {
        task.check();
        try {
            if (task.transport) {
                auto response=task.transport(candidate,"","","");task.check();
                if(response.status<200 || response.status>=300)
                    throw Error("download","http_"+std::to_string(response.status),"下载服务返回 HTTP "+std::to_string(response.status),response.status,candidate);
                if(response.body.size()>limit)throw Error("download","response_limit","下载响应超过大小限制。");
                return response.body;
            }
            Curl c;
            return read_data(c, candidate, task, limit);
        } catch (const Error &e) {
            last = e;
        }
    }
    throw *last;
}
Json fetch_json(const std::string &url, const Task &task) {
    auto j = Json::parse(fetch_data(url, task), nullptr, false);
    if (j.is_discarded())
        throw Error("download", "invalid_json", "服务返回的元数据无法解析。", 0, url);
    return j;
}
Json post_json(const std::string &url, const Json &body, const Task &task) {
    // The same cancellable HTTPS transport as downloads, with one attempt and a
    // bounded response. No OAuth bearer tokens are sent to metadata services.
    download_candidates(url, task);
    if(task.transport) {
        auto response=task.transport(url,body.dump(),"","application/json");task.check();
        if(response.status<200 || response.status>=300)
            throw Error("download","http_"+std::to_string(response.status),"下载服务返回 HTTP "+std::to_string(response.status),response.status,url);
        if(response.body.size()>32*1024*1024)throw Error("download","response_limit","下载响应超过大小限制。");
        auto result=Json::parse(response.body,nullptr,false);
        if(!result.is_object())throw Error("updates","invalid_response","更新服务返回格式无效。",0,url);
        return result;
    }
    Curl c;
    std::string bytes, payload = body.dump();
    auto headers = curl_slist_append(nullptr, "Content-Type: application/json");
    struct Headers { curl_slist *p; ~Headers() { curl_slist_free_all(p); } } guard{headers};
    curl_easy_setopt(c.c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c.c, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(c.c, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(payload.size()));
    curl_easy_setopt(c.c, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(c.c, CURLOPT_WRITEDATA, &bytes);
    curl_easy_setopt(c.c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
        auto &out = *static_cast<std::string *>(ctx); auto count = a * b;
        if (count > 32 * 1024 * 1024 || out.size() > 32 * 1024 * 1024 - count) return 0;
        out.append(p, count); return count;
    });
    perform(c, url, task);
    auto result = Json::parse(bytes, nullptr, false);
    if (!result.is_object()) throw Error("updates", "invalid_response", "更新服务返回格式无效。", 0, url);
    return result;
}
static void restricted_url(const std::string &url,const Json &hosts) {
    if(hosts.is_null())return;
    std::smatch match;
    bool valid=hosts.is_array() && !hosts.empty() && hosts.size()<=32 &&
        std::regex_match(url,match,std::regex(R"(https://([A-Za-z0-9.-]+)(/[^#]*)?)"));
    for(size_t n=0;n<url.size() && valid;++n) {
        const auto c=static_cast<unsigned char>(url[n]);
        if(c<=32 || c>=127 || c=='\\')valid=false;
        if(c=='%') {
            if(n+2>=url.size() || !std::isxdigit(static_cast<unsigned char>(url[n+1])) || !std::isxdigit(static_cast<unsigned char>(url[n+2])))valid=false;
            else n+=2;
        }
    }
    std::string host=valid?match[1].str():"";
    std::transform(host.begin(),host.end(),host.begin(),[](unsigned char c){return char(std::tolower(c));});
    bool allowed=false;
    if(valid)for(const auto &item:hosts)if(item.is_string() && item.get<std::string>()==host)allowed=true;
    if(!allowed)throw Error("modpack","download_host","整合包下载地址或重定向域名不在允许列表中。",0,url);
}
bool curseforge_download_url(const std::string &url) {
    try {
        restricted_url(url, curseforge_cdn_hosts());
        return true;
    } catch (const Error &) {
        return false;
    }
}

static void restrict_download_url(const std::string &url, const Json &hosts, bool curseforge) {
    if (curseforge) {
        if (!curseforge_download_url(url))
            throw Error("curseforge", "download_host", "CurseForge 下载地址不在官方域名列表中。");
    } else {
        restricted_url(url, hosts);
    }
}

void download(const Json &d, const fs::path &target, const Task &task) {
    const auto url = d.value("url", "");
    const bool curseforge = d.value("source", "") == "curseforge";
    const auto hosts = curseforge ? curseforge_cdn_hosts() : d.value("allowed_hosts", Json());
    const auto policy = [&](const std::string &candidate) {
        // Validate every redirect before connecting, including unrestricted
        // third-party HTTPS hosts. API keys never enter file-download handles.
        download_candidates(candidate, task);
        restrict_download_url(candidate, hosts, curseforge);
    };
    policy(url);
    auto exchange = resume::curl_exchange(task);
    if (task.transport) {
        // Legacy buffered seam used by existing metadata/install fixtures.
        // It has no range support and therefore deliberately returns a full
        // 200 body; the streaming seam tests all native resume semantics.
        exchange = [task](const resume::Request &request, const resume::HeaderSink &headers,
                          const resume::BodySink &body) {
            task.check();
            const auto raw = task.transport(request.url, "", "", "");
            task.check();
            resume::Response response{raw.status, {}, raw.location};
            if (raw.status == 200) response.headers["content-length"] = std::to_string(raw.body.size());
            headers(response);
            if (!raw.body.empty()) body(raw.body.data(), raw.body.size());
            return response;
        };
    }
    resume::transfer(d, target, task, download_candidates(url, task), policy, exchange);
}
} // namespace solar::engine
