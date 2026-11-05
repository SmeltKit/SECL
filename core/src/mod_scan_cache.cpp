// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "mod_scan_cache.hpp"
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace solar::engine {
namespace {
struct Parsed { Json value; size_t bytes; };
struct File { std::string stamp, content; };
std::mutex mutex;
std::map<std::string, Parsed> parsed;
std::map<std::string, File> files;
size_t cached_bytes = 0;
std::uint64_t reads = 0, parses = 0, fast_hits = 0, content_hits = 0;
constexpr size_t budget = 32 * 1024 * 1024;

// Size+mtime alone is not enough: replacing a jar while restoring its mtime
// must invalidate browsing data. Use file identity and change time as well.
// When a platform cannot provide a reliable stamp, fall back to reading bytes.
std::string stamp(const fs::path &path) {
    if (fs::is_symlink(path) || !fs::is_regular_file(path))
        throw Error("mods", "invalid_file", "模组文件不存在或不是普通文件。");
#ifdef _WIN32
    auto h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    struct Handle { HANDLE h; ~Handle() { CloseHandle(h); } } guard{h};
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{};
    if (!GetFileInformationByHandle(h, &info) ||
        !GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        basic.ChangeTime.QuadPart == 0) return {};
    return std::to_string(info.dwVolumeSerialNumber) + ":" + std::to_string(info.nFileIndexHigh) + ":" +
        std::to_string(info.nFileIndexLow) + ":" + std::to_string(info.nFileSizeHigh) + ":" +
        std::to_string(info.nFileSizeLow) + ":" + std::to_string(basic.LastWriteTime.QuadPart) + ":" +
        std::to_string(basic.ChangeTime.QuadPart);
#else
    struct stat s{};
    if (lstat(path.c_str(), &s) != 0 || !S_ISREG(s.st_mode)) return {};
#ifdef __APPLE__
    auto m = s.st_mtimespec, c = s.st_ctimespec;
#else
    auto m = s.st_mtim, c = s.st_ctim;
#endif
    return std::to_string(s.st_dev) + ":" + std::to_string(s.st_ino) + ":" + std::to_string(s.st_size) + ":" +
        std::to_string(m.tv_sec) + ":" + std::to_string(m.tv_nsec) + ":" +
        std::to_string(c.tv_sec) + ":" + std::to_string(c.tv_nsec);
#endif
}
Json bind(Json value, const fs::path &path) {
    auto name = path.filename().u8string();
    value["filename"] = name;
    value["enabled"] = name.size() < 9 || name.substr(name.size() - 9) != ".disabled";
    if (!value.value("known", false)) value["name"] = name;
    return value;
}
}
Json scan_mod_file(const fs::path &path, const std::string &loader, bool use_cache) {
    auto before = stamp(path);
    auto key = fs::absolute(path).lexically_normal().u8string() + "\n" + loader;
    if (use_cache && !before.empty()) {
        std::lock_guard<std::mutex> lock(mutex);
        auto found = files.find(key);
        if (found != files.end() && found->second.stamp == before) {
            auto content = parsed.find(found->second.content);
            if (content != parsed.end()) {
                ++fast_hits; return bind(content->second.value, path);
            }
        }
    }
    auto bytes = read_file(path, 256 * 1024 * 1024);
    auto hash = hash_bytes(bytes), content_key = hash + "\n" + loader;
    Json value;
    {
        std::lock_guard<std::mutex> lock(mutex); ++reads;
        auto found = parsed.find(content_key);
        if (found != parsed.end()) { value = found->second.value; ++content_hits; }
    }
    if (value.is_null()) {
        value = inspect_mod_bytes(bytes, "content.jar", loader);
        std::lock_guard<std::mutex> lock(mutex); ++parses;
    }
    auto after = stamp(path);
    if (before != after)
        throw Error("mods", "file_changed", "模组文件已变化，请刷新列表。");
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto size = value.dump().size();
        if (size <= budget && !parsed.count(content_key)) {
            while (!parsed.empty() && (cached_bytes + size > budget || parsed.size() >= 256)) {
                cached_bytes -= parsed.begin()->second.bytes; parsed.erase(parsed.begin());
            }
            parsed.emplace(content_key, Parsed{value, size}); cached_bytes += size;
        }
        if (!after.empty()) {
            if (files.size() >= 1024) files.erase(files.begin());
            files[key] = {after, content_key};
        }
    }
    return bind(std::move(value), path);
}
Json mod_scan_cache_stats() {
    std::lock_guard<std::mutex> lock(mutex);
    return {{"reads", reads}, {"parses", parses}, {"fast_hits", fast_hits},
        {"content_hits", content_hits}, {"bytes", cached_bytes}, {"entries", parsed.size()}};
}
void clear_mod_scan_cache() {
    std::lock_guard<std::mutex> lock(mutex);
    parsed.clear(); files.clear(); cached_bytes = reads = parses = fast_hits = content_hits = 0;
}
} // namespace solar::engine
