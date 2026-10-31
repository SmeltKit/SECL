// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <curl/curl.h>
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
        curl_easy_setopt(c, CURLOPT_USERAGENT, "SECL/0.1.0");
    }
    ~Curl() { curl_easy_cleanup(c); }
};
static void perform(Curl &curl, const std::string &url, const Task &task) {
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
    if (status < 200 || status >= 300)
        throw Error("download", "http_" + std::to_string(status),
                    "下载服务返回 HTTP " + std::to_string(status), int(status), url);
}
std::string fetch_data(const std::string &url, const Task &task, size_t limit) {
    std::optional<Error> last;
    for (auto &candidate : download_candidates(url, task)) {
        task.check();
        try {
            Curl c;
            curl_easy_setopt(c.c, CURLOPT_TIMEOUT_MS, 30000L);
            struct Buffer {
                std::string bytes;
                size_t limit;
            };
            Buffer buffer{{}, limit};
            curl_easy_setopt(c.c, CURLOPT_WRITEDATA, &buffer);
            curl_easy_setopt(
                c.c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
                    auto &v = *static_cast<Buffer *>(ctx);
                    auto n = a * b;
                    if (n > v.limit || v.bytes.size() > v.limit - n)
                        return 0;
                    v.bytes.append(p, n);
                    return n;
                });
            perform(c, candidate, task);
            return buffer.bytes;
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
void download(const Json &d, const fs::path &target, const Task &task) {
    auto url = d.value("url", "");
    std::string algorithm, expected;
    if (d.contains("sha512")) {
        algorithm = "sha512";
        expected = d.at("sha512").get<std::string>();
    } else if (d.contains("sha256")) {
        algorithm = "sha256";
        expected = d.at("sha256").get<std::string>();
    } else {
        algorithm = "sha1";
        expected = d.value("sha1", "");
    }
    if (!std::regex_match(expected, std::regex(algorithm == "sha512"   ? "[a-fA-F0-9]{128}"
                                               : algorithm == "sha256" ? "[a-fA-F0-9]{64}"
                                                                       : "[a-fA-F0-9]{40}")))
        throw Error("download", "hash_required", "文件必须提供有效的 SHA 校验值。");
    std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
    auto size = d.value("size", -1LL);
    if (size < -1 || size > 4LL * 1024 * 1024 * 1024)
        throw Error("download", "invalid_size", "下载文件大小无效。");
    if (fs::is_regular_file(target) && (size < 0 || fs::file_size(target) == std::uint64_t(size)) &&
        hash_file(target, algorithm, task.cancel) == expected)
        return;
    std::optional<Error> last;
    for (auto &candidate : download_candidates(url, task)) {
        auto temporary = target;
        temporary += ".part-" + random_url_token();
        try {
            task.check();
            fs::create_directories(target.parent_path());
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out)
                throw Error("download", "write_failed", "无法创建下载暂存文件。");
            struct Output {
                std::ofstream *stream;
                std::int64_t size, received = 0;
            };
            Output output{&out, size};
            Curl c;
            curl_easy_setopt(c.c, CURLOPT_TIMEOUT_MS, 300000L);
            curl_easy_setopt(c.c, CURLOPT_WRITEDATA, &output);
            curl_easy_setopt(
                c.c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
                    auto &v = *static_cast<Output *>(ctx);
                    auto n = a * b;
                    if (n > 4ULL * 1024 * 1024 * 1024 ||
                        v.received > 4LL * 1024 * 1024 * 1024 - std::int64_t(n) ||
                        (v.size >= 0 && v.received + std::int64_t(n) > v.size))
                        return 0;
                    v.stream->write(p, std::streamsize(n));
                    if (!*v.stream)
                        return 0;
                    v.received += std::int64_t(n);
                    return n;
                });
            perform(c, candidate, task);
            out.close();
            if ((size >= 0 && output.received != size) ||
                hash_file(temporary, algorithm, task.cancel) != expected)
                throw Error("download", "verification_failed", "下载文件大小或哈希校验失败。", 0,
                            candidate);
            std::error_code ec;
            fs::rename(temporary, target, ec);
#ifdef _WIN32
            if (ec && MoveFileExW(temporary.c_str(), target.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                ec.clear();
#endif
            if (ec)
                throw Error("download", "commit_failed", "无法提交已验证的下载文件。");
            if (d.value("executable", false)) {
                std::error_code pe;
                fs::permissions(
                    target, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, pe);
            }
            return;
        } catch (const Error &e) {
            std::error_code ec;
            fs::remove(temporary, ec);
            last = e;
            task.check();
        }
    }
    throw *last;
}
} // namespace solar::engine
