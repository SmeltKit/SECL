// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "download_resume.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace solar::engine::resume {
namespace {
constexpr std::int64_t maximum = 4LL * 1024 * 1024 * 1024;
constexpr std::int64_t ttl = 7 * 24 * 60 * 60;
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == s.npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
[[noreturn]] void invalid_range(const std::string &url) {
    throw Error("download", "invalid_range", "下载服务器的断点响应无效，请重试或更换下载源。", 0, url);
}
bool header_value(const std::string &s) {
    return s.size() <= 2048 && std::none_of(s.begin(), s.end(), [](unsigned char c) {
        return c < 32 || c == 127;
    });
}
bool strong_etag(const std::string &s) {
    return s.size() >= 2 && s.front() == '"' && s.back() == '"' && header_value(s) &&
        std::none_of(s.begin() + 1, s.end() - 1, [](unsigned char c) { return c == '"'; });
}
std::string get(const HeaderMap &headers, const std::string &key) {
    const auto found = headers.find(key);
    return found == headers.end() ? "" : found->second;
}
std::int64_t decimal(const std::string &s, const std::string &url) {
    if (s.empty() || s.size() > 20) invalid_range(url);
    std::int64_t value = 0;
    for (const unsigned char c : s) {
        if (c < '0' || c > '9' || value > (maximum - (c - '0')) / 10) invalid_range(url);
        value = value * 10 + (c - '0');
    }
    return value;
}
struct Spec {
    std::string algorithm, expected, secondary;
    std::int64_t size = -1;
    explicit Spec(const Json &d) {
        if (!d.is_object()) throw Error("download", "invalid_descriptor", "下载文件信息无效。");
        algorithm = d.contains("sha512") ? "sha512" : d.contains("sha256") ? "sha256" : "sha1";
        if (d.contains(algorithm) && d.at(algorithm).is_string()) expected = d.at(algorithm);
        const auto count = algorithm == "sha512" ? 128 : algorithm == "sha256" ? 64 : 40;
        if (!std::regex_match(expected, std::regex("[a-fA-F0-9]{" + std::to_string(count) + "}")))
            throw Error("download", "hash_required", "文件必须提供有效的 SHA 校验值。");
        expected = lower(expected);
        if (d.contains("sha1")) {
            if (!d.at("sha1").is_string()) throw Error("download", "hash_required", "文件必须提供有效的 SHA 校验值。");
            secondary = lower(d.at("sha1").get<std::string>());
            if (!secondary.empty() && !std::regex_match(secondary, std::regex("[a-f0-9]{40}")))
                throw Error("download", "hash_required", "文件必须提供有效的 SHA 校验值。");
        }
        if (d.contains("size")) {
            const auto &n = d.at("size");
            if (!n.is_number_integer() || (n.is_number_unsigned() && n.get<std::uint64_t>() > maximum))
                throw Error("download", "invalid_size", "下载文件大小无效。");
            size = n.get<std::int64_t>();
        }
        if (size < -1 || size > maximum) throw Error("download", "invalid_size", "下载文件大小无效。");
    }
    Json binding() const { return {{"algorithm", algorithm}, {"hash", expected}, {"sha1", secondary}, {"size", size}}; }
    bool verifies(const fs::path &file, const Task &task) const {
        task.check();
        return fs::is_regular_file(file) && !fs::is_symlink(file) && fs::file_size(file) <= maximum &&
            (size < 0 || fs::file_size(file) == std::uint64_t(size)) &&
            hash_file(file, algorithm, task.cancel) == expected &&
            (secondary.empty() || hash_file(file, "sha1", task.cancel) == secondary);
    }
};
class Lock {
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    OVERLAPPED range_{};
#else
    int file_ = -1;
#endif
public:
    explicit Lock(const fs::path &path) {
#ifdef _WIN32
        file_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw Error("download", "cache_unavailable", "无法使用下载缓存。");
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(file_, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
            CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
            throw Error("download", "cache_unavailable", "无法使用下载缓存。");
        }
        if (!LockFileEx(file_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range_)) {
            const auto error = GetLastError(); CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
            throw Error("download", error == ERROR_LOCK_VIOLATION ? "download_busy" : "cache_unavailable",
                error == ERROR_LOCK_VIOLATION ? "另一个下载任务正在处理同一文件，请稍后重试。" : "无法使用下载缓存。");
        }
#else
        file_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (file_ < 0) throw Error("download", "cache_unavailable", "无法使用下载缓存。");
        struct stat info{};
        if (::fstat(file_, &info) != 0 || !S_ISREG(info.st_mode)) {
            ::close(file_); file_ = -1;
            throw Error("download", "cache_unavailable", "无法使用下载缓存。");
        }
        if (::flock(file_, LOCK_EX | LOCK_NB) != 0) {
            const auto error = errno; ::close(file_); file_ = -1;
            const auto busy = error == EWOULDBLOCK || error == EAGAIN;
            throw Error("download", busy ? "download_busy" : "cache_unavailable",
                busy ? "另一个下载任务正在处理同一文件，请稍后重试。" : "无法使用下载缓存。");
        }
#endif
    }
    ~Lock() {
#ifdef _WIN32
        if (file_ != INVALID_HANDLE_VALUE) { UnlockFileEx(file_, 0, 1, 0, &range_); CloseHandle(file_); }
#else
        if (file_ >= 0) { ::flock(file_, LOCK_UN); ::close(file_); }
#endif
        // Never unlink a lock inode while another process may have opened it.
    }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
};
void root_directory(const fs::path &root) {
    if (fs::is_symlink(root)) throw Error("download", "cache_unavailable", "无法使用下载缓存。");
    try {
        fs::create_directories(root);
#ifndef _WIN32
        fs::permissions(root, fs::perms::owner_all);
#endif
    } catch (const fs::filesystem_error &) {
        throw Error("download", "cache_unavailable", "无法使用下载缓存。");
    }
}
struct Cache {
    fs::path directory, part, metadata;
    Json record;
    const Spec &spec;
    explicit Cache(const fs::path &root, const std::string &key, const Spec &s) : spec(s) {
        directory = checked_path(root, key);
        fs::create_directories(directory);
        part = checked_path(directory, "payload.part"); metadata = checked_path(directory, "metadata.json");
        try {
            if (fs::is_regular_file(metadata) && !fs::is_symlink(metadata)) record = Json::parse(read_file(metadata, 16384));
            if (!record.is_object() || record.value("schema", 0) != 1 || record.value("binding", Json()) != spec.binding() ||
                !record.value("url_key", Json()).is_string() || !record.value("etag", Json()).is_string() ||
                !record.value("last_modified", Json()).is_string() ||
                !record.value("representation_size", Json()).is_number_integer() ||
                record.value("representation_size", -1LL) < -1 || record.value("representation_size", -1LL) > maximum ||
                !fs::is_regular_file(part) ||
                fs::is_symlink(part) || fs::file_size(part) > maximum ||
                (spec.size >= 0 && fs::file_size(part) > std::uint64_t(spec.size))) reset();
        } catch (...) { reset(); }
    }
    std::int64_t offset() const { return fs::is_regular_file(part) ? std::int64_t(fs::file_size(part)) : 0; }
    void reset() {
        std::error_code ignored; fs::remove(part, ignored); fs::remove(metadata, ignored);
        record = {{"schema", 1}, {"binding", spec.binding()}, {"url_key", ""}, {"etag", ""},
                  {"last_modified", ""}, {"representation_size", -1}, {"updated_at", now()}};
    }
    std::string validator(const std::string &url) const {
        if (record.value("url_key", "") != hash_bytes(url, "sha256")) return "";
        const auto etag = record.value("etag", "");
        if (strong_etag(etag)) return etag;
        const auto modified = record.value("last_modified", "");
        return header_value(modified) && curl_getdate(modified.c_str(), nullptr) >= 0 ? modified : "";
    }
    void response(const std::string &url, const Response &r, std::int64_t total) {
        const auto etag = get(r.headers, "etag"), modified = get(r.headers, "last-modified");
        record["url_key"] = hash_bytes(url, "sha256");
        record["etag"] = strong_etag(etag) ? etag : "";
        // RFC 9110 13.1.5 / 8.8.2.2: use a date only without an entity tag
        // and when Date establishes Last-Modified is a strong validator.
        const auto date = curl_getdate(get(r.headers, "date").c_str(), nullptr);
        const auto time = curl_getdate(modified.c_str(), nullptr);
        record["last_modified"] = etag.empty() && header_value(modified) && time >= 0 && date >= time &&
            date - time >= 60 ? modified : "";
        record["representation_size"] = total; record["updated_at"] = now();
        atomic_write(metadata, record.dump());
    }
};
void safe_target(const fs::path &target) {
    if (target.filename().empty() || fs::is_symlink(target) || (fs::exists(target) && !fs::is_regular_file(target)))
        throw Error("download", "invalid_target", "下载目标必须是普通文件。");
}
void commit(const fs::path &part, const fs::path &target, bool executable, const Spec &spec, const Task &task) {
    safe_target(target);
    const auto temporary = target.parent_path() / (".solar-download-" + random_url_token());
    try {
        fs::create_directories(target.parent_path());
        checked_path(target.parent_path(), temporary.filename().u8string());
        fs::copy_file(part, temporary, fs::copy_options::none);
        // Validate the actual publication inode, and honor cancellation after a
        // possibly slow cross-filesystem copy, before replacing the old target.
        if (!spec.verifies(temporary, task))
            throw Error("download", "verification_failed", "下载文件大小或哈希校验失败。");
        if (executable) fs::permissions(temporary, fs::perms::owner_exec | fs::perms::group_exec |
                                       fs::perms::others_exec, fs::perm_options::add);
        task.check(); safe_target(target);
        std::error_code error; fs::rename(temporary, target, error);
#ifdef _WIN32
        if (error && MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) error.clear();
#endif
        if (error) throw Error("download", "commit_failed", "无法提交已验证的下载文件。");
    } catch (const fs::filesystem_error &) {
        std::error_code ignored; fs::remove(temporary, ignored);
        throw Error("download", "commit_failed", "无法提交已验证的下载文件。");
    } catch (...) { std::error_code ignored; fs::remove(temporary, ignored); throw; }
}
Response normalized(Response r) {
    HeaderMap headers;
    for (const auto &h : r.headers) {
        const auto name = lower(h.first);
        if (headers.count(name)) throw Error("download", "invalid_headers", "下载服务器的响应头无效。");
        headers[name] = trim(h.second);
    }
    r.headers = std::move(headers);
    if (r.location.empty()) r.location = get(r.headers, "location");
    return r;
}
void https_url(const std::string &url, bool loopback_test) {
    const auto valid = std::regex_match(url, std::regex(R"(https://[A-Za-z0-9.-]+(?:/[^#]*)?)"));
    const auto local = loopback_test && std::regex_match(url, std::regex(R"(http://127\.0\.0\.1:[0-9]{1,5}/[^#]*)"));
    if ((!valid && !local) || std::any_of(url.begin(), url.end(), [](unsigned char c) { return c <= 32 || c >= 127 || c == '\\'; }))
        throw Error("download", "invalid_url", "仅接受有效的 HTTPS 下载地址。");
}
} // namespace

void prune_cache(const fs::path &root, const std::string &active_key, std::int64_t time, std::uint64_t budget) {
    struct Entry { std::string key; fs::path path; std::uint64_t bytes; std::int64_t used; };
    try {
        root_directory(root);
        const Lock cleanup(checked_path(root, ".maintenance.lock"));
        if (!time) {
            time = now();
            const auto marker = checked_path(root, ".last-cleanup");
            if (fs::is_regular_file(marker)) {
                const auto old = std::stoll(read_file(marker, 64));
                if (old <= time && time - old < 600) return;
            }
            atomic_write(marker, std::to_string(time));
        }
        std::vector<Entry> entries; std::uint64_t total = 0;
        for (const auto &entry : fs::directory_iterator(root)) {
            const auto key = entry.path().filename().u8string();
            if (!std::regex_match(key, std::regex("[a-f0-9]{64}")) || entry.is_symlink() || !entry.is_directory()) continue;
            const auto part = checked_path(entry.path(), "payload.part");
            const auto data = checked_path(entry.path(), "metadata.json");
            std::uint64_t bytes = fs::is_regular_file(part) ? fs::file_size(part) : 0;
            std::int64_t used = 0;
            try { used = Json::parse(read_file(data, 16384)).value("updated_at", 0LL); } catch (...) {}
            if (fs::exists(part)) {
                const auto age = std::chrono::duration_cast<std::chrono::seconds>(fs::file_time_type::clock::now() - fs::last_write_time(part)).count();
                used = std::max(used, now() - std::max(std::int64_t(0), std::int64_t(age)));
            }
            total += bytes; entries.push_back({key, entry.path(), bytes, used});
            if (entries.size() >= 10000) break;
        }
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return a.used < b.used; });
        std::size_t count = entries.size();
        for (const auto &entry : entries) {
            if (entry.key == active_key || (entry.used <= time && time - entry.used <= ttl && total <= budget && count <= 4096)) continue;
            try {
                const Lock lock(checked_path(root, entry.key + ".lock"));
                fs::remove_all(entry.path); total -= entry.bytes; --count;
            } catch (...) {} // Active files and inaccessible cache entries are retained.
        }
    } catch (...) {} // Cache maintenance must never fail an installation.
}

void transfer(const Json &descriptor, const fs::path &target, const Task &task,
              const std::vector<std::string> &candidates, const UrlPolicy &policy, const Exchange &exchange) {
    const Spec spec(descriptor);
    task.check(); safe_target(target);
    if (spec.verifies(target, task)) return;
    if (candidates.empty() || candidates.size() > 8) throw Error("download", "invalid_url", "下载地址无效。");
    const auto root = task.download_cache_root.empty() ? fs::u8path(user_data_root()) / "download-cache-v1"
                                                      : fs::u8path(task.download_cache_root);
    root_directory(root);
    const auto key = hash_bytes(spec.binding().dump(), "sha256");
    prune_cache(root, key);
    const Lock held(checked_path(root, key + ".lock"));
    Cache cache(root, key, spec);
    const auto finish = [&] {
        task.check();
        if (!spec.verifies(cache.part, task)) {
            cache.reset(); throw Error("download", "verification_failed", "下载文件大小或哈希校验失败。");
        }
        task.check(); commit(cache.part, target, descriptor.value("executable", false), spec, task);
        cache.reset();
        std::error_code ignored; fs::remove(cache.directory, ignored);
    };
    const auto cached = cache.offset();
    auto known = cache.record.value("representation_size", -1LL);
    if (cached && ((spec.size >= 0 && cached == spec.size) || (known >= 0 && cached == known))) {
        if (spec.verifies(cache.part, task)) { finish(); return; }
        cache.reset();
    }
    std::optional<Error> last;
    for (const auto &source : candidates) {
        auto url = source; bool restarted = false;
        try {
            for (int redirects = 0;;) {
                task.check(); policy(url);
                const auto offset = cache.offset();
                Request request{url, offset, offset ? cache.validator(url) : ""};
                Response response; bool headers_seen = false, discard = true, poisoned = false;
                std::int64_t position = offset, length = -1, received = 0, total = spec.size;
                std::ofstream output;
                try {
                    const auto returned = exchange(request, [&](const Response &raw) {
                        if (raw.status >= 100 && raw.status < 200) return;
                        if (headers_seen) invalid_range(url);
                        headers_seen = true; response = normalized(raw);
                        if (response.status != 200 && response.status != 206) return;
                        const auto encoding = lower(get(response.headers, "content-encoding"));
                        const auto transfer = lower(get(response.headers, "transfer-encoding"));
                        if ((!encoding.empty() && encoding != "identity") || (!transfer.empty() && transfer != "chunked")) invalid_range(url);
                        const auto content_length = get(response.headers, "content-length");
                        if (response.headers.count("content-length") && content_length.empty()) invalid_range(url);
                        if (!content_length.empty()) length = decimal(content_length, url);
                        if (response.status == 206) {
                            std::smatch range;
                            const auto value = lower(get(response.headers, "content-range"));
                            if (!offset || !std::regex_match(value, range, std::regex(R"(bytes ([0-9]+)-([0-9]+)/([0-9]+))"))) invalid_range(url);
                            const auto start = decimal(range[1], url), end = decimal(range[2], url);
                            total = decimal(range[3], url);
                            if (start != offset || end < start || end >= total || (spec.size >= 0 && total != spec.size) ||
                                (length >= 0 && length != end - start + 1)) invalid_range(url);
                            const auto prior = cache.record.value("representation_size", -1LL);
                            if (prior >= 0 && prior != total) invalid_range(url);
                            length = end - start + 1;
                            if (!request.if_range.empty()) {
                                const auto current = get(response.headers, request.if_range.front() == '"' ? "etag" : "last-modified");
                                if (!current.empty() && current != request.if_range) invalid_range(url);
                            }
                        } else {
                            // A server can ignore Range or an If-Range validator.
                            // A 200 body replaces the cached prefix, never appends.
                            if (length >= 0 && spec.size >= 0 && length != spec.size)
                                throw Error("download", "verification_failed", "下载文件大小或哈希校验失败。", 0, url);
                            position = 0; total = spec.size >= 0 ? spec.size : length;
                        }
                        output.open(cache.part, std::ios::binary | (response.status == 206 ? std::ios::app : std::ios::trunc));
                        if (!output) throw Error("download", "write_failed", "无法创建下载暂存文件。");
                        cache.response(url, response, total);
                        discard = false; task.report("下载文件", position, total);
                    }, [&](const char *bytes, std::size_t size) {
                        task.check();
                        if (!headers_seen) invalid_range(url);
                        if (discard) return;
                        if (size > std::size_t(maximum) || position > maximum - std::int64_t(size) ||
                            (length >= 0 && received > length - std::int64_t(size)) ||
                            (total >= 0 && position > total - std::int64_t(size))) {
                            poisoned = true; invalid_range(url);
                        }
                        output.write(bytes, std::streamsize(size));
                        if (!output) throw Error("download", "write_failed", "无法写入下载暂存文件。");
                        position += std::int64_t(size); received += std::int64_t(size);
                        task.report("下载文件", position, total);
                    });
                    if (!headers_seen || returned.status != response.status) invalid_range(url);
                    if (!returned.location.empty()) response.location = returned.location;
                    if (output.is_open()) { output.close(); if (!output) throw Error("download", "write_failed", "无法写入下载暂存文件。"); }
                } catch (...) {
                    if (output.is_open()) output.close();
                    if (poisoned) cache.reset();
                    throw;
                }
                task.check();
                if (response.status >= 300 && response.status < 400) {
                    if (redirects++ >= 3) throw Error("download", "redirect_limit", "下载重定向次数超过限制。", 0, url);
                    if (response.location.empty()) throw Error("download", "invalid_redirect", "下载重定向缺少有效地址。", response.status, url);
                    policy(response.location); url = response.location; continue;
                }
                if (response.status == 416 && offset) {
                    std::smatch range; const auto value = lower(get(response.headers, "content-range"));
                    const auto complete = std::regex_match(value, range, std::regex(R"(bytes \*/([0-9]+))")) ? decimal(range[1], url) : -1;
                    if (complete == offset && (spec.size < 0 || complete == spec.size) && spec.verifies(cache.part, task)) { finish(); return; }
                    if (!restarted) { cache.reset(); restarted = true; continue; }
                }
                if (response.status != 200 && response.status != 206)
                    throw Error("download", "http_" + std::to_string(response.status),
                                "下载服务返回 HTTP " + std::to_string(response.status), response.status, url);
                if ((length >= 0 && received != length) || (total >= 0 && position != total))
                    throw Error("download", "incomplete_download", "下载尚未完成，已保留断点，请重试。", 0, url);
                finish(); return;
            }
        } catch (const Error &error) { task.check(); if (error.code == "cancelled") throw; last = error; }
    }
    throw *last;
}

Exchange curl_exchange(const Task &task, bool loopback_test) {
    return [task, loopback_test](const Request &request, const HeaderSink &headers, const BodySink &body) {
        https_url(request.url, loopback_test); task.check();
        struct Handle {
            CURL *value = nullptr; curl_slist *headers = nullptr;
            Handle() {
                static const auto init = curl_global_init(CURL_GLOBAL_DEFAULT);
                if (init || !(value = curl_easy_init())) throw Error("network", "curl_init", "网络初始化失败。");
            }
            ~Handle() { curl_slist_free_all(headers); if (value) curl_easy_cleanup(value); }
            void add(const std::string &s) {
                const auto next = curl_slist_append(headers, s.c_str());
                if (!next) throw Error("network", "curl_init", "无法创建网络请求。");
                headers = next;
            }
        } handle;
        struct Transfer {
            const Task &task; const HeaderSink &headers; const BodySink &body;
            Response response; std::exception_ptr error;
            std::size_t header_bytes = 0; bool begun = false, delivered = false;
        } transfer{task, headers, body, {}, {}};
        auto c = handle.value;
        curl_easy_setopt(c, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L); curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L); curl_easy_setopt(c, CURLOPT_MAXREDIRS, 0L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS, long(loopback_test ? CURLPROTO_HTTP | CURLPROTO_HTTPS : CURLPROTO_HTTPS));
        curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, long(CURLPROTO_HTTPS));
        curl_easy_setopt(c, CURLOPT_SUPPRESS_CONNECT_HEADERS, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L); curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 300000L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L); curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
        curl_easy_setopt(c, CURLOPT_USERAGENT, "SECL/0.5.0");
        curl_easy_setopt(c, CURLOPT_HTTP_CONTENT_DECODING, 0L);
        curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
        if (loopback_test) curl_easy_setopt(c, CURLOPT_PROXY, "");
        handle.add("Accept-Encoding: identity");
        if (request.offset) handle.add("Range: bytes=" + std::to_string(request.offset) + "-");
        if (!request.if_range.empty()) {
            if (!request.offset || !header_value(request.if_range)) invalid_range(request.url);
            handle.add("If-Range: " + request.if_range);
        }
        // File CDNs are public downloads. API credentials never enter this
        // handle, including redirects to another host.
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, handle.headers);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &transfer);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
            auto &v = *static_cast<Transfer *>(ctx); const auto n = a * b;
            try {
                v.task.check();
                if (n > 65536 || v.header_bytes > 65536 - n) throw Error("download", "invalid_headers", "下载服务器的响应头无效。");
                v.header_bytes += n; std::string line(p, n);
                if (line.rfind("HTTP/", 0) == 0) {
                    if (v.delivered) throw Error("download", "invalid_headers", "下载服务器的响应头无效。");
                    const auto space = line.find(' ');
                    v.response = {}; v.response.status = space == line.npos ? 0 : std::atoi(line.c_str() + space + 1); v.begun = true;
                } else if (line == "\r\n" || line == "\n") {
                    if (v.begun && v.response.status >= 200 && !v.delivered) { v.headers(v.response); v.delivered = true; }
                    v.begun = false;
                } else if (v.begun) {
                    const auto colon = line.find(':');
                    if (colon == line.npos || colon == 0 ||
                        !std::all_of(line.begin(), line.begin() + std::ptrdiff_t(colon), [](unsigned char ch) {
                            return std::isalnum(ch) || std::string("!#$%&'*+-.^_`|~").find(char(ch)) != std::string::npos;
                        })) throw Error("download", "invalid_headers", "下载服务器的响应头无效。");
                    const auto name = lower(line.substr(0, colon));
                    const auto value = trim(line.substr(colon + 1));
                    if (v.response.headers.count(name) && (name == "content-length" || name == "content-range" || name == "etag" || name == "last-modified" || name == "content-encoding" || name == "location"))
                        throw Error("download", "invalid_headers", "下载服务器的响应头无效。");
                    v.response.headers[name] = value;
                } // Ignore trailers; they cannot retroactively change range bounds.
                return n;
            } catch (...) { v.error = std::current_exception(); return 0; }
        });
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
            auto &v = *static_cast<Transfer *>(ctx);
            try { v.body(p, a * b); return a * b; }
            catch (...) { v.error = std::current_exception(); return 0; }
        });
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L); curl_easy_setopt(c, CURLOPT_XFERINFODATA, &transfer);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, +[](void *ctx, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            const auto &v = *static_cast<Transfer *>(ctx); return v.task.cancel && v.task.cancel->load() ? 1 : 0;
        });
        const auto error = curl_easy_perform(c); task.check();
        if (transfer.error) std::rethrow_exception(transfer.error);
        if (error != CURLE_OK && transfer.response.status < 400)
            throw Error("download", "curl_" + std::to_string(error), error == CURLE_OPERATION_TIMEDOUT ?
                "网络请求超时，请手动重试。" : std::string("下载失败：") + curl_easy_strerror(error), transfer.response.status, request.url);
        char *next = nullptr; curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &next);
        if (next) transfer.response.location = next;
        return transfer.response;
    };
}
} // namespace solar::engine::resume
