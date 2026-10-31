// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <zlib.h>
#ifdef _WIN32
#include <bcrypt.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif
namespace solar::engine {
void Task::check() const {
    if (cancel && cancel->load())
        throw Error("operation", "cancelled", "操作已取消。");
}
void Task::report(const std::string &s, std::int64_t n, std::int64_t total) const {
    check();
    if (progress)
        progress(s, n, total);
}
fs::path checked_path(const fs::path &root, const std::string &relative) {
    auto p = fs::u8path(relative);
    if (p.empty() || p.is_absolute() || relative.find('\\') != relative.npos ||
        relative.find(':') != relative.npos || relative.find('\0') != relative.npos)
        throw Error("files", "unsafe_path", "路径无效。");
    for (const auto &part : p)
        if (part == "." || part == "..")
            throw Error("files", "unsafe_path", "路径不能跳出目标目录。");
    auto base = fs::weakly_canonical(root);
    auto full = fs::weakly_canonical(root / p);
    auto mismatch = std::mismatch(base.begin(), base.end(), full.begin(), full.end());
    if (mismatch.first != base.end())
        throw Error("files", "path_escape", "路径不能跳出目标目录。");
    auto walk = base;
    for (const auto &part : p) {
        walk /= part;
        if (fs::is_symlink(walk))
            throw Error("files", "symlink_target", "目标不能是符号链接。");
    }
    return base / p;
}
void atomic_write(const fs::path &target, const std::string &bytes) {
    fs::create_directories(target.parent_path());
    auto stage = target;
    stage += ".tmp-" + random_url_token();
    try {
        std::ofstream f(stage, std::ios::binary | std::ios::trunc);
        if (!f.write(bytes.data(), std::streamsize(bytes.size())))
            throw Error("files", "write_failed", "无法写入文件。");
        f.close();
        std::error_code ec;
        fs::rename(stage, target, ec);
#ifdef _WIN32
        if (ec) {
            if (!MoveFileExW(stage.c_str(), target.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw Error("files", "commit_failed", "无法提交写入文件。");
        }
#else
        if (ec)
            throw Error("files", "commit_failed", "无法提交写入文件。");
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(stage, ec);
        throw;
    }
}
Json read_json(const fs::path &p, const Json &fallback) {
    if (!fs::exists(p))
        return fallback;
    auto j = Json::parse(read_file(p), nullptr, false);
    if (j.is_discarded())
        throw Error("files", "invalid_json", "本地 JSON 无法解析：" + p.filename().u8string());
    return j;
}
class Digest {
    std::string algorithm_;
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm_handle_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
#elif defined(__APPLE__)
    CC_SHA512_CTX sha512_{};
    CC_SHA256_CTX sha256_{};
    CC_SHA1_CTX sha1_{};
#else
    EVP_MD_CTX *context_ = nullptr;
#endif
  public:
    explicit Digest(std::string a) : algorithm_(std::move(a)) {
        if (algorithm_ != "sha1" && algorithm_ != "sha256" && algorithm_ != "sha512")
            throw Error("hash", "invalid_algorithm", "不支持此哈希算法。");
#ifdef _WIN32
        auto type = algorithm_ == "sha512"   ? BCRYPT_SHA512_ALGORITHM
                    : algorithm_ == "sha256" ? BCRYPT_SHA256_ALGORITHM
                                             : BCRYPT_SHA1_ALGORITHM;
        if (BCryptOpenAlgorithmProvider(&algorithm_handle_, type, nullptr, 0) ||
            BCryptCreateHash(algorithm_handle_, &hash_, nullptr, 0, nullptr, 0, 0))
            throw Error("hash", "init_failed", "哈希初始化失败。");
#elif defined(__APPLE__)
        if (algorithm_ == "sha512")
            CC_SHA512_Init(&sha512_);
        else if (algorithm_ == "sha256")
            CC_SHA256_Init(&sha256_);
        else
            CC_SHA1_Init(&sha1_);
#else
        context_ = EVP_MD_CTX_new();
        auto md = algorithm_ == "sha512"   ? EVP_sha512()
                  : algorithm_ == "sha256" ? EVP_sha256()
                                           : EVP_sha1();
        if (!context_ || EVP_DigestInit_ex(context_, md, nullptr) != 1)
            throw Error("hash", "init_failed", "哈希初始化失败。");
#endif
    }
    ~Digest() {
#ifdef _WIN32
        if (hash_)
            BCryptDestroyHash(hash_);
        if (algorithm_handle_)
            BCryptCloseAlgorithmProvider(algorithm_handle_, 0);
#elif !defined(__APPLE__)
        EVP_MD_CTX_free(context_);
#endif
    }
    void update(const char *p, size_t n) {
#ifdef _WIN32
        if (BCryptHashData(hash_, (PUCHAR)p, ULONG(n), 0))
            throw Error("hash", "update_failed", "哈希计算失败。");
#elif defined(__APPLE__)
        if (algorithm_ == "sha512")
            CC_SHA512_Update(&sha512_, p, CC_LONG(n));
        else if (algorithm_ == "sha256")
            CC_SHA256_Update(&sha256_, p, CC_LONG(n));
        else
            CC_SHA1_Update(&sha1_, p, CC_LONG(n));
#else
        if (EVP_DigestUpdate(context_, p, n) != 1)
            throw Error("hash", "update_failed", "哈希计算失败。");
#endif
    }
    std::string final() {
        unsigned char bytes[64]{};
        unsigned n = algorithm_ == "sha512" ? 64 : algorithm_ == "sha256" ? 32 : 20;
#ifdef _WIN32
        if (BCryptFinishHash(hash_, bytes, n, 0))
            throw Error("hash", "final_failed", "哈希计算失败。");
#elif defined(__APPLE__)
        if (algorithm_ == "sha512")
            CC_SHA512_Final(bytes, &sha512_);
        else if (algorithm_ == "sha256")
            CC_SHA256_Final(bytes, &sha256_);
        else
            CC_SHA1_Final(bytes, &sha1_);
#else
        if (EVP_DigestFinal_ex(context_, bytes, &n) != 1)
            throw Error("hash", "final_failed", "哈希计算失败。");
#endif
        std::ostringstream out;
        for (unsigned k = 0; k < n; ++k)
            out << std::hex << std::setw(2) << std::setfill('0') << unsigned(bytes[k]);
        return out.str();
    }
};
std::string hash_bytes(const std::string &data, const std::string &algorithm) {
    Digest d(algorithm);
    d.update(data.data(), data.size());
    return d.final();
}
std::string hash_file(const fs::path &p, const std::string &algorithm, const Cancel &cancel) {
    Digest d(algorithm);
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error("hash", "file_unreadable", "无法读取待校验文件。");
    char buffer[65536];
    while (f) {
        if (cancel && cancel->load())
            throw Error("hash", "cancelled", "校验已取消。");
        f.read(buffer, sizeof(buffer));
        d.update(buffer, size_t(f.gcount()));
    }
    if (!f.eof())
        throw Error("hash", "read_failed", "无法完整读取待校验文件。");
    return d.final();
}
static std::uint16_t u16(const std::string &b, size_t p) {
    if (p + 2 > b.size())
        throw Error("archive", "zip_truncated", "ZIP 数据被截断。");
    return (unsigned char)b[p] | std::uint16_t((unsigned char)b[p + 1]) << 8;
}
static std::uint32_t u32(const std::string &b, size_t p) {
    return u16(b, p) | std::uint32_t(u16(b, p + 2)) << 16;
}
std::vector<ZipEntry> zip_entries(const std::string &b) {
    std::vector<ZipEntry> out;
    size_t end = b.npos;
    if (b.size() >= 22) {
        size_t low = b.size() > 65557 ? b.size() - 65557 : 0;
        for (size_t p = b.size() - 22;; --p) {
            if (u32(b, p) == 0x06054b50 && p + 22 + u16(b, p + 20) == b.size()) {
                end = p;
                break;
            }
            if (p == low)
                break;
        }
    }
    if (end == b.npos || u16(b, end + 4) || u16(b, end + 6) || u16(b, end + 10) == 65535)
        throw Error("archive", "invalid_zip", "压缩包格式无效或不支持 ZIP64。");
    size_t p = u32(b, end + 16);
    for (unsigned k = 0; k < u16(b, end + 10); ++k) {
        if (u32(b, p) != 0x02014b50)
            throw Error("archive", "invalid_zip", "ZIP 目录损坏。");
        unsigned n = u16(b, p + 28);
        if (p + 46 + n > b.size())
            throw Error("archive", "invalid_zip", "ZIP 文件名损坏。");
        ZipEntry e{b.substr(p + 46, n), u32(b, p + 16), u32(b, p + 20),
                   u32(b, p + 24),      u32(b, p + 42), u16(b, p + 10)};
        auto mode = (u32(b, p + 38) >> 16) & 0170000;
        if ((u16(b, p + 8) & 1) || mode == 0120000 || e.name.find('\0') != e.name.npos)
            throw Error("archive", "unsafe_entry", "压缩包包含加密或符号链接条目。");
        p += 46 + n + u16(b, p + 30) + u16(b, p + 32);
        if (p > end)
            throw Error("archive", "invalid_zip", "ZIP 目录越界。");
        out.push_back(e);
    }
    return out;
}
std::string zip_entry_bytes(const std::string &b, const std::string &name, size_t limit) {
    for (auto &e : zip_entries(b)) {
        if (e.name != name)
            continue;
        if (e.size > limit || e.compressed > b.size() || u32(b, e.offset) != 0x04034b50)
            throw Error("archive", "entry_limit", "压缩包条目过大或损坏。");
        size_t start = size_t(e.offset) + 30 + u16(b, e.offset + 26) + u16(b, e.offset + 28);
        if (start + e.compressed > b.size())
            throw Error("archive", "zip_truncated", "压缩包条目越界。");
        std::string output(e.size, '\0');
        if (e.method == 0)
            output = b.substr(start, e.compressed);
        else if (e.method == 8) {
            z_stream z{};
            z.next_in = (Bytef *)b.data() + start;
            z.avail_in = e.compressed;
            z.next_out = (Bytef *)output.data();
            z.avail_out = e.size;
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                throw Error("archive", "inflate_failed", "解压初始化失败。");
            auto r = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (r != Z_STREAM_END || z.total_out != e.size)
                throw Error("archive", "inflate_failed", "压缩包内容损坏。");
        } else
            throw Error("archive", "compression_unsupported", "不支持此压缩方式。");
        if (output.size() != e.size ||
            crc32(0, (const Bytef *)output.data(), uInt(output.size())) != e.crc)
            throw Error("archive", "crc_mismatch", "压缩包 CRC 校验失败。");
        return output;
    }
    return {};
}
std::string jar_entry(const fs::path &p, const std::string &name, size_t limit) {
    return zip_entry_bytes(read_file(p, 256 * 1024 * 1024), name, limit);
}
void extract_zip(const fs::path &p, const fs::path &directory, const Task &task) {
    auto b = read_file(p, 512 * 1024 * 1024);
    size_t expanded = 0;
    for (auto &e : zip_entries(b)) {
        task.check();
        if (e.name.empty() || e.name.back() == '/')
            continue;
        auto dest = checked_path(directory, e.name);
        expanded += e.size;
        if (expanded > 2ULL * 1024 * 1024 * 1024)
            throw Error("archive", "expansion_limit", "解压内容超过限制。");
        atomic_write(dest, zip_entry_bytes(b, e.name, 256 * 1024 * 1024));
    }
}
} // namespace solar::engine
