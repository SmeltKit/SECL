// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository root.
#include "solar/engine.hpp"
#include "toml.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>

// Metadata semantics are based on the loader specifications and their readers:
// https://docs.fabricmc.net/develop/loader/fabric-mod-json
// https://github.com/QuiltMC/rfcs/blob/main/specification/0002-quilt.mod.json.md
// https://github.com/QuiltMC/quilt-loader/blob/develop/src/main/java/org/quiltmc/loader/impl/metadata/qmj/V1ModMetadataReader.java
// https://docs.minecraftforge.net/en/latest/gettingstarted/modfiles/
// https://docs.neoforged.net/docs/gettingstarted/modfiles/
// https://github.com/MinecraftForge/JarJar/tree/main/metadata/src/main/java/net/minecraftforge/jarjar/metadata/json

namespace solar::engine {
namespace {
constexpr size_t metadata_limit = 2 * 1024 * 1024;
constexpr size_t nested_jar_limit = 32 * 1024 * 1024;
constexpr size_t nested_total_limit = 128 * 1024 * 1024;
constexpr size_t nested_count_limit = 128;
constexpr unsigned expression_depth_limit = 32;
struct ScanBudget { size_t count = 0, bytes = 0; };

[[noreturn]] void invalid(const std::string &message) {
    throw Error("mods", "invalid_metadata", message);
}
void warning(Json &m, const std::string &code, const std::string &message,
             const std::string &source) {
    m["metadata_warnings"].push_back({{"code", code}, {"message", message}, {"source", source}});
}
std::string string_field(const Json &value, const char *key, const std::string &fallback = {}) {
    if (!value.is_object()) invalid("模组元数据必须是对象。");
    auto it = value.find(key);
    if (it == value.end()) return fallback;
    if (!it->is_string()) invalid(std::string("模组元数据字段必须是字符串：") + key);
    return it->get<std::string>();
}
bool bool_field(const Json &value, const char *key, bool fallback = false) {
    auto it = value.find(key);
    if (it == value.end()) return fallback;
    if (!it->is_boolean()) invalid(std::string("模组元数据字段必须是布尔值：") + key);
    return it->get<bool>();
}
void bounded_array(const Json &value, const std::string &source, size_t limit = 4096) {
    if (!value.is_array() || value.size() > limit)
        invalid("模组元数据数组无效或超过限制：" + source);
}
Json metadata(const std::string &loader, const std::string &source) {
    return {{"loader", loader}, {"metadata_source", source}, {"ids", Json::array()},
            {"requirements", Json::object()}, {"breaks", Json::object()},
            {"conflicts", Json::object()}, {"components", Json::array()},
            {"provided_versions", Json::object()}, {"provided_groups", Json::object()},
            {"embedded_hashes", Json::object()}, {"dependency_clauses", Json::array()},
            {"metadata_warnings", Json::array()}, {"environment", "*"}, {"icon_path", ""}};
}
void provide(Json &m, const std::string &id, const std::string &version,
             const std::string &group = {}) {
    if (id.empty() || version.empty()) invalid("模组声明的 ID 或版本为空。");
    auto pos = id.find(':');
    auto short_id = pos == std::string::npos ? id : id.substr(pos + 1);
    auto actual_group = pos == std::string::npos ? group : id.substr(0, pos);
    if (short_id.empty()) invalid("模组声明的 ID 为空。");
    bool valid_id = short_id.size() >= 2 && short_id.size() <= 64 &&
                    short_id.front() >= 'a' && short_id.front() <= 'z';
    const bool forge = m["loader"] == "forge" || m["loader"] == "neoforge";
    for (unsigned char c : short_id)
        valid_id &= (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || (!forge && c == '-');
    if (!valid_id || (pos != std::string::npos && m["loader"] != "quilt"))
        warning(m, "invalid_mod_id", "模组 ID 不符合加载器规定，需手动确认。", id);
    if (version.find("${") != version.npos)
        warning(m, "unresolved_placeholder", "模组或别名版本包含未替换的占位符。", id);
    for (auto &key : actual_group.empty() ? std::vector<std::string>{short_id}
                                         : std::vector<std::string>{short_id, actual_group + ":" + short_id}) {
        if (m["provided_versions"].contains(key)) {
            if (m["provided_versions"][key] != version)
                warning(m, "ambiguous_provided_version", "同一模组别名声明了多个版本，需手动确认。", key);
            continue;
        }
        m["ids"].push_back(key);
        m["provided_versions"][key] = version;
    }
    if (!actual_group.empty()) m["provided_groups"][short_id] = actual_group;
}
// Legacy maps are retained for integrations which do not yet consume clauses. In
// particular, repeated requirements must be AND, never an overwritten last value.
void legacy_clause(Json &m, const std::string &kind, const Json &expression) {
    if (expression.value("op", "") != "atom" || expression.value("optional", false) ||
        expression.contains("unless") || expression.contains("environment")) return;
    auto field = kind == "depends" ? "requirements" : kind == "breaks" ? "breaks"
                                           : kind == "conflicts" ? "conflicts" : "";
    if (!*field) return;
    auto id = expression.at("id").get<std::string>();
    auto range = expression.at("range");
    if (!m[field].contains(id)) m[field][id] = range;
    else {
        const char *join = kind == "depends" ? "all" : "any";
        auto before = m[field][id];
        m[field][id] = Json{{join, Json::array({before, range})}};
    }
}
void clause(Json &m, const std::string &kind, Json expression, const std::string &source,
            const std::string &owner = {}, const std::string &side = "BOTH") {
    Json value{{"kind", kind}, {"expression", expression}, {"source", source}, {"side", side}};
    if (!owner.empty()) value["owner"] = owner;
    m["dependency_clauses"].push_back(value);
    legacy_clause(m, kind, expression);
}
Json atom(const std::string &id, const Json &range, const std::string &dialect) {
    if (id.empty()) invalid("模组依赖缺少 ID。");
    return {{"op", "atom"}, {"id", id}, {"range", range}, {"dialect", dialect}};
}
Json range_value(const Json &raw, const std::string &dialect, Json &m,
                 const std::string &source, unsigned depth = 0) {
    if (depth > expression_depth_limit) invalid("模组依赖表达式层级过深。");
    if (raw.is_string()) {
        auto text = raw.get<std::string>();
        if (text.empty()) invalid("模组依赖版本范围为空。");
        if (text.find("${") != text.npos)
            warning(m, "unresolved_placeholder", "依赖版本范围包含未替换的占位符。", source);
        return raw;
    }
    if (raw.is_array()) {
        bounded_array(raw, source);
        if (raw.empty()) invalid("模组依赖版本数组为空。");
        if (dialect == "quilt")
            warning(m, "deprecated_quilt_version_array", "Quilt 版本数组按任一版本匹配，建议检查其范围。", source);
        Json values = Json::array();
        for (auto &entry : raw) {
            if (!entry.is_string()) invalid("模组依赖版本数组必须只包含字符串。");
            values.push_back(range_value(entry, dialect, m, source, depth + 1));
        }
        return values;
    }
    if (dialect == "quilt" && raw.is_object() && raw.size() == 1 &&
        (raw.contains("any") || raw.contains("all"))) {
        auto key = raw.begin().key();
        auto values = raw.begin().value();
        bounded_array(values, source);
        if (values.empty()) invalid("Quilt 版本约束组为空。");
        for (auto &value : values) value = range_value(value, dialect, m, source, depth + 1);
        return Json{{key, values}};
    }
    invalid("无法识别模组依赖版本范围：" + source);
}
Json quilt_dependency(const Json &raw, bool any, Json &m, const std::string &source,
                      unsigned depth = 0) {
    if (depth > expression_depth_limit) invalid("模组依赖表达式层级过深。");
    if (raw.is_string()) return atom(raw.get<std::string>(), "*", "quilt");
    if (raw.is_array()) {
        bounded_array(raw, source);
        if (raw.empty()) invalid("Quilt 依赖组为空。");
        Json children = Json::array();
        for (auto &value : raw) children.push_back(quilt_dependency(value, any, m, source, depth + 1));
        return {{"op", any ? "any" : "all"}, {"children", children}};
    }
    if (!raw.is_object()) invalid("Quilt 依赖必须是字符串、对象或数组。");
    auto out = atom(string_field(raw, "id"),
                    range_value(raw.value("versions", Json("*")), "quilt", m, source), "quilt");
    if (bool_field(raw, "optional")) out["optional"] = true;
    if (raw.contains("unless")) out["unless"] = quilt_dependency(raw.at("unless"), true, m, source, depth + 1);
    if (raw.contains("reason")) out["reason"] = string_field(raw, "reason");
    if (raw.contains("environment")) {
        auto env = string_field(raw, "environment");
        out["environment"] = env;
        // This game-provider extension is described by the RFC, but the current
        // stock Quilt parser does not consume it. Do not claim it is enforced.
        warning(m, "quilt_dependency_environment", "Quilt 依赖环境扩展的处理取决于游戏提供器，需手动确认。", source);
    }
    static const std::set<std::string> known{"id", "versions", "optional", "unless", "reason", "environment"};
    for (auto &field : raw.items()) if (!known.count(field.key()))
        warning(m, "unsupported_dependency_field", "依赖含未支持的字段：" + field.key(), source);
    return out;
}
void icon(Json &m, const Json &value) {
    if (value.is_null()) return;
    if (value.is_string()) { m["icon_path"] = value; return; }
    if (value.is_object()) {
        for (auto &item : value.items()) {
            if (!item.value().is_string()) invalid("模组图标路径必须是字符串。");
            if (m["icon_path"] == "") m["icon_path"] = item.value();
        }
        return;
    }
    invalid("模组图标声明无效。");
}
Json json_metadata(const std::string &text, const std::string &loader, const std::string &source) {
    auto doc = Json::parse(text);
    if (!doc.is_object()) invalid("模组元数据必须是对象。");
    auto m = metadata(loader, source);
    const bool quilt = loader == "quilt";
    auto schema = doc.find(quilt ? "schema_version" : "schemaVersion");
    if (schema == doc.end() || !schema->is_number_integer() || *schema != 1)
        warning(m, "unsupported_metadata_schema", "模组元数据版本未受完整支持。", source);
    auto data = quilt ? doc.at("quilt_loader") : doc;
    if (!data.is_object()) invalid("模组加载器元数据必须是对象。");
    auto id = string_field(data, "id"), version = string_field(data, "version");
    auto group = quilt ? string_field(data, "group") : "";
    m["id"] = id; m["version"] = version; m["group"] = group;
    if (version.find("${") != version.npos)
        warning(m, "unresolved_placeholder", "模组版本包含未替换的占位符。", source);
    auto display = quilt ? data.value("metadata", Json::object()) : data;
    m["name"] = string_field(display, "name", id);
    icon(m, display.value("icon", Json()));
    if (quilt) {
        if (doc.contains("minecraft")) m["environment"] = string_field(doc.at("minecraft"), "environment", "*");
        if (group.empty()) warning(m, "missing_quilt_group", "Quilt 元数据缺少 Maven 组，分组依赖无法可靠核对。", source);
        for (auto field : {"depends", "breaks"}) {
            auto dependencies = data.value(field, Json::array());
            bounded_array(dependencies, source);
            size_t n = 0;
            for (auto &dependency : dependencies)
                clause(m, field, quilt_dependency(dependency, std::string(field) == "depends", m,
                       source + ":quilt_loader." + field + "[" + std::to_string(n++) + "]"), source, id);
        }
        static const std::set<std::string> loader_fields{"id","group","version","entrypoints","jars",
            "language_adapters","depends","breaks","repositories","load_type","provides",
            "intermediate_mappings","metadata","required_features"};
        for (auto &field : data.items()) if (!loader_fields.count(field.key()))
            warning(m, "unsupported_loader_field", "Quilt 加载元数据含未支持的字段：" + field.key(), source);
        if (data.contains("required_features"))
            warning(m, "unsupported_loader_feature", "Quilt 模组要求的加载器功能尚未核对。", source);
        if (doc.contains("plugins") || data.contains("plugins"))
            warning(m, "loader_plugin", "Quilt 插件可更改模组发现及依赖规则，需手动确认。", source);
        m["load_type"] = string_field(data, "load_type", "");
        if (m["load_type"] != "" && m["load_type"] != "always" &&
            m["load_type"] != "if_possible" && m["load_type"] != "if_required")
            warning(m, "unsupported_load_type", "无法识别 Quilt 模组加载策略。", source);
        if (data.contains("intermediate_mappings")) m["intermediate_mappings"] = data["intermediate_mappings"];
    } else {
        m["environment"] = string_field(data, "environment", "*");
        for (auto field : {"depends", "breaks", "conflicts", "recommends", "suggests"}) {
            auto dependencies = data.value(field, Json::object());
            if (!dependencies.is_object() || dependencies.size() > 4096) invalid("Fabric 依赖表无效。");
            for (auto &dependency : dependencies.items())
                clause(m, field, atom(dependency.key(), range_value(dependency.value(), "fabric", m,
                       source + ":" + field + "." + dependency.key()), "fabric"), source, id);
        }
    }
    auto environment = m["environment"].get<std::string>();
    if (environment != "*" && environment != "client" && environment != "server" && environment != "dedicated_server")
        warning(m, "unsupported_environment", "无法识别模组运行环境。", source);
    provide(m, id, version, group);
    auto provides = data.value("provides", Json::array());
    bounded_array(provides, source);
    for (auto &provided : provides) {
        if (provided.is_string()) provide(m, provided.get<std::string>(), version, group);
        else if (quilt && provided.is_object())
            provide(m, string_field(provided, "id"), string_field(provided, "version", version), group);
        else invalid("模组别名声明格式无效。");
    }
    auto jars = data.value("jars", Json::array());
    if (quilt && doc.contains("jars")) {
        warning(m, "unsupported_top_level_jars", "Quilt 顶层 jars 不属于标准加载元数据。", source);
        if (jars.empty()) jars = doc["jars"];
    }
    bounded_array(jars, source, nested_count_limit);
    m["nested_jars"] = Json::array();
    for (auto &jar : jars) {
        if (jar.is_string() && quilt) m["nested_jars"].push_back(jar);
        else if (jar.is_object()) {
            if (quilt) warning(m, "nonstandard_nested_jar", "Quilt 内嵌 JAR 使用了非标准对象格式。", source);
            m["nested_jars"].push_back(string_field(jar, "file"));
        } else invalid("内嵌模组声明格式无效。");
    }
    return m;
}

Json toml_json(const toml::node &node) {
    if (auto table = node.as_table()) {
        Json out = Json::object();
        for (auto &[key, value] : *table) out[std::string(key.str())] = toml_json(value);
        return out;
    }
    if (auto array = node.as_array()) {
        Json out = Json::array();
        for (auto &value : *array) out.push_back(toml_json(value));
        return out;
    }
    if (auto value = node.value<std::string>()) return *value;
    if (auto value = node.value<bool>()) return *value;
    if (auto value = node.value<std::int64_t>()) return *value;
    if (auto value = node.value<double>()) return *value;
    // Dates cannot occur in any compatibility field; retaining their text also
    // permits arbitrary TOML properties without pretending they are strings.
    std::ostringstream text; text << toml::toml_formatter{node};
    return Json{{"toml_literal", text.str()}};
}
std::map<std::string, std::string> manifest_main(const std::string &bytes) {
    std::map<std::string, std::string> out;
    std::istringstream stream(bytes);
    std::string line, key;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break; // Named sections must not override main attributes.
        if (line.front() == ' ' && !key.empty()) { out[key] += line.substr(1); continue; }
        auto colon = line.find(": ");
        if (colon == line.npos) { key.clear(); continue; }
        key = line.substr(0, colon);
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        out[key] = line.substr(colon + 2);
    }
    return out;
}
std::string substitute_version(std::string version, const Json &properties,
                               const std::map<std::string, std::string> &manifest,
                               Json &m, const std::string &source) {
    size_t start = 0;
    while ((start = version.find("${file.", start)) != version.npos) {
        auto end = version.find('}', start);
        if (end == version.npos) break;
        auto key = version.substr(start + 7, end - start - 7);
        std::string value;
        if (key == "jarVersion") {
            auto found = manifest.find("implementation-version");
            if (found != manifest.end()) value = found->second;
        } else if (properties.is_object() && properties.contains(key) && properties[key].is_string())
            value = properties[key].get<std::string>();
        if (!value.empty() && value.find("${") == value.npos) {
            version.replace(start, end - start + 1, value); start += value.size();
        } else start = end + 1;
    }
    if (version.find("${") != version.npos)
        warning(m, "unresolved_placeholder", "模组版本中的文件属性无法解析，需手动确认。", source);
    return version;
}
Json forge_metadata(const std::string &text, std::string loader, const std::string &source,
                    const std::map<std::string, std::string> &manifest) {
    auto doc = toml_json(toml::parse(text));
    // Early NeoForge releases use META-INF/mods.toml. A declared neoforge
    // dependency distinguishes them without searching comments/descriptions.
    if (loader == "forge" && doc.contains("dependencies") && doc["dependencies"].is_object())
        for (auto &owner : doc["dependencies"].items()) if (owner.value().is_array())
            for (auto &dep : owner.value()) if (dep.is_object() && string_field(dep, "modId") == "neoforge") loader = "neoforge";
    auto m = metadata(loader, source);
    auto language = string_field(doc, "modLoader"), range = string_field(doc, "loaderVersion");
    if (language.empty() || range.empty())
        warning(m, "missing_language_loader", "Forge 模组缺少语言加载器或其版本范围。", source);
    else {
        auto version_source = loader == "forge" && (language == "javafml" || language == "lowcodefml")
                                  ? "forge_major" : (language == "javafml" || language == "lowcodefml") ? "fml" : "unknown";
        m["language_loader"] = {{"id", language}, {"range", range}, {"dialect", "maven"}, {"version_source", version_source}};
        if (std::string(version_source) == "unknown")
            warning(m, "unsupported_language_loader", "自定义语言加载器的安装与版本需手动确认。", source);
    }
    if (bool_field(doc, "clientSideOnly")) m["environment"] = "client";
    auto mods = doc.value("mods", Json::array());
    bounded_array(mods, source, 1024);
    if (mods.empty()) invalid("Forge 元数据缺少 mods 声明。");
    auto properties = doc.value("properties", Json::object());
    if (!properties.is_object()) invalid("Forge 文件属性表无效。");
    for (auto &mod : mods) {
        auto id = string_field(mod, "modId");
        auto version = substitute_version(string_field(mod, "version", "1"), properties, manifest, m, source);
        if (m["provided_versions"].contains(id)) invalid("Forge 文件包含重复的模组 ID。");
        provide(m, id, version);
        if (!m.contains("id")) {
            m["id"] = id; m["version"] = version; m["name"] = string_field(mod, "displayName", id);
            m["icon_path"] = string_field(mod, "logoFile");
        }
    }
    auto dependencies = doc.value("dependencies", Json::object());
    if (!dependencies.is_object()) invalid("Forge 依赖表无效。");
    m["server_dependencies"] = Json::array();
    m["orphan_dependencies"] = Json::object();
    for (auto &owner : dependencies.items()) {
        bounded_array(owner.value(), source);
        if (!m["provided_versions"].contains(owner.key())) {
            warning(m, "unknown_dependency_owner", "依赖表引用了本 JAR 未声明的模组。", source + ":" + owner.key());
            m["orphan_dependencies"][owner.key()] = owner.value();
            continue;
        }
        size_t n = 0;
        for (auto &dep : owner.value()) {
            auto where = source + ":dependencies." + owner.key() + "[" + std::to_string(n++) + "]";
            auto id = string_field(dep, "modId"), type = string_field(dep, "type");
            auto version_range = string_field(dep, "versionRange", "*");
            auto side = string_field(dep, "side", "BOTH");
            if (id.empty()) invalid("Forge 依赖缺少 modId。");
            if (side == "SERVER") { m["server_dependencies"].push_back(dep); continue; }
            if (side != "BOTH" && side != "CLIENT")
                warning(m, "unsupported_dependency_side", "无法识别 Forge 依赖的运行端。", where);
            bool mandatory = bool_field(dep, "mandatory", true);
            if (type.empty()) type = mandatory ? "required" : "optional";
            if (type != "required" && type != "optional" && type != "incompatible" && type != "discouraged") {
                warning(m, "unsupported_dependency_type", "无法识别 Forge 依赖类型：" + type, where); continue;
            }
            if (loader == "forge" && dep.contains("type"))
                warning(m, "nonstandard_dependency_type", "Forge 依赖使用 NeoForge 类型字段，需核对加载器。", where);
            auto expression = atom(id, range_value(version_range, "maven", m, where), "maven");
            if (type == "optional") expression["optional"] = true;
            if (dep.contains("reason")) expression["reason"] = string_field(dep, "reason");
            auto kind = type == "incompatible" ? "breaks" : type == "discouraged" ? "conflicts" : "depends";
            clause(m, kind, expression, where, owner.key(), side);
            auto ordering = string_field(dep, "ordering", "NONE");
            if (ordering != "NONE") {
                m["dependency_clauses"].back()["ordering"] = ordering;
                warning(m, "dependency_ordering", "依赖加载顺序约束尚未核对，需确认是否存在循环。", where);
            }
            static const std::set<std::string> known{"modId","type","mandatory","versionRange","side","reason","ordering","referralUrl"};
            for (auto &field : dep.items()) if (!known.count(field.key()))
                warning(m, "unsupported_dependency_field", "依赖含未支持的字段：" + field.key(), where);
        }
    }
    auto features = doc.value("features", Json::object());
    if (!features.is_object()) invalid("Forge 功能需求表无效。");
    for (auto &owner : features.items()) {
        if (!owner.value().is_object()) invalid("Forge 功能需求表无效。");
        for (auto &feature : owner.value().items()) {
            if (feature.key() == "javaVersion" && feature.value().is_string())
                clause(m, "depends", atom("java", feature.value(), "maven"), source + ":features", owner.key());
            else warning(m, "unsupported_feature", "模组功能需求尚未核对：" + feature.key(), source + ":features");
        }
    }
    // These are data consumed by executable transformers or loader plugins, not
    // dependency declarations. Their presence must not imply runtime validation.
    if (doc.contains("mixins")) for (auto &mixin : doc["mixins"])
        if (mixin.is_object() && mixin.contains("behaviorVersion"))
            warning(m, "mixin_behavior_version", "Mixin 行为版本尚未核对。", source + ":mixins");
    static const std::set<std::string> fields{"modLoader","loaderVersion","license","issueTrackerURL",
        "showAsResourcePack","showAsDataPack","clientSideOnly","services","properties","mods",
        "dependencies","features","modproperties","accessTransformers","mixins"};
    for (auto &field : doc.items()) if (!fields.count(field.key()))
        warning(m, "unsupported_loader_field", "Forge 加载元数据含未支持的字段：" + field.key(), source);
    return m;
}

Json inspect_bytes(const std::string &, const std::string &, const std::string &, int, ScanBudget &);
bool accepts_nested(const std::string &loader, const Json &child) {
    auto other = child.value("loader", "");
    return loader == other || (loader == "quilt" && other == "fabric");
}
void add_component(Json &m, const Json &child) {
    m["components"].push_back(child);
    for (auto &id : child.value("ids", Json::array())) {
        auto key = id.get<std::string>();
        auto version = child.value("provided_versions", Json::object()).value(key, child.value("version", ""));
        if (!m["provided_versions"].contains(key)) {
            m["ids"].push_back(key); m["provided_versions"][key] = version;
            m["embedded_hashes"][key] = child.at("sha512");
        }
    }
}
std::string nested_bytes(const std::string &bytes, const std::string &name, ScanBudget &budget) {
    if (name.empty() || name.front() == '/' || name.find('\\') != name.npos || name.find(':') != name.npos)
        invalid("内嵌 JAR 路径无效。");
    for (const auto &part : fs::u8path(name)) if (part == ".." || part == ".") invalid("内嵌 JAR 路径无效。");
    if (++budget.count > nested_count_limit) throw Error("mods", "nested_limit", "内嵌模组数量过多。");
    auto child = zip_entry_bytes(bytes, name, nested_jar_limit);
    if (child.empty()) invalid("声明的内嵌 JAR 不存在或为空：" + name);
    budget.bytes += child.size();
    if (budget.bytes > nested_total_limit) throw Error("mods", "nested_limit", "内嵌模组总大小超过限制。");
    return child;
}
void nested_metadata(Json &m, const std::string &bytes, int depth, ScanBudget &budget) {
    auto loader = m["loader"].get<std::string>();
    auto jars = m.value("nested_jars", Json::array());
    m.erase("nested_jars");
    std::set<std::string> visited;
    for (auto &jar : jars) {
        auto path = jar.get<std::string>();
        if (!visited.insert(path).second) invalid("内嵌 JAR 声明重复。");
        auto raw = nested_bytes(bytes, path, budget);
        auto child = inspect_bytes(raw, path, loader, depth + 1, budget);
        if (!child.value("known", false)) invalid("内嵌模组元数据无法识别：" + path + " " + child.value("problem", ""));
        if (!accepts_nested(loader, child)) {
            warning(m, "foreign_nested_metadata", "内嵌 JAR 只有其他加载器的模组声明，未将其视为当前加载器的依赖提供者。", path);
            m["ignored_components"].push_back(child);
            continue;
        }
        add_component(m, child);
    }
    if (loader != "forge" && loader != "neoforge") return;
    auto text = zip_entry_bytes(bytes, "META-INF/jarjar/metadata.json", metadata_limit);
    if (text.empty()) return;
    auto doc = Json::parse(text);
    auto libraries = doc.at("jars");
    bounded_array(libraries, "META-INF/jarjar/metadata.json", nested_count_limit);
    for (auto &field : doc.items()) if (field.key() != "jars")
        warning(m, "unsupported_jarjar_field", "JarJar 元数据含未支持的字段：" + field.key(), "META-INF/jarjar/metadata.json");
    m["jarjar_libraries"] = Json::array();
    for (auto &lib : libraries) {
        auto path = string_field(lib, "path");
        if (!visited.insert(path).second) invalid("JarJar 内嵌 JAR 声明重复。");
        auto identity = lib.at("identifier"), version = lib.at("version");
        Json record{{"group", string_field(identity, "group")}, {"artifact", string_field(identity, "artifact")},
                    {"range", string_field(version, "range")}, {"version", string_field(version, "artifactVersion")},
                    {"path", path}, {"is_obfuscated", bool_field(lib, "isObfuscated")}};
        if (record["group"] == "" || record["artifact"] == "" || record["range"] == "" || record["version"] == "")
            invalid("JarJar 元数据缺少组件标识或版本。");
        static const std::set<std::string> jar_fields{"identifier", "version", "path", "isObfuscated"};
        for (auto &field : lib.items()) if (!jar_fields.count(field.key()))
            warning(m, "unsupported_jarjar_field", "JarJar 组件含未支持的字段：" + field.key(), path);
        auto raw = nested_bytes(bytes, path, budget);
        record["sha512"] = hash_bytes(raw);
        auto child = inspect_bytes(raw, path, loader, depth + 1, budget);
        if (!child.value("valid_archive", false)) invalid("JarJar 内嵌 JAR 损坏：" + path);
        if (child.value("known", false) && !accepts_nested(loader, child)) {
            warning(m, "foreign_nested_metadata", "JarJar 组件只有其他加载器的模组声明，未将其视为当前加载器的依赖提供者。", path);
            m["ignored_components"].push_back(child);
            m["jarjar_libraries"].push_back(record);
            continue;
        }
        if (!child.value("known", false)) {
            if (!child.value("metadata", Json::object()).empty() || child.value("has_metadata", false))
                invalid("JarJar 内嵌模组元数据无效：" + path);
            // JarJar also packages plain libraries, which have no mod ID.
            child["library"] = true; child["known"] = true; child["ids"] = Json::array();
            child["own_ids"] = Json::array(); child["own_provided_versions"] = Json::object();
            child["version"] = record["version"]; child["problem"] = "";
            // A library can itself carry JarJar children.
            auto container = metadata(loader, path);
            nested_metadata(container, raw, depth + 1, budget);
            for (auto field : {"components", "metadata_warnings", "jarjar_libraries", "ids", "provided_versions", "embedded_hashes"})
                if (container.contains(field)) child[field] = container[field];
        }
        child["jarjar"] = record;
        m["jarjar_libraries"].push_back(record);
        add_component(m, child);
    }
    if (!libraries.empty()) warning(m, "jarjar_selection", "JarJar 的跨 JAR 版本协商尚未完整模拟，内嵌候选已静态检查。", "META-INF/jarjar/metadata.json");
}
Json inspect_bytes(const std::string &bytes, const std::string &file,
                   const std::string &preferred, int depth, ScanBudget &budget) {
    Json result{{"filename", file}, {"name", file},
                {"enabled", file.size() < 9 || file.substr(file.size() - 9) != ".disabled"},
                {"known", false}, {"valid_archive", false}, {"sha512", hash_bytes(bytes)},
                {"metadata", Json::object()}, {"metadata_warnings", Json::array()}, {"problem", ""}};
    try {
        if (depth > 4) throw Error("mods", "nested_limit", "内嵌模组层级过深。");
        auto entries = zip_entries(bytes);
        result["valid_archive"] = true;
        std::set<std::string> names;
        for (auto &entry : entries) if (!names.insert(entry.name).second)
            invalid("压缩包包含重复路径，无法可靠读取模组元数据。");
        auto manifest = manifest_main(zip_entry_bytes(bytes, "META-INF/MANIFEST.MF", metadata_limit));
        Json failures = Json::array();
        for (auto &entry : std::vector<std::pair<std::string, std::string>>{
            {"fabric", "fabric.mod.json"}, {"quilt", "quilt.mod.json"},
            {"forge", "META-INF/mods.toml"}, {"neoforge", "META-INF/neoforge.mods.toml"}}) {
            if (!names.count(entry.second)) continue;
            result["has_metadata"] = true;
            try {
                auto text = zip_entry_bytes(bytes, entry.second, metadata_limit);
                if (text.empty()) invalid("模组元数据文件为空。");
                auto m = entry.first == "fabric" || entry.first == "quilt"
                    ? json_metadata(text, entry.first, entry.second)
                    : forge_metadata(text, entry.first, entry.second, manifest);
                if (m.value("id", "").empty() || m.value("version", "").empty()) invalid("模组缺少 ID 或版本。");
                m["own_ids"] = m["ids"];
                m["own_provided_versions"] = m["provided_versions"];
                if (m["loader"] == "fabric") m["load_type"] = depth == 0 ? "always" : "if_possible";
                else if (m["loader"] == "quilt" && m.value("load_type", "").empty())
                    m["load_type"] = depth == 0 ? "always" : "if_required";
                else if (m["loader"] == "forge" || m["loader"] == "neoforge") m["load_type"] = "always";
                nested_metadata(m, bytes, depth, budget);
                if (m.value("name", "").empty()) m["name"] = m["id"];
                m["known"] = true;
                auto loader = m["loader"].get<std::string>();
                if (result["metadata"].contains(loader)) {
                    warning(m, "multiple_loader_descriptors", "同一加载器存在多个元数据文件，需核对加载器实际选择。", entry.second);
                }
                result["metadata"][loader] = std::move(m);
            } catch (const std::exception &e) {
                failures.push_back({{"code", "invalid_loader_metadata"}, {"message", e.what()}, {"source", entry.second}});
            }
        }
        auto all = result["metadata"];
        std::string chosen = all.contains(preferred) ? preferred : all.empty() ? "" : all.begin().key();
        if (!chosen.empty()) {
            for (auto &entry : all[chosen].items()) result[entry.key()] = entry.value();
            for (auto &failure : failures) result["metadata_warnings"].push_back(failure);
            result["known"] = true;
        } else if (!failures.empty()) {
            result["problem"] = failures.front()["message"];
            result["metadata_warnings"] = failures;
        } else {
            auto transformer = zip_entry_bytes(bytes, "optifine/OptiFineClassTransformer.class");
            if (transformer.size() > 4 && transformer.substr(0, 4) == std::string("\xca\xfe\xba\xbe", 4)) {
                result["id"] = "optifine"; result["ids"] = Json::array({"optifine"});
                result["name"] = "OptiFine"; result["loader"] = "optifine";
                result["version"] = "unknown"; result["known"] = true;
            } else result["problem"] = "无法识别模组元数据，需手动确认兼容性。";
        }
    } catch (const std::exception &e) { result["known"] = false; result["problem"] = e.what(); }
    return result;
}
} // namespace
Json inspect_mod_bytes(const std::string &bytes, const std::string &file,
                       const std::string &preferred, int depth) {
    ScanBudget budget;
    return inspect_bytes(bytes, file, preferred, depth, budget);
}
} // namespace solar::engine
