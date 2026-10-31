// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/engine.hpp"
#include <fstream>
#include <regex>
#include <set>
#include <zlib.h>
namespace solar::engine {
static std::string checksum(const std::string &url, const Task &t) {
    auto bytes = fetch_data(url, t, 16384);
    std::smatch m;
    if (!std::regex_search(bytes, m, std::regex("[A-Fa-f0-9]{64}")))
        throw Error("java", "invalid_checksum", "发行源没有提供有效的 SHA-256。");
    auto s = m[0].str();
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}
Json java_build_legal(const Json &p) {
    auto provider = p.value("provider", "");
    static const std::map<std::string, std::pair<std::string, std::string>> links{
        {"temurin", {"https://adoptium.net/", "https://adoptium.net/docs/faq/"}},
        {"zulu", {"https://www.azul.com/downloads/", "https://docs.azul.com/core/tpl"}},
        {"microsoft",
         {"https://learn.microsoft.com/en-us/java/openjdk/download",
          "https://learn.microsoft.com/en-us/java/openjdk/faq"}},
        {"oracle-openjdk", {"https://jdk.java.net/", "https://openjdk.org/legal/gplv2+ce.html"}}};
    if (!links.count(provider))
        throw Error("java", "unsupported_provider", "未知 Java 发行版。");
    auto v = links.at(provider);
    return {{"name", "GPLv2 + Classpath Exception"},
            {"website", v.first},
            {"license_url", v.second},
            {"build_url", p.value("source_page", v.first)},
            {"scope", "许可摘要仅供说明；完整许可与第三方声明以所选构建包内 "
                      "LICENSE 和 legal/ 为准。"}};
}
Json java_builds(int major, const std::string &provider, const std::string &os,
                 const std::string &raw_arch, const Task &t) {
    if (major < 8 || major > 99 || (os != "windows" && os != "osx" && os != "linux"))
        throw Error("java", "invalid_query", "Java 版本或操作系统无效。");
    auto arch = normalize_arch(raw_arch);
    Json out = Json::array();
    if (provider == "all") {
        for (auto name : {"zulu", "temurin", "microsoft", "oracle-openjdk"}) {
            try {
                for (auto &p : java_builds(major, name, os, arch, t))
                    out.push_back(p);
            } catch (const Error &) {
                t.check();
            }
        }
        return out;
    }
    if (provider == "temurin") {
        auto a = arch == "x86_64" ? "x64" : arch == "x86" ? "x86" : arch;
        auto data = fetch_json("https://api.adoptium.net/v3/assets/feature_releases/" +
                                   std::to_string(major) + "/ga?architecture=" + url_encode(a) +
                                   "&heap_size=normal&image_type=jre&jvm_impl=hotspot&os=" +
                                   (os == "osx" ? "mac" : os) + "&page_size=20&project=jdk",
                               t);
        for (auto &r : data)
            for (auto &b : r.value("binaries", Json::array())) {
                auto p = b.value("package", Json::object());
                out.push_back(
                    {{"provider", provider},
                     {"id", r.value("release_name", "")},
                     {"version", r.value("version_data", Json::object()).value("semver", "")},
                     {"major", major},
                     {"os", os},
                     {"arch", arch},
                     {"type", "JRE"},
                     {"url", p.value("link", "")},
                     {"sha256", p.value("checksum", "")},
                     {"size", p.value("size", 0LL)},
                     {"archive_name", p.value("name", "")},
                     {"source_page", r.value("release_link", "")}});
            }
    } else if (provider == "zulu") {
        auto a = arch == "aarch64" ? "arm" : arch == "x86" || arch == "x86_64" ? "x86" : arch;
        auto data = fetch_json("https://api.azul.com/metadata/v1/zulu/packages/?java_version=" +
                                   std::to_string(major) + "&os=" + (os == "osx" ? "macos" : os) +
                                   "&arch=" + a + "&hw_bitness=" + (arch == "x86" ? "32" : "64") +
                                   "&java_package_type=jre&javafx_bundled=false&archive_type=" +
                                   (os == "windows" ? "zip" : "tar.gz") +
                                   "&release_status=ga&availability_types=CA&page_size=30&latest="
                                   "false",
                               t);
        for (auto &p : data) {
            std::string version;
            for (auto &v : p.value("java_version", Json::array())) {
                if (!version.empty())
                    version += '.';
                version += std::to_string(v.get<int>());
            }
            out.push_back({{"provider", provider},
                           {"id", p.value("package_uuid", "")},
                           {"version", version},
                           {"major", major},
                           {"os", os},
                           {"arch", arch},
                           {"type", "JRE"},
                           {"url", p.value("download_url", "")},
                           {"sha256", p.value("sha256_hash", "")},
                           {"size", p.value("size", -1LL)},
                           {"archive_name", p.value("name", "")},
                           {"source_page", "https://www.azul.com/downloads/"}});
        }
    } else if (provider == "microsoft") {
        auto html = fetch_data("https://learn.microsoft.com/en-us/java/openjdk/download", t) +
                    fetch_data("https://learn.microsoft.com/en-us/java/openjdk/older-releases", t);
        auto a = arch == "x86_64" ? "x64" : arch == "aarch64" ? "aarch64" : "x86";
        std::regex pattern("https://aka\\.ms/download-jdk/(microsoft-jdk-([0-9]+(?:\\.[0-9]+)*)-" +
                           (os == "osx" ? "macos" : os) + "-" + a + "\\.(?:zip|tar\\.gz))");
        std::set<std::string> seen;
        for (std::sregex_iterator it(html.begin(), html.end(), pattern), end; it != end; ++it) {
            auto version = (*it)[2].str();
            if (std::stoi(version) != major || !seen.insert((*it)[1]).second)
                continue;
            out.push_back(
                {{"provider", provider},
                 {"id", (*it)[1].str()},
                 {"version", version},
                 {"major", major},
                 {"os", os},
                 {"arch", arch},
                 {"type", "JDK"},
                 {"url", it->str()},
                 {"checksum_url", it->str() + ".sha256sum.txt"},
                 {"size", -1},
                 {"archive_name", (*it)[1].str()},
                 {"source_page", "https://learn.microsoft.com/en-us/java/openjdk/download"}});
        }
    } else if (provider == "oracle-openjdk") {
        auto html = fetch_data("https://jdk.java.net/archive/", t);
        std::regex pattern(
            R"LINK(https://download\.java\.net/java/GA/[^"<>\s]+/(openjdk-(\d+(?:\.\d+)*)_(windows|linux|macos|osx)-([a-z0-9_-]+)_bin\.(zip|tar\.gz)))LINK");
        std::set<std::string> seen;
        for (std::sregex_iterator it(html.begin(), html.end(), pattern), end; it != end; ++it) {
            auto version = (*it)[2].str(), system = (*it)[3].str();
            if (system == "macos")
                system = "osx";
            if (std::stoi(version) != major || system != os || normalize_arch((*it)[4]) != arch ||
                !seen.insert(it->str()).second)
                continue;
            out.push_back({{"provider", provider},
                           {"id", it->str()},
                           {"version", version},
                           {"major", major},
                           {"os", os},
                           {"arch", arch},
                           {"type", "JDK"},
                           {"url", it->str()},
                           {"checksum_url", it->str() + ".sha256"},
                           {"size", -1},
                           {"archive_name", (*it)[1].str()},
                           {"source_page", "https://jdk.java.net/archive/"},
                           {"archived", true}});
        }
    } else
        throw Error("java", "unsupported_provider", "未知 Java 发行版。");
    return out;
}
static void extract_tar_gz(const fs::path &archive, const fs::path &dest, const Task &t) {
#ifdef _WIN32
    auto f = gzopen_w(archive.c_str(), "rb");
#else
    auto f = gzopen(archive.c_str(), "rb");
#endif
    if (!f)
        throw Error("archive", "gzip_open", "无法读取 Java 压缩包。");
    struct Close {
        gzFile f;
        ~Close() { gzclose(f); }
    } close{f};
    auto read = [&](size_t n) {
        std::string b(n, '\0');
        size_t got = 0;
        while (got < n) {
            t.check();
            int r = gzread(f, b.data() + got, unsigned(std::min<size_t>(65536, n - got)));
            if (r <= 0)
                throw Error("archive", "tar_truncated", "Java 压缩包已截断。");
            got += size_t(r);
        }
        return b;
    };
    auto number = [](std::string s) {
        s = std::regex_replace(s, std::regex("^[ \\x00]+|[ \\x00]+$"), "");
        if (s.empty())
            return 0ULL;
        if (!std::regex_match(s, std::regex("[0-7]+")))
            throw Error("archive", "tar_number", "TAR 数字字段无效。");
        return std::stoull(s, nullptr, 8);
    };
    auto text = [](std::string s) { return s.substr(0, s.find('\0')); };
    std::string longname, longlink;
    Json pax = Json::object();
    std::vector<std::tuple<std::string, std::string, bool>> links;
    std::uint64_t expanded = 0;
    unsigned count = 0;
    for (;;) {
        auto h = read(512);
        if (h == std::string(512, '\0'))
            break;
        if (++count > 50000)
            throw Error("archive", "tar_limit", "Java 压缩包条目过多。");
        unsigned sum = 0;
        for (size_t k = 0; k < h.size(); ++k)
            sum += k >= 148 && k < 156 ? ' ' : static_cast<unsigned char>(h[k]);
        if (sum != number(h.substr(148, 8)))
            throw Error("archive", "tar_checksum", "TAR 头部校验失败。");
        auto name = text(h.substr(0, 100)), prefix = text(h.substr(345, 155)),
             link = text(h.substr(157, 100));
        if (!prefix.empty())
            name = prefix + "/" + name;
        auto size = number(h.substr(124, 12)), mode = number(h.substr(100, 8));
        char type = h[156];
        if (size > 512ULL * 1024 * 1024 || (expanded += size) > 2ULL * 1024 * 1024 * 1024)
            throw Error("archive", "tar_limit", "Java 解压体积超过限制。");
        auto payload = read(size_t((size + 511) / 512 * 512)).substr(0, size_t(size));
        if (type == 'L') {
            longname = text(payload);
            continue;
        }
        if (type == 'K') {
            longlink = text(payload);
            continue;
        }
        if (type == 'x' || type == 'g') {
            size_t pos = 0;
            while (pos < payload.size()) {
                auto space = payload.find(' ', pos);
                if (space == payload.npos)
                    throw Error("archive", "invalid_pax", "PAX 格式无效。");
                auto len = std::stoull(payload.substr(pos, space - pos));
                if (!len || pos + len > payload.size())
                    throw Error("archive", "invalid_pax", "PAX 长度无效。");
                auto item = payload.substr(space + 1, pos + len - space - 2);
                auto eq = item.find('=');
                if (eq != item.npos)
                    pax[item.substr(0, eq)] = item.substr(eq + 1);
                pos += len;
            }
            continue;
        }
        if (!longname.empty()) {
            name = longname;
            longname.clear();
        }
        if (!longlink.empty()) {
            link = longlink;
            longlink.clear();
        }
        name = pax.value("path", name);
        link = pax.value("linkpath", link);
        pax = Json::object();
        while (name.rfind("./", 0) == 0)
            name.erase(0, 2);
        while (!name.empty() && name.back() == '/')
            name.pop_back();
        if (name.empty())
            continue;
        auto path = checked_path(dest, name);
        if (type == '0' || type == '\0') {
            atomic_write(path, payload);
            if (mode & 0111)
                fs::permissions(
                    path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add);
        } else if (type == '5')
            fs::create_directories(path);
        else if (type == '1' || type == '2') {
            if (size || fs::u8path(link).is_absolute())
                throw Error("archive", "tar_link", "Java 压缩包链接无效。");
            auto target =
                (type == '1' ? fs::u8path(link) : fs::u8path(name).parent_path() / fs::u8path(link))
                    .lexically_normal()
                    .generic_u8string();
            checked_path(dest, target);
            links.push_back({name, target, type == '1'});
        } else
            throw Error("archive", "tar_type", "Java 压缩包包含不支持的条目类型。");
    }
    for (auto &entry : links) {
        auto from = checked_path(dest, std::get<0>(entry)),
             to = checked_path(dest, std::get<1>(entry));
        if (!fs::is_regular_file(to))
            throw Error("archive", "tar_link", "Java 链接目标不存在。");
        fs::create_directories(from.parent_path());
        fs::copy_file(to, from);
    }
}
void extract_java_archive(const fs::path &archive, const fs::path &dest, const Task &t) {
    if (archive.extension() == ".zip")
        extract_zip(archive, dest, t);
    else
        extract_tar_gz(archive, dest, t);
}
Json install_java(const Json &input, const fs::path &directory, const Task &t) {
    auto p = input;
    auto provider = p.value("provider", "");
    if (p.value("os", "") != platform().os)
        throw Error("java", "wrong_os", "不能在当前系统安装其他系统的 Java。");
    if (provider == "zulu" && p.value("sha256", "").empty()) {
        auto detail = fetch_json(
            "https://api.azul.com/metadata/v1/zulu/packages/" + url_encode(p.value("id", "")), t);
        p["sha256"] = detail.value("sha256_hash", "");
        if (!detail.value("download_url", "").empty())
            p["url"] = detail["download_url"];
    }
    if (p.value("sha256", "").empty())
        p["sha256"] = checksum(p.value("checksum_url", ""), t);
    auto fingerprint = hash_bytes(p.dump(), "sha256").substr(0, 20);
    auto final = checked_path(directory, provider + "-" + fingerprint);
    if (fs::exists(final)) {
        auto scan = scan_java({final}, false, t, 8, 20000);
        if (scan["runtimes"].size() == 1)
            return scan["runtimes"][0];
        throw Error("java", "incomplete_install", "已有 Java 安装目录不完整。");
    }
    auto stage = checked_path(directory, ".install-" + random_url_token());
    fs::create_directories(stage);
    try {
        auto name = p.value("archive_name", "");
        bool zip = name.size() > 4 && name.substr(name.size() - 4) == ".zip";
        auto archive = stage / (zip ? "runtime.zip" : "runtime.tar.gz");
        p["size"] = -1;
        download(p, archive, t);
        auto data = stage / "runtime";
        extract_java_archive(archive, data, t);
        auto runtimes = scan_java({data}, false, t, 10, 30000).at("runtimes");
        if (runtimes.size() != 1)
            throw Error("java", "ambiguous_runtime", "压缩包没有唯一可执行的 Java。");
        auto j = runtimes[0];
        if (j.value("major", 0) != p.value("major", 0) ||
            normalize_arch(j.value("arch", "")) != normalize_arch(p.value("arch", "")))
            throw Error("java", "runtime_mismatch", "Java 实际版本或架构与所选构建不匹配。");
        auto vendor = j.value("vendor", "");
        std::transform(vendor.begin(), vendor.end(), vendor.begin(), ::tolower);
        bool vendor_ok = provider == "temurin"          ? (vendor.find("adoptium") != vendor.npos ||
                                                  vendor.find("adoptopenjdk") != vendor.npos)
                         : provider == "zulu"           ? vendor.find("azul") != vendor.npos
                         : provider == "microsoft"      ? vendor.find("microsoft") != vendor.npos
                         : provider == "oracle-openjdk" ? vendor.find("oracle") != vendor.npos
                                                        : false;
        if (!vendor_ok)
            throw Error("java", "vendor_mismatch", "实际 Java 发行版与所选构建不一致。");
        auto expected = p.value("version", "");
        auto actual = j.value("runtime_version", j.value("version", ""));
        auto simple = [](std::string s) {
            s = std::regex_replace(s, std::regex("[-+]LTS.*$"), "");
            return s;
        };
        if (!expected.empty() && simple(actual).rfind(simple(expected), 0) != 0 &&
            simple(j.value("version", "")).rfind(simple(expected), 0) != 0)
            throw Error("java", "build_mismatch", "Java 实际版本与所选构建不一致。");
        auto relative = fs::relative(fs::u8path(j.at("path").get<std::string>()), data);
        atomic_write(data / ".solar-runtime.json", p.dump(2));
        fs::rename(data, final);
        std::error_code cleanup;
        fs::remove_all(stage, cleanup);
        j["path"] = (final / relative).u8string();
        return j;
    } catch (...) {
        std::error_code ec;
        fs::remove_all(stage, ec);
        throw;
    }
}
} // namespace solar::engine
