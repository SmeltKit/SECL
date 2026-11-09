// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include "../src/download_resume.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
using Socket = SOCKET;
static constexpr Socket invalid_socket = INVALID_SOCKET;
static void close_socket(Socket s) { closesocket(s); }
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
static constexpr Socket invalid_socket = -1;
static void close_socket(Socket s) { close(s); }
#endif
using namespace solar;
using namespace solar::engine;
namespace dl = solar::engine::resume;
static int checks = 0;
static void check(bool okay, const char *label) { ++checks; if (!okay) throw std::runtime_error(label); }
static void rejects(const std::function<void()> &action, const std::string &code) {
    try { action(); } catch (const Error &e) { check(e.code == code, ("expected " + code + ", got " + e.code).c_str()); return; }
    throw std::runtime_error("expected rejection " + code);
}
static dl::Response response(int code, dl::HeaderMap headers = {}) { return {code, std::move(headers), {}}; }
static dl::Response send(const dl::Response &r, const std::string &bytes,
                         const dl::HeaderSink &headers, const dl::BodySink &body) {
    headers(r); if (!bytes.empty()) body(bytes.data(), bytes.size()); return r;
}
static dl::Response full(const std::string &bytes, const dl::HeaderSink &headers, const dl::BodySink &body) {
    return send(response(200, {{"Content-Length", std::to_string(bytes.size())}, {"ETag", "\"v1\""}}), bytes, headers, body);
}
static dl::Response remaining(const dl::Request &request, const std::string &bytes,
                              const dl::HeaderSink &headers, const dl::BodySink &body) {
    const auto offset = std::size_t(request.offset);
    return send(response(206, {{"Content-Length", std::to_string(bytes.size() - offset)},
        {"Content-Range", "bytes " + std::to_string(offset) + "-" + std::to_string(bytes.size() - 1) + "/" + std::to_string(bytes.size())},
        {"ETag", "\"v1\""}}), bytes.substr(offset), headers, body);
}
static void https_only(const std::string &url) {
    if (url.rfind("https://example.test/", 0) != 0 && url.rfind("https://mirror.test/", 0) != 0)
        throw Error("test", "download_host", "untrusted test URL");
}
struct Fixture {
    fs::path root, target, cache;
    Task task;
    std::string data = "abcdefghijklmNOPQRSTUVWXYZ0123456789";
    std::string url = "https://example.test/file";
    Json descriptor;
    explicit Fixture(const fs::path &base) {
        root = base / random_url_token(); target = root / "destination/file.jar"; cache = root / "cache";
        fs::create_directories(target.parent_path()); atomic_write(target, "old target remains");
        task.download_cache_root = cache.u8string();
        descriptor = {{"url", url}, {"sha256", hash_bytes(data, "sha256")}, {"size", data.size()}};
    }
    void run(const dl::Exchange &exchange, const std::vector<std::string> &urls = {}) {
        dl::transfer(descriptor, target, task, urls.empty() ? std::vector<std::string>{url} : urls, https_only, exchange);
    }
    fs::path part() const {
        if (fs::exists(cache)) for (const auto &entry : fs::directory_iterator(cache))
            if (entry.is_directory() && fs::is_regular_file(entry.path() / "payload.part")) return entry.path() / "payload.part";
        return {};
    }
    void seed(const dl::HeaderMap &fields = {{"ETag", "\"v1\""}}, std::size_t count = 5) {
        rejects([&] { run([&](const dl::Request &, const dl::HeaderSink &headers, const dl::BodySink &body) -> dl::Response {
            auto h = fields; h["Content-Length"] = std::to_string(data.size());
            headers(response(200, h)); body(data.data(), count);
            throw Error("download", "network_interrupted", "intentional interruption");
        }); }, "network_interrupted");
        check(!part().empty() && read_file(part()) == data.substr(0, count), "valid interrupted prefix retained outside target stage");
        check(read_file(target) == "old target remains", "interruption leaves old target intact");
    }
};
class ExternalLock {
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE; OVERLAPPED range_{};
#else
    int file_ = -1;
#endif
public:
    explicit ExternalLock(const fs::path &file) {
#ifdef _WIN32
        file_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE || !LockFileEx(file_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range_))
            throw std::runtime_error("test lock failed");
#else
        file_ = ::open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (file_ < 0 || ::flock(file_, LOCK_EX | LOCK_NB)) throw std::runtime_error("test lock failed");
#endif
    }
    ~ExternalLock() {
#ifdef _WIN32
        if (file_ != INVALID_HANDLE_VALUE) { UnlockFileEx(file_, 0, 1, 0, &range_); CloseHandle(file_); }
#else
        if (file_ >= 0) { ::flock(file_, LOCK_UN); ::close(file_); }
#endif
    }
};
struct Child {
    std::int64_t handle = 0;
    ~Child() { if (handle) terminate_process(handle); }
    void start(const fs::path &exe, const fs::path &directory, const std::vector<std::string> &arguments) {
        Command c; c.executable = exe; c.directory = directory; c.arguments = arguments; handle = spawn_game(c);
    }
    int wait() {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < end) {
            const auto code = game_exit(handle);
            if (code != -1) { handle = 0; return code; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        throw std::runtime_error("child timed out");
    }
    void kill() { terminate_process(handle); handle = 0; }
};
// A real loopback HTTP server. Only curl_exchange(task, true) may connect to it;
// ordinary production downloads continue to reject HTTP URLs.
class Server {
    Socket listener_ = invalid_socket;
    std::thread worker_;
    std::string data_;
    std::mutex mutex_;
    std::vector<std::string> requests_;
    std::string error_;
    static void write_all(Socket client, const std::string &bytes) {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
#ifdef _WIN32
            const auto n = ::send(client, bytes.data() + sent, int(std::min(std::size_t(32768), bytes.size() - sent)), 0);
#else
            const auto n = ::send(client, bytes.data() + sent, std::min(std::size_t(32768), bytes.size() - sent), 0);
#endif
            if (n <= 0) return;
            sent += std::size_t(n);
        }
    }
public:
    std::string url;
    explicit Server(std::string data) : data_(std::move(data)) {
#ifdef _WIN32
        WSADATA info{}; if (WSAStartup(MAKEWORD(2, 2), &info)) throw std::runtime_error("WSAStartup");
#endif
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ == invalid_socket) throw std::runtime_error("socket");
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || ::listen(listener_, 4)) throw std::runtime_error("bind/listen");
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &length);
        url = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/artifact";
        worker_ = std::thread([&] {
            try {
                for (int index = 0; index < 2; ++index) {
                    const auto client = ::accept(listener_, nullptr, nullptr); if (client == invalid_socket) return;
#ifdef SO_NOSIGPIPE
                    int yes = 1; setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
                    std::string request; char bytes[2048];
                    while (request.find("\r\n\r\n") == request.npos && request.size() < 16384) {
                        const auto n = ::recv(client, bytes, sizeof(bytes), 0); if (n <= 0) break; request.append(bytes, std::size_t(n));
                    }
                    { std::lock_guard<std::mutex> guard(mutex_); requests_.push_back(request); }
                    const auto prefix = std::size_t(128 * 1024);
                    if (!index) {
                        write_all(client, "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(data_.size()) +
                            "\r\nETag: \"network-v1\"\r\nConnection: close\r\n\r\n" + data_.substr(0, prefix));
                    } else {
                        write_all(client, "HTTP/1.1 206 Partial Content\r\nContent-Length: " + std::to_string(data_.size() - prefix) +
                            "\r\nContent-Range: bytes " + std::to_string(prefix) + "-" + std::to_string(data_.size() - 1) + "/" + std::to_string(data_.size()) +
                            "\r\nETag: \"network-v1\"\r\nConnection: close\r\n\r\n" + data_.substr(prefix));
                    }
                    close_socket(client);
                }
            } catch (const std::exception &e) { std::lock_guard<std::mutex> guard(mutex_); error_ = e.what(); }
        });
    }
    ~Server() {
        if (listener_ != invalid_socket) {
#ifdef _WIN32
            ::shutdown(listener_, SD_BOTH);
#else
            ::shutdown(listener_, SHUT_RDWR);
#endif
            close_socket(listener_);
        }
        if (worker_.joinable()) worker_.join();
#ifdef _WIN32
        WSACleanup();
#endif
    }
    std::vector<std::string> requests() { std::lock_guard<std::mutex> guard(mutex_); return requests_; }
};
int main(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--hold-lock") {
        try { ExternalLock lock(fs::u8path(argv[2])); atomic_write(fs::u8path(argv[3]), "ready");
            std::this_thread::sleep_for(std::chrono::seconds(30)); return 0; } catch (...) { return 2; }
    }
    if (argc == 8 && std::string(argv[1]) == "--network-child") {
        const auto expect_failure = std::string(argv[7]) == "fail";
        try {
            Task task; task.download_cache_root = argv[2];
            Json d{{"sha256", argv[5]}, {"size", std::stoll(argv[6])}};
            const std::string url = argv[4];
            dl::transfer(d, fs::u8path(argv[3]), task, {url}, [&](const std::string &next) {
                if (next != url) throw Error("test", "download_host", "loopback redirect forbidden");
            }, dl::curl_exchange(task, true));
            return expect_failure ? 3 : 0;
        } catch (const Error &e) { return expect_failure && e.code.rfind("curl_", 0) == 0 ? 0 : 4; }
        catch (...) { return 5; }
    }
    const auto base = fs::temp_directory_path() / ("secl-download-resume-" + random_url_token());
    try {
        fs::create_directories(base);
        {
            Fixture f(base); f.seed();
            std::int64_t first_done = -1, final_done = -1;
            f.task.progress = [&](const std::string &stage, std::int64_t done, std::int64_t total) {
                check(stage == "下载文件" && total == std::int64_t(f.data.size()), "progress uses whole artifact total");
                if (first_done < 0) first_done = done; final_done = done;
            };
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.offset == 5 && r.if_range == "\"v1\"", "resumes actual prefix with matching strong ETag");
                return remaining(r, f.data, h, b);
            });
            check(first_done == 5 && final_done == std::int64_t(f.data.size()), "progress includes resumed offset");
            check(read_file(f.target) == f.data && f.part().empty(), "verified complete artifact atomically replaces target and clears payload cache");
        }
        {
            Fixture f(base); f.seed();
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.offset == 5, "range requested before ignored-range response"); return full(f.data, h, b);
            });
            check(read_file(f.target) == f.data, "200 ignored Range overwrites prefix instead of appending");
        }
        for (const auto &case_name : {"missing", "start", "total", "length", "encoding", "etag", "multipart"}) {
            Fixture f(base); f.seed(); const auto original = read_file(f.part());
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                dl::HeaderMap fields{{"content-range", "bytes 5-35/36"}, {"content-length", "31"}, {"etag", "\"v1\""}};
                fields["content-range"] = "bytes 5-" + std::to_string(f.data.size() - 1) + "/" + std::to_string(f.data.size());
                fields["content-length"] = std::to_string(f.data.size() - 5);
                const std::string which = case_name;
                if (which == "missing") fields.erase("content-range");
                if (which == "start") fields["content-range"] = "bytes 4-" + std::to_string(f.data.size() - 1) + "/" + std::to_string(f.data.size());
                if (which == "total") fields["content-range"] = "bytes 5-" + std::to_string(f.data.size() - 1) + "/999";
                if (which == "length") fields["content-length"] = "3";
                if (which == "encoding") fields["content-encoding"] = "gzip";
                if (which == "etag") fields["etag"] = "\"changed\"";
                if (which == "multipart") fields["content-range"] = "bytes 5-10/36,12-20/36";
                return send(response(206, fields), "bad body", h, b);
            }); }, "invalid_range");
            check(read_file(f.part()) == original && read_file(f.target) == "old target remains", "invalid 206 cannot append or replace old target");
        }
        for (const auto status : {403, 404, 500, 503}) {
            Fixture f(base); f.seed();
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                return send(response(status), "server error HTML", h, b);
            }); }, "http_" + std::to_string(status));
            check(read_file(f.part()) == f.data.substr(0, 5), "HTTP error response body never contaminates prefix");
        }
        {
            Fixture f(base); f.seed();
            f.task.progress = [&](const std::string &, std::int64_t done, std::int64_t) { if (done >= 10) f.task.cancel->store(true); };
            rejects([&] { f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                const auto n = f.data.size() - std::size_t(r.offset);
                h(response(206, {{"content-range", "bytes 5-" + std::to_string(f.data.size() - 1) + "/" + std::to_string(f.data.size())},
                    {"content-length", std::to_string(n)}})); b(f.data.data() + 5, 5); b(f.data.data() + 10, n - 5); return response(206);
            }); }, "cancelled");
            check(read_file(f.part()) == f.data.substr(0, 10), "cancellation flushes valid partial without deleting it");
            f.task.cancel = std::make_shared<std::atomic_bool>(false); f.task.progress = {};
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { check(r.offset == 10, "retry starts at cancelled offset"); return remaining(r, f.data, h, b); });
        }
        {
            Fixture f(base);
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                return send(response(200), f.data.substr(0, 7), h, b);
            }); }, "incomplete_download");
            check(read_file(f.part()) == f.data.substr(0, 7), "EOF without content length retains incomplete expected-size prefix");
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.offset == 7, "retry resumes incomplete clean EOF response"); return remaining(r, f.data, h, b);
            });
        }
        {
            Fixture f(base); f.seed();
            rejects([&] { f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                remaining(r, f.data, h, b); b("!", 1); return response(206);
            }); }, "invalid_range");
            check(f.part().empty() && read_file(f.target) == "old target remains", "overlong ranged body discards poisoned partial without publishing");
        }
        {
            Fixture f(base); f.descriptor["sha1"] = hash_bytes("a different artifact", "sha1");
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) { return full(f.data, h, b); }); }, "verification_failed");
            check(f.part().empty() && read_file(f.target) == "old target remains", "secondary SHA1 is required alongside stronger primary hash");
        }
        {
            Fixture f(base); f.seed();
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                return send(response(200, {{"content-length", ""}}), f.data, h, b);
            }); }, "invalid_range");
            check(read_file(f.part()) == f.data.substr(0, 5), "empty Content-Length is invalid and cannot truncate valid prefix");
        }
        {
            Fixture f(base); int attempts = 0;
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                ++attempts;
                if (r.url == f.url) return send(response(503), "temporarily unavailable", h, b);
                return full(f.data, h, b);
            }, {f.url, "https://mirror.test/other"});
            check(attempts == 2 && read_file(f.target) == f.data, "source fallback performs one bounded attempt per candidate");
        }
        {
            Fixture f(base); f.seed(); const auto existing = f.part(); atomic_write(existing, "XXXXX");
            rejects([&] { f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { return remaining(r, f.data, h, b); }); }, "verification_failed");
            check(f.part().empty() && read_file(f.target) == "old target remains", "failed final hash discards poisoned partial and protects target");
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { check(r.offset == 0, "corrupt cache retries from zero"); return full(f.data, h, b); });
        }
        {
            Fixture f(base); f.seed();
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.url == "https://mirror.test/other" && r.offset == 5 && r.if_range.empty(), "mirror change reuses content prefix without forwarding origin validator");
                return remaining(r, f.data, h, b);
            }, {"https://mirror.test/other"});
        }
        for (const auto &fields : std::vector<dl::HeaderMap>{
                 {}, {{"ETag", "W/\"weak\""}},
                 {{"ETag", "W/\"weak\""}, {"Last-Modified", "Mon, 03 Nov 2025 10:00:00 GMT"}, {"Date", "Mon, 03 Nov 2025 10:10:00 GMT"}},
                 {{"Last-Modified", "Mon, 03 Nov 2025 10:00:00 GMT"}, {"Date", "Mon, 03 Nov 2025 10:00:10 GMT"}}}) {
            Fixture f(base); f.seed(fields);
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { check(r.if_range.empty(), "missing or weak validators rely only on final full hash"); return remaining(r, f.data, h, b); });
        }
        {
            Fixture f(base); f.seed({{"Last-Modified", "Mon, 03 Nov 2025 10:00:00 GMT"}, {"Date", "Mon, 03 Nov 2025 10:10:00 GMT"}});
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.if_range == "Mon, 03 Nov 2025 10:00:00 GMT", "strong Last-Modified date supplies If-Range without ETag");
                return remaining(r, f.data, h, b);
            });
        }
        {
            Fixture f(base); f.descriptor["size"] = -1;
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) -> dl::Response {
                h(response(200)); b(f.data.data(), f.data.size()); throw Error("download", "network_interrupted", "interrupted after last bytes");
            }); }, "network_interrupted");
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                check(r.offset == std::int64_t(f.data.size()), "unknown-size prefix resumes at on-disk length");
                return send(response(416, {{"Content-Range", "bytes */" + std::to_string(f.data.size())}}), "error body", h, b);
            });
            check(read_file(f.target) == f.data, "416 accepts complete local content only after full hash verification");
        }
        {
            Fixture f(base); f.seed(); int attempts = 0;
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) {
                ++attempts;
                if (attempts == 1) return send(response(416, {{"content-range", "bytes */999"}}), "bad", h, b);
                check(r.offset == 0 && r.if_range.empty(), "416 stale prefix restarts cleanly"); return full(f.data, h, b);
            });
            check(attempts == 2, "416 recovery performs only one bounded fresh attempt");
        }
        {
            Fixture f(base); f.seed(); int attempts = 0;
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                ++attempts; return send(response(416), "error", h, b);
            }); }, "http_416");
            check(attempts == 2, "permanent 416 does not retry forever");
        }
        {
            Fixture f(base); int requests = 0;
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                ++requests; return send(response(302, {{"location", "https://untrusted.test/file"}}), "redirect page", h, b);
            }); }, "download_host");
            check(requests == 1 && f.part().empty(), "redirect policy rejects destination before connection and does not cache redirect body");
        }
        {
            Fixture f(base); int requests = 0;
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) {
                ++requests; return send(response(302, {{"location", f.url}}), "", h, b);
            }); }, "redirect_limit");
            check(requests == 4, "redirect chain has original request plus at most three hops");
        }
        {
            Fixture f(base); f.seed(); auto changed = f.descriptor; changed["size"] = f.data.size() + 1;
            bool new_binding = false;
            rejects([&] { dl::transfer(changed, f.target, f.task, {f.url}, https_only,
                [&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { new_binding = r.offset == 0; return full(f.data, h, b); }); }, "verification_failed");
            check(new_binding && read_file(f.part()) == f.data.substr(0, 5), "different expected size cannot reuse another cache binding");
        }
        {
            Fixture f(base); f.seed(); const auto lock_file = f.cache / (f.part().parent_path().filename().u8string() + ".lock");
            const auto ready = f.root / "ready"; Child child;
            child.start(fs::absolute(fs::u8path(argv[0])), base, {"--hold-lock", lock_file.u8string(), ready.u8string()});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (!fs::exists(ready) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            check(fs::exists(ready), "separate process owns cache artifact lock");
            bool connected = false; const auto begin = std::chrono::steady_clock::now();
            rejects([&] { f.run([&](const dl::Request &, const dl::HeaderSink &, const dl::BodySink &) { connected = true; return response(500); }); }, "download_busy");
            check(!connected && std::chrono::steady_clock::now() - begin < std::chrono::milliseconds(500), "concurrent artifact request fails nonblocking before touching partial");
            child.kill();
            f.run([&](const dl::Request &r, const dl::HeaderSink &h, const dl::BodySink &b) { check(r.offset == 5, "process crash releases OS lock without losing partial"); return remaining(r, f.data, h, b); });
        }
        {
            Fixture f(base);
#ifndef _WIN32
            fs::permissions(f.target.parent_path(), fs::perms::owner_read | fs::perms::owner_exec);
#else
            SetFileAttributesW(f.target.c_str(), FILE_ATTRIBUTE_READONLY);
#endif
            bool failed = false;
            try { f.run([&](const dl::Request &, const dl::HeaderSink &h, const dl::BodySink &b) { return full(f.data, h, b); }); } catch (...) { failed = true; }
#ifndef _WIN32
            fs::permissions(f.target.parent_path(), fs::perms::owner_all);
#else
            SetFileAttributesW(f.target.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
            check(failed && read_file(f.target) == "old target remains" && read_file(f.part()) == f.data,
                  "failed target commit keeps both old target and verified cache for retry");
            f.run([&](const dl::Request &, const dl::HeaderSink &, const dl::BodySink &) -> dl::Response { throw std::runtime_error("must not refetch verified complete cache"); });
            check(read_file(f.target) == f.data, "verified complete cache repairs failed commit without network");
        }
        {
            const auto root = base / "maintenance"; fs::create_directories(root);
            const auto a = std::string(64, 'a'), b = std::string(64, 'b'), c = std::string(64, 'c');
            for (const auto &key : {a, b, c}) { fs::create_directories(root / key); atomic_write(root / key / "payload.part", "0123456789"); atomic_write(root / key / "metadata.json", Json{{"updated_at", now()}}.dump()); }
            ExternalLock lock(root / (b + ".lock"));
            dl::prune_cache(root, c, now() + 8 * 24 * 60 * 60);
            check(!fs::exists(root / a) && fs::exists(root / b) && fs::exists(root / c), "TTL cleanup skips OS-locked and active cache entries");
            dl::prune_cache(root, {}, now(), 0);
            check(fs::exists(root / b) && !fs::exists(root / c), "size-budget cleanup removes idle data but not another process's partial");
        }
        {
            Fixture f(base); const std::string data(512 * 1024, 'z'); Server server(data);
            const auto sha = hash_bytes(data, "sha256"); const auto executable = fs::absolute(fs::u8path(argv[0]));
            Child first; first.start(executable, base, {"--network-child", f.cache.u8string(), f.target.u8string(), server.url, sha, std::to_string(data.size()), "fail"});
            check(first.wait() == 0 && !f.part().empty() && fs::file_size(f.part()) == 128 * 1024,
                  "real HTTP connection truncation retains exactly the received prefix across process exit");
            check(read_file(f.target) == "old target remains", "network interruption does not publish partial game file");
            Child second; second.start(executable, base, {"--network-child", f.cache.u8string(), f.target.u8string(), server.url, sha, std::to_string(data.size()), "success"});
            check(second.wait() == 0 && read_file(f.target) == data, "new process resumes real HTTP Range and verifies full payload");
            const auto requests = server.requests();
            check(requests.size() == 2 && requests[0].find("Range:") == std::string::npos &&
                  requests[1].find("Range: bytes=131072-") != std::string::npos &&
                  requests[1].find("If-Range: \"network-v1\"") != std::string::npos,
                  "real curl sends persisted offset and strong validator on restart");
            check(requests[0].find("x-api-key") == std::string::npos && requests[1].find("x-api-key") == std::string::npos,
                  "native file transport never sends CurseForge API credentials");
            rejects([&] { auto io = dl::curl_exchange(f.task); io({server.url, 0, {}}, [](const dl::Response &) {}, [](const char *, std::size_t) {}); }, "invalid_url");
        }
        fs::remove_all(base);
        std::cout << checks << " download-resume checks passed\n"; return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        std::error_code ignored; fs::remove_all(base, ignored); return 1;
    }
}
