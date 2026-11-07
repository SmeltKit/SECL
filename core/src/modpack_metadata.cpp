// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "modpack_internal.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>

namespace solar::engine {
namespace {
// Inspection never extracts the archive. These bounds apply before allocating
// entry contents; declared downloads have a separate 16 GiB aggregate budget.
constexpr std::uint64_t archive_limit = 256ULL * 1024 * 1024;
constexpr std::uint64_t expanded_limit = 2ULL * 1024 * 1024 * 1024;
constexpr std::uint64_t download_limit = 16ULL * 1024 * 1024 * 1024;
constexpr std::size_t index_limit = 16 * 1024 * 1024;
constexpr std::size_t override_limit = 256 * 1024 * 1024;
constexpr std::size_t entry_limit = 10000;

[[noreturn]] void invalid(const std::string &message, const std::string &code = "invalid_modpack") {
    throw Error("modpack", code, message);
}
std::string lower_ascii(std::string value) {
    for (char &c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}
bool valid_utf8(const std::string &text) {
    for (std::size_t i = 0; i < text.size();) {
        auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) continue;
        unsigned remaining = 0, minimum = 0, point = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { remaining = 1; minimum = 0x80; point = lead & 31; }
        else if (lead >= 0xe0 && lead <= 0xef) { remaining = 2; minimum = 0x800; point = lead & 15; }
        else if (lead >= 0xf0 && lead <= 0xf4) { remaining = 3; minimum = 0x10000; point = lead & 7; }
        else return false;
        if (text.size() - i < remaining) return false;
        while (remaining--) {
            auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            point = (point << 6) | (next & 63);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) return false;
    }
    return true;
}
std::string safe_path(const std::string &value, bool directory = false) {
    if (value.empty() || value.size() > 4096 || !valid_utf8(value) || value.front() == '/' ||
        value.find('\\') != value.npos || value.find(':') != value.npos)
        invalid("整合包包含不安全的文件路径。", "unsafe_path");
    auto path = value;
    if (directory && path.back() == '/') path.pop_back();
    std::size_t begin = 0;
    while (begin <= path.size()) {
        auto end = path.find('/', begin);
        auto part = path.substr(begin, end == path.npos ? path.npos : end - begin);
        if (part.empty() || part.size() > 255 || part == "." || part == ".." ||
            part.back() == '.' || part.back() == ' ')
            invalid("整合包包含不安全的文件路径。", "unsafe_path");
        for (auto c : part) if (static_cast<unsigned char>(c) < 32 || c == 127 ||
            std::string("<>\"|?*").find(c) != std::string::npos)
            invalid("整合包包含不安全的文件路径。", "unsafe_path");
        auto stem = lower_ascii(part.substr(0, part.find('.')));
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        bool port_device = (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
            (stem.substr(3) == "¹" || stem.substr(3) == "²" || stem.substr(3) == "³");
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            stem == "conin$" || stem == "conout$" || port_device ||
            (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") &&
             stem[3] >= '0' && stem[3] <= '9'))
            invalid("整合包路径包含 Windows 保留名称。", "unsafe_path");
        if (end == path.npos) break;
        begin = end + 1;
    }
    return path;
}
void destination_path(const std::string &path) {
    safe_path(path);
    auto folded = lower_ascii(path);
    auto root = folded.substr(0, folded.find('/'));
    static const std::set<std::string> reserved{
        "versions", "libraries", "assets", "runtime", "runtimes", "natives",
        "launcher_profiles.json", "launcher_accounts.json", "launcher_settings.json",
        "secl.json", "secl", ".secl", ".solar", ".m2cl"};
    if (reserved.count(root)) invalid("整合包不能覆盖启动器管理的文件。", "reserved_path");
    std::size_t start = 0;
    while (start < folded.size()) {
        auto end = folded.find('/', start);
        auto part = folded.substr(start, end == folded.npos ? folded.npos : end - start);
        if (part.rfind(".solar-", 0) == 0 || part.rfind(".m2cl-", 0) == 0 ||
            part.rfind(".secl-", 0) == 0 || part == ".secl" || part == ".solar" || part == ".m2cl")
            invalid("整合包不能覆盖启动器管理的文件。", "reserved_path");
        if (end == folded.npos) break;
        start = end + 1;
    }
}
// Every implicit directory is also registered, so case differences in parent
// directories and file-versus-directory conflicts cannot pass unnoticed.
class PathTable {
    struct Node { std::string spelling; bool directory; int layer; };
    std::map<std::string, Node> paths_;
  public:
    void add(const std::string &path, bool directory, int layer, bool replacements) {
        std::size_t end = 0;
        for (;;) {
            end = path.find('/', end);
            bool final = end == path.npos;
            auto part = final ? path : path.substr(0, end);
            bool is_directory = !final || directory;
            auto key = lower_ascii(part);
            auto existing = paths_.find(key);
            if (existing == paths_.end()) paths_.emplace(key, Node{part, is_directory, layer});
            else {
                auto &old = existing->second;
                if (old.spelling != part || old.directory != is_directory)
                    invalid("整合包路径存在大小写冲突或文件与目录冲突。", "path_conflict");
                if (final && !is_directory) {
                    if (!replacements || layer <= old.layer)
                        invalid("整合包包含重复的文件路径。", "path_conflict");
                    old.layer = layer;
                }
            }
            if (final) break;
            ++end;
        }
    }
};
std::uint16_t word(const std::string &bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2)
        invalid("整合包 ZIP 数据被截断。", "invalid_archive");
    return static_cast<unsigned char>(bytes[offset]) |
           (std::uint16_t(static_cast<unsigned char>(bytes[offset + 1])) << 8);
}
std::uint32_t dword(const std::string &bytes, std::size_t offset) {
    return word(bytes, offset) | (std::uint32_t(word(bytes, offset + 2)) << 16);
}
std::vector<ZipEntry> checked_entries(const std::string &bytes, const Task &task) {
    std::size_t eocd = bytes.npos;
    if (bytes.size() >= 22) {
        auto low = bytes.size() > 65557 ? bytes.size() - 65557 : 0;
        for (auto at = bytes.size() - 22;; --at) {
            if (dword(bytes, at) == 0x06054b50 && at + 22 + word(bytes, at + 20) == bytes.size()) {
                eocd = at; break;
            }
            if (at == low) break;
        }
    }
    if (eocd == bytes.npos || word(bytes, eocd + 4) || word(bytes, eocd + 6) ||
        word(bytes, eocd + 8) != word(bytes, eocd + 10) || word(bytes, eocd + 10) == 65535)
        invalid("整合包必须是完整的单卷 ZIP，且不支持 ZIP64。", "invalid_archive");
    auto count = word(bytes, eocd + 10);
    if (count > entry_limit) invalid("整合包文件数量超过 10000 个的限制。", "archive_limit");
    std::size_t position = dword(bytes, eocd + 16), directory_size = dword(bytes, eocd + 12);
    const auto directory_begin = position;
    if (position > eocd || directory_size != eocd - position)
        invalid("整合包 ZIP 目录损坏。", "invalid_archive");
    std::vector<ZipEntry> entries;
    std::vector<std::pair<std::size_t, std::size_t>> spans;
    PathTable names;
    std::set<std::string> explicit_names;
    std::uint64_t expanded = 0;
    for (unsigned i = 0; i < count; ++i) {
        task.check();
        if (position > eocd || eocd - position < 46 || dword(bytes, position) != 0x02014b50)
            invalid("整合包 ZIP 目录损坏。", "invalid_archive");
        auto flags = word(bytes, position + 8), method = word(bytes, position + 10);
        auto length = word(bytes, position + 28), extra = word(bytes, position + 30), comment = word(bytes, position + 32);
        auto record_size = std::size_t(46) + length + extra + comment;
        if (record_size > eocd - position || word(bytes, position + 34))
            invalid("整合包 ZIP 目录损坏。", "invalid_archive");
        auto attributes = dword(bytes, position + 38);
        auto mode = (attributes >> 16) & 0170000;
        if ((flags & ~0x080eU) || (mode && mode != 0100000 && mode != 0040000))
            invalid("整合包包含加密、符号链接或特殊文件条目。", "unsafe_archive");
        if (method != 0 && method != 8)
            invalid("整合包使用了不支持的压缩方式。", "invalid_archive");
        ZipEntry entry{bytes.substr(position + 46, length), dword(bytes, position + 16),
            dword(bytes, position + 20), dword(bytes, position + 24), dword(bytes, position + 42), method};
        bool directory = !entry.name.empty() && entry.name.back() == '/';
        auto path = safe_path(entry.name, directory);
        if ((mode == 0040000 || (attributes & 0x10)) && !directory)
            invalid("整合包目录条目标记不一致。", "invalid_archive");
        if (!explicit_names.insert(path).second)
            invalid("整合包 ZIP 包含重复路径。", "path_conflict");
        names.add(path, directory, 0, false);
        if (entry.size == 0xffffffffU || entry.compressed == 0xffffffffU || entry.offset == 0xffffffffU ||
            (directory && entry.size))
            invalid("整合包 ZIP 条目大小无效或使用 ZIP64。", "invalid_archive");
        expanded += entry.size;
        if (expanded > expanded_limit) invalid("整合包展开后超过 2 GiB 的限制。", "archive_limit");
        auto local = std::size_t(entry.offset);
        if (local > directory_begin || directory_begin - local < 30 || dword(bytes, local) != 0x04034b50)
            invalid("整合包 ZIP 文件头损坏。", "invalid_archive");
        auto local_name = word(bytes, local + 26), local_extra = word(bytes, local + 28);
        auto start = local + 30 + local_name + local_extra;
        if (start > directory_begin || entry.compressed > directory_begin - start ||
            word(bytes, local + 6) != flags || word(bytes, local + 8) != method ||
            local_name != entry.name.size() || bytes.compare(local + 30, local_name, entry.name) != 0)
            invalid("整合包 ZIP 文件头与目录不一致。", "invalid_archive");
        if (!(flags & 8) && (dword(bytes, local + 14) != entry.crc ||
            dword(bytes, local + 18) != entry.compressed || dword(bytes, local + 22) != entry.size))
            invalid("整合包 ZIP 文件头与目录不一致。", "invalid_archive");
        if (method == 0 && entry.compressed != entry.size)
            invalid("整合包 ZIP 文件大小不一致。", "invalid_archive");
        spans.emplace_back(local, start + entry.compressed);
        entries.push_back(entry);
        position += record_size;
    }
    if (position != eocd) invalid("整合包 ZIP 目录损坏。", "invalid_archive");
    std::sort(spans.begin(), spans.end());
    for (std::size_t i = 1; i < spans.size(); ++i)
        if (spans[i].first < spans[i - 1].second)
            invalid("整合包 ZIP 文件条目重叠。", "invalid_archive");
    return entries;
}
std::string string_field(const Json &object, const char *field, std::size_t limit = 4096) {
    if (!object.is_object() || !object.contains(field) || !object[field].is_string())
        invalid(std::string("整合包缺少有效字段：") + field);
    auto value = object[field].get<std::string>();
    if (value.empty() || value.size() > limit || !valid_utf8(value) || value.find('\0') != value.npos)
        invalid(std::string("整合包字段无效：") + field);
    return value;
}
std::uint64_t unsigned_size(const Json &value) {
    if (!value.is_number_integer() || (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        invalid("整合包 fileSize 必须是非负整数。");
    auto result = value.get<std::uint64_t>();
    if (result > expanded_limit) invalid("整合包单个下载文件超过 2 GiB 的限制。", "archive_limit");
    return result;
}
std::string hash_field(const Json &hashes, const char *field, std::size_t length) {
    auto hash = string_field(hashes, field, length);
    if (hash.size() != length || !std::all_of(hash.begin(), hash.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    })) invalid(std::string("整合包文件哈希无效：") + field);
    return lower_ascii(hash);
}
void validate_url(const std::string &url) {
    if (url.size() > 8192 || url.rfind("https://", 0) != 0)
        invalid("整合包下载链接必须使用受信任域名的 HTTPS 地址。", "unsafe_url");
    auto end = url.find_first_of("/?#", 8);
    auto host = lower_ascii(url.substr(8, end == url.npos ? url.npos : end - 8));
    static const std::set<std::string> allowed{"cdn.modrinth.com", "github.com", "raw.githubusercontent.com", "gitlab.com"};
    if (!allowed.count(host) || url.find('#') != url.npos)
        invalid("整合包下载链接必须使用受信任域名的 HTTPS 地址。", "unsafe_url");
    const std::string legal = "-._~:/?[]@!$&'()*+,;=%";
    for (std::size_t i = 0; i < url.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(url[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              legal.find(char(c)) != legal.npos))
            invalid("整合包下载链接包含无效字符。", "unsafe_url");
        if (c == '%') {
            auto hex = [](unsigned char h) { return (h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'); };
            if (i + 2 >= url.size() || !hex(url[i + 1]) || !hex(url[i + 2]))
                invalid("整合包下载链接包含无效转义。", "unsafe_url");
            i += 2;
        }
    }
}
std::string side(const Json &env, const char *key) {
    auto value = string_field(env, key, 16);
    if (value != "required" && value != "optional" && value != "unsupported")
        invalid("整合包环境字段必须是 required、optional 或 unsupported。");
    return value;
}
Json parse_index(const std::string &text) {
    if (text.empty() || !valid_utf8(text)) invalid("整合包索引必须使用 UTF-8 编码。");
    std::vector<std::set<std::string>> keys;
    try {
        auto parsed = Json::parse(text, [&keys](int depth, Json::parse_event_t event, Json &value) {
            if (depth > 64) invalid("整合包索引嵌套层级超过限制。", "archive_limit");
            if (event == Json::parse_event_t::object_start) keys.emplace_back();
            if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
                invalid("整合包索引包含重复的 JSON 字段。");
            if (event == Json::parse_event_t::object_end) keys.pop_back();
            return true;
        });
        if (!parsed.is_object()) invalid("整合包索引必须是 JSON 对象。");
        return parsed;
    } catch (const Error &) { throw; }
    catch (const Json::exception &) { invalid("整合包索引 JSON 无法解析。"); }
}

std::string curseforge_id(const Json &file, const char *key) {
    if (!file.contains(key) || !file[key].is_number_integer())
        invalid(std::string("CurseForge 文件缺少有效的 ") + key + "。");
    auto id = file[key].get<std::uint64_t>();
    if (id == 0 || id > std::numeric_limits<std::uint32_t>::max())
        invalid(std::string("CurseForge 文件标识无效：") + key);
    return std::to_string(id);
}

void read_curseforge_manifest(const Json &manifest, Json &result, const Task &task) {
    if (!manifest.contains("manifestVersion") || !manifest["manifestVersion"].is_number_integer() ||
        manifest["manifestVersion"] != 1)
        invalid("仅支持 manifestVersion 为 1 的 CurseForge 整合包。", "unsupported_format");
    if (string_field(manifest, "manifestType", 32) != "minecraftModpack")
        invalid("仅支持 Minecraft 整合包。", "unsupported_game");
    if (manifest.contains("version")) {
        if (!manifest["version"].is_string()) invalid("CurseForge 整合包版本必须是字符串。");
        if (manifest["version"] != "") result["version_id"] = string_field(manifest, "version");
    }
    if (!manifest.contains("minecraft") || !manifest["minecraft"].is_object())
        invalid("CurseForge 整合包缺少有效的 minecraft。");
    const auto &minecraft = manifest["minecraft"];
    result["minecraft"] = string_field(minecraft, "version", 256);
    if (!minecraft.contains("modLoaders") || !minecraft["modLoaders"].is_array() ||
        minecraft["modLoaders"].size() > 16)
        invalid("CurseForge 整合包的 modLoaders 无效。");
    const auto &loaders = minecraft["modLoaders"];
    std::string primary;
    for (const auto &loader : loaders) {
        auto id = string_field(loader, "id", 256);
        if (loader.contains("primary") && !loader["primary"].is_boolean())
            invalid("CurseForge 整合包的 primary 必须是布尔值。");
        if (loader.value("primary", loaders.size() == 1)) {
            if (!primary.empty()) invalid("整合包声明了多个主加载器。", "multiple_loaders");
            primary = std::move(id);
        }
    }
    if (!loaders.empty()) {
        if (primary.empty()) invalid("整合包未指定主加载器。", "unsupported_dependency");
        const auto delimiter = primary.find('-');
        const auto kind = primary.substr(0, delimiter);
        static const std::set<std::string> supported{"fabric", "forge", "neoforge", "quilt"};
        if (!supported.count(kind) || delimiter == primary.npos || delimiter + 1 == primary.size())
            invalid("整合包要求尚不支持的加载器：" + primary, "unsupported_dependency");
        result["loader"] = kind;
        result["loader_version"] = primary.substr(delimiter + 1);
    }
    if (!manifest.contains("files") || !manifest["files"].is_array() || manifest["files"].size() > entry_limit)
        invalid("整合包 files 必须是最多包含 10000 项的数组。");
    std::set<std::string> ids;
    for (const auto &file : manifest["files"]) {
        task.check();
        if (!file.is_object() || !file.contains("required") || !file["required"].is_boolean())
            invalid("CurseForge 文件缺少有效的 required。");
        auto project_id = curseforge_id(file, "projectID"), file_id = curseforge_id(file, "fileID");
        auto key = project_id + "/" + file_id;
        if (!ids.insert(file_id).second) invalid("CurseForge 整合包重复声明了同一个文件。", "invalid_modpack");
        const auto required = file["required"].get<bool>();
        result["files"].push_back({{"path", key}, {"project_id", project_id}, {"file_id", file_id},
            {"client", required ? "required" : "optional"}});
        if (!required) result["optional_count"] = result["optional_count"].get<unsigned>() + 1;
    }
}

void read_overrides(Json &result, const std::vector<ZipEntry> &entries, const std::string &prefix,
                    int layer, PathTable &destinations, const Task &task) {
    for (const auto &entry : entries) {
        task.check();
        if (entry.name.rfind(prefix, 0) != 0 || entry.name.back() == '/') continue;
        if (entry.size > override_limit)
            invalid("整合包单个覆盖文件超过 256 MiB 的限制。", "archive_limit");
        auto destination = entry.name.substr(prefix.size());
        destination_path(destination);
        destinations.add(destination, false, layer, true);
        result["overrides"].push_back({{"entry", entry.name}, {"path", destination},
            {"size", entry.size}, {"crc32", entry.crc}, {"layer", layer}});
    }
}
} // namespace

std::string curseforge_modpack_destination(const std::string &kind, const std::string &filename) {
    static const std::map<std::string, std::pair<std::string, std::string>> locations{
        {"mod", {"mods/", ".jar"}}, {"resourcepack", {"resourcepacks/", ".zip"}},
        {"shader", {"shaderpacks/", ".zip"}}};
    const auto location = locations.find(kind);
    if (location == locations.end()) invalid("CurseForge 整合包含有无法自动安放的资源类型。", "unsupported_project_type");
    const auto suffix = fs::u8path(safe_path(filename)).extension().u8string();
    if (filename.find('/') != filename.npos || suffix != location->second.second)
        invalid("CurseForge 文件名与资源类型不匹配。", "unsafe_path");
    auto path = location->second.first + filename;
    destination_path(path);
    return path;
}

void validate_curseforge_modpack_paths(const Json &files, const Json &overrides) {
    PathTable destinations;
    for (const auto &file : files) destinations.add(file.at("path"), false, -1, false);
    for (const auto &file : overrides) destinations.add(file.at("path"), false, 0, false);
}

Json inspect_modpack(const fs::path &path, const Task &task) {
    task.check();
    const auto extension = lower_ascii(path.extension().u8string());
    if (extension != ".mrpack" && extension != ".zip")
        invalid("请选择 Modrinth .mrpack 或 CurseForge .zip 整合包。", "unsupported_format");
    const bool curseforge = extension == ".zip";
    const auto index_name = curseforge ? "manifest.json" : "modrinth.index.json";
    auto bytes = read_file(path, archive_limit);
    auto entries = checked_entries(bytes, task);
    bool found = false;
    for (const auto &entry : entries) if (entry.name == index_name) {
        found = true;
        if (entry.size > index_limit) invalid("整合包索引超过 16 MiB 的限制。", "archive_limit");
    }
    if (!found) invalid(std::string("整合包根目录缺少 ") + index_name + "。");
    auto index = parse_index(zip_entry_bytes(bytes, index_name, index_limit));
    Json result{{"path", fs::absolute(path).u8string()}, {"archive_sha512", hash_bytes(bytes)},
        {"format", curseforge ? "curseforge" : "modrinth"}, {"name", string_field(index, "name")}, {"version_id", ""},
        {"summary", ""}, {"minecraft", ""}, {"loader", "vanilla"}, {"loader_version", ""},
        {"files", Json::array()}, {"overrides", Json::array()}, {"server_files", 0},
        {"optional_count", 0}, {"override_count", 0}};
    PathTable destinations;
    if (curseforge) {
        read_curseforge_manifest(index, result, task);
        if (!index.contains("overrides") || index["overrides"] != "") {
            auto prefix = index.contains("overrides") ?
                safe_path(string_field(index, "overrides"), true) + "/" : "overrides/";
            read_overrides(result, entries, prefix, 0, destinations, task);
        }
        result["override_count"] = result["overrides"].size();
        task.check();
        return result;
    }
    if (!index.contains("formatVersion") || !index["formatVersion"].is_number_integer() || index["formatVersion"] != 1)
        invalid("仅支持 formatVersion 为 1 的 Modrinth 整合包。", "unsupported_format");
    if (string_field(index, "game", 32) != "minecraft")
        invalid("仅支持 Minecraft 整合包。", "unsupported_game");
    result["version_id"] = string_field(index, "versionId");
    if (index.contains("summary")) {
        if (!index["summary"].is_string()) invalid("整合包 summary 必须是字符串。");
        auto summary = index["summary"].get<std::string>();
        if (summary.size() > 65536 || summary.find('\0') != summary.npos) invalid("整合包 summary 无效。");
        result["summary"] = summary;
    }
    if (!index.contains("dependencies") || !index["dependencies"].is_object())
        invalid("整合包缺少有效的 dependencies。");
    const auto &dependencies = index["dependencies"];
    result["minecraft"] = string_field(dependencies, "minecraft", 256);
    const std::map<std::string, std::string> loaders{
        {"fabric-loader", "fabric"}, {"quilt-loader", "quilt"}, {"forge", "forge"}, {"neoforge", "neoforge"}};
    for (const auto &dependency : dependencies.items()) {
        if (dependency.key() == "minecraft") continue;
        auto supported = loaders.find(dependency.key());
        if (supported == loaders.end())
            invalid("整合包要求尚不支持的依赖：" + dependency.key(), "unsupported_dependency");
        if (result["loader"] != "vanilla") invalid("整合包声明了多个模组加载器。", "multiple_loaders");
        result["loader"] = supported->second;
        result["loader_version"] = string_field(dependencies, dependency.key().c_str(), 256);
    }
    if (!index.contains("files") || !index["files"].is_array() || index["files"].size() > entry_limit)
        invalid("整合包 files 必须是最多包含 10000 项的数组。");
    std::uint64_t download_total = 0;
    for (const auto &file : index["files"]) {
        task.check();
        auto destination = string_field(file, "path");
        destination_path(destination);
        destinations.add(destination, false, -1, false);
        if (!file.contains("hashes") || !file["hashes"].is_object()) invalid("整合包文件缺少 hashes。");
        auto sha1 = hash_field(file["hashes"], "sha1", 40), sha512 = hash_field(file["hashes"], "sha512", 128);
        if (!file.contains("fileSize")) invalid("整合包文件缺少 fileSize。");
        auto size = unsigned_size(file["fileSize"]);
        download_total += size;
        if (download_total > download_limit) invalid("整合包下载总大小超过 16 GiB 的限制。", "archive_limit");
        std::string client = "required";
        if (file.contains("env")) {
            client = side(file["env"], "client");
            side(file["env"], "server");
            for (const auto &field : file["env"].items()) if (field.key() != "client" && field.key() != "server")
                invalid("整合包包含不支持的环境字段：" + field.key());
        }
        if (!file.contains("downloads") || !file["downloads"].is_array() ||
            file["downloads"].empty() || file["downloads"].size() > 16)
            invalid("整合包文件必须包含 1 至 16 个下载链接。");
        Json urls = Json::array();
        std::set<std::string> unique_urls;
        for (const auto &download : file["downloads"]) {
            if (!download.is_string()) invalid("整合包下载链接必须是字符串。", "unsafe_url");
            auto url = download.get<std::string>(); validate_url(url);
            if (unique_urls.insert(url).second) urls.push_back(url);
        }
        result["files"].push_back({{"path", destination}, {"sha1", sha1}, {"sha512", sha512},
            {"size", size}, {"urls", urls}, {"client", client}});
        if (client == "unsupported") result["server_files"] = result["server_files"].get<unsigned>() + 1;
        if (client == "optional") result["optional_count"] = result["optional_count"].get<unsigned>() + 1;
    }
    read_overrides(result, entries, "overrides/", 0, destinations, task);
    read_overrides(result, entries, "client-overrides/", 1, destinations, task);
    // Server overrides are deliberately excluded from the client installation,
    // but their names and ZIP structure were checked along with every entry.
    result["override_count"] = result["overrides"].size();
    task.check();
    return result;
}
} // namespace solar::engine
