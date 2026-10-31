// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/accounts.hpp"
#include "install_internal.hpp"
#include "tinyxml2.h"
#include <ctime>
#include <iomanip>
#include <sstream>
namespace solar {
static Json checked_accounts(const Json &accounts, const std::string &selected) {
    if (!accounts.is_array() || accounts.size() > 100)
        throw Error("accounts", "invalid_accounts", "账号列表格式无效。");
    Json out = Json::array();
    std::set<std::string> seen;
    for (auto &a : accounts) {
        auto uuid = a.value("uuid", "");
        auto id = a.value("id", "");
        if (a.value("type", "") != "microsoft" ||
            !std::regex_match(uuid, std::regex("[0-9a-f]{32}")) || id != "microsoft:" + uuid ||
            !std::regex_match(a.value("name", ""), std::regex("[A-Za-z0-9_]{1,16}")) ||
            !std::regex_match(a.value("client_id", a.value("clientId", "")),
                              std::regex("[a-fA-F0-9]{8}(-[a-fA-F0-9]{4}){3}-[a-fA-F0-9]{12}")) ||
            !seen.insert(id).second)
            throw Error("accounts", "invalid_account", "账号记录无效或重复。");
        out.push_back({{"id", id},
                       {"type", "microsoft"},
                       {"uuid", uuid},
                       {"name", a.at("name")},
                       {"client_id", a.value("client_id", a.value("clientId", ""))},
                       {"needs_login", true},
                       {"remembered", a.value("remembered", false)},
                       {"skin_url", a.value("skin_url", a.value("skinUrl", ""))}});
    }
    if (!selected.empty() && !seen.count(selected))
        throw Error("accounts", "invalid_selection", "选中的账号不存在。");
    return {{"schema_version", 1}, {"accounts", out}, {"selected", selected}};
}
Json load_accounts(const fs::path &directory) {
    auto p = directory / "accounts.json";
    if (!fs::exists(p))
        return {{"schema_version", 1}, {"accounts", Json::array()}, {"selected", ""}};
    auto j = engine::read_json(p);
    if (j.value("schema_version", j.value("schemaVersion", 0)) != 1)
        throw Error("accounts", "invalid_schema", "账号列表格式无效，原文件未覆盖。");
    return checked_accounts(j.at("accounts"), j.value("selected", ""));
}
void save_accounts(const fs::path &directory, const Json &accounts, const std::string &selected) {
    auto j = checked_accounts(accounts, selected);
    engine::detail::Lock lock(directory);
    engine::atomic_write(directory / "accounts.json", j.dump(2));
    fs::permissions(directory / "accounts.json", fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace);
}
std::string credential_namespace(const fs::path &directory) {
    return "io.smeltkit.solar.accounts." +
           engine::hash_bytes(fs::weakly_canonical(directory).u8string(), "sha256");
}
SecretRedactor::SecretRedactor(std::vector<std::string> values) {
    for (auto &s : values)
        if (!s.empty()) {
            longest_ = std::max(longest_, s.size());
            secrets_.push_back(s);
        }
}
std::string SecretRedactor::append(const std::string &bytes, bool finish) {
    pending_ += bytes;
    std::string out;
    while (!pending_.empty()) {
        auto safe = finish                        ? pending_.size()
                    : pending_.size() >= longest_ ? pending_.size() - longest_ + 1
                                                  : 0;
        if (!safe)
            break;
        size_t first = pending_.npos, length = 0;
        for (auto &s : secrets_) {
            auto at = pending_.find(s);
            if (at < first || (at == first && s.size() > length)) {
                first = at;
                length = s.size();
            }
        }
        if (first != pending_.npos && first < safe) {
            out += pending_.substr(0, first) + "[REDACTED]";
            pending_.erase(0, first + length);
        } else {
            out += pending_.substr(0, safe);
            pending_.erase(0, safe);
        }
    }
    return out;
}
std::string GameLogDecoder::append(const std::string &bytes, bool finish) {
    pending_ += bytes;
    std::string out;
    for (;;) {
        auto event = pending_.find("<log4j:Event"), newline = pending_.find('\n');
        if (event != pending_.npos && (newline == pending_.npos || event < newline)) {
            if (event) {
                out += pending_.substr(0, event);
                pending_.erase(0, event);
            }
            auto end = pending_.find("</log4j:Event>");
            if (end == pending_.npos)
                break;
            auto fragment = pending_.substr(0, end + 14);
            pending_.erase(0, end + 14);
            tinyxml2::XMLDocument xml;
            if (xml.Parse(fragment.c_str(), fragment.size()) != tinyxml2::XML_SUCCESS) {
                out += fragment + '\n';
                continue;
            }
            auto root = xml.RootElement();
            auto message = root ? root->FirstChildElement("log4j:Message") : nullptr;
            auto throwable = root ? root->FirstChildElement("log4j:Throwable") : nullptr;
            auto attr = [&](const char *key) {
                auto v = root ? root->Attribute(key) : nullptr;
                return v ? std::string(v) : "";
            };
            out += '[' + attr("thread") + '/' + attr("level") + "] " +
                   (message && message->GetText() ? message->GetText() : "") + '\n';
            if (throwable && throwable->GetText())
                out += std::string(throwable->GetText()) + '\n';
        } else {
            if (newline == pending_.npos)
                break;
            out += pending_.substr(0, newline + 1);
            pending_.erase(0, newline + 1);
        }
    }
    if (finish || pending_.size() > 1024 * 1024) {
        out += pending_;
        pending_.clear();
    }
    return out;
}
} // namespace solar
