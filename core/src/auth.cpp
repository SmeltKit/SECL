// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/core.hpp"
#include <curl/curl.h>
#include <memory>
#include <regex>
namespace solar {
Error::Error(std::string s, std::string c, std::string m, int h, std::string u)
    : std::runtime_error(std::move(m)), stage(std::move(s)), code(std::move(c)), url(std::move(u)),
      status(h) {}
Json Error::json() const {
    return {{"stage", stage},
            {"code", code},
            {"message", what()},
            {"http_status", status},
            {"url", url}};
}
std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string url_encode(const std::string &s) {
    static const char *hex = "0123456789ABCDEF";
    std::string r;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            r += char(c);
        else {
            r += '%';
            r += hex[c >> 4];
            r += hex[c & 15];
        }
    }
    return r;
}
std::string redact(std::string s, const std::vector<std::string> &secrets) {
    for (const auto &v : secrets)
        if (!v.empty()) {
            size_t p = 0;
            while ((p = s.find(v, p)) != std::string::npos) {
                s.replace(p, v.size(), "[REDACTED]");
                p += 10;
            }
        }
    return s;
}
HttpResponse https_request(const std::string &url, const std::string &body,
                           const std::string &bearer, const std::string &content_type) {
    static const int init = []() { return curl_global_init(CURL_GLOBAL_DEFAULT); }();
    if (init != CURLE_OK || url.rfind("https://", 0) != 0)
        throw Error("network", "https_required", "无法初始化安全连接。");
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
    if (!handle)
        throw Error("network", "curl_init", "无法初始化网络。");
    auto c = handle.get();
    HttpResponse result;
    char details[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, details);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "SECL/0.1.0");
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "x-xbl-contract-version: 1");
    if (!bearer.empty())
        headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    if (!body.empty())
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_guard(headers,
                                                                             curl_slist_free_all);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, long(body.size()));
    }
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(
        c, CURLOPT_WRITEFUNCTION, +[](char *p, size_t a, size_t b, void *ctx) -> size_t {
            auto &s = *static_cast<std::string *>(ctx);
            const auto n = a * b;
            if (n > 1024 * 1024 || s.size() > 1024 * 1024 - n)
                return 0;
            s.append(p, n);
            return n;
        });
    const auto error = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    result.status = int(status);
    if (error != CURLE_OK)
        throw Error("network", "curl_" + std::to_string(error),
                    error == CURLE_OPERATION_TIMEDOUT
                        ? "连接或请求超时，请手动重试。"
                        : std::string("网络请求失败：") + curl_easy_strerror(error),
                    result.status, url);
    return result;
}
static std::string decode_url(std::string s) {
    std::string r;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+')
            r += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) {
            auto h = [](char c) {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            };
            int a = h(s[i + 1]), b = h(s[i + 2]);
            if (a < 0 || b < 0)
                return {};
            r += char(a * 16 + b);
            i += 2;
        } else
            r += s[i];
    }
    return r;
}
std::string callback_code(const std::string &request, const std::string &expected) {
    const auto end = request.find("\r\n");
    if (end == request.npos || request.rfind("GET /oauth/callback?", 0) != 0)
        throw Error("callback", "invalid_callback", "登录回调格式无效。");
    auto space = request.find(' ', 4);
    if (space == request.npos || request.substr(space + 1, end - space - 1) != "HTTP/1.1")
        throw Error("callback", "invalid_callback", "登录回调格式无效。");
    auto target = request.substr(4, space - 4);
    auto query = target.substr(target.find('?') + 1);
    std::map<std::string, std::string> fields;
    size_t p = 0;
    while (p <= query.size()) {
        auto next = query.find('&', p);
        auto part = query.substr(p, next == query.npos ? next : next - p);
        auto eq = part.find('=');
        auto key = decode_url(part.substr(0, eq));
        if (fields.count(key))
            throw Error("callback", "duplicate_parameter", "登录回调包含重复参数。");
        fields[key] = eq == part.npos ? "" : decode_url(part.substr(eq + 1));
        if (next == query.npos)
            break;
        p = next + 1;
    }
    auto state = fields["state"];
    unsigned difference = unsigned(state.size() ^ expected.size());
    for (size_t n = 0; n < expected.size(); ++n)
        difference |= unsigned((n < state.size() ? state[n] : 0) ^ expected[n]);
    if (difference)
        throw Error("callback", "state_mismatch", "登录回调与当前会话不匹配。");
    if (fields.count("error"))
        throw Error("callback", "authorization_denied", "微软授权已取消或被拒绝，请重新登录。");
    auto code = fields["code"];
    if (code.empty() || code.size() > 32768 || code.find_first_of("\r\n \t\0", 0, 5) != code.npos)
        throw Error("callback", "invalid_code", "登录回调没有有效授权码。");
    return code;
}
static bool token_ok(const std::string &s) {
    return !s.empty() && s.size() <= 32768 &&
           s.find_first_of(" \r\n\t\0", 0, 5) == std::string::npos;
}
static std::string safe_service_code(const Json &o) {
    if (o.contains("XErr") && o["XErr"].is_number_unsigned())
        return "XErr_" + std::to_string(o["XErr"].get<std::uint64_t>());
    auto c = o.value("error", "");
    return std::regex_match(c, std::regex("[A-Za-z0-9_.-]{1,80}")) ? c : "service_error";
}
static Identity authenticate_impl(const std::string &client, const std::string &redirect,
                                  const std::string &verifier, const std::string &code,
                                  const Transport &transport,
                                  const std::function<void(std::string)> &progress, bool refresh) {
    std::vector<std::string> secrets{code, verifier};
    auto request = [&](const std::string &stage, const std::string &url, const std::string &body,
                       const std::string &bearer = "",
                       const std::string &type = "application/json") {
        progress(stage);
        HttpResponse response;
        try {
            response = transport(url, body, bearer, type);
        } catch (const Error &e) {
            throw Error(stage, e.code, redact(e.what(), secrets), e.status, url);
        }
        Json o = Json::parse(response.body, nullptr, false);
        if (response.status < 200 || response.status >= 300) {
            std::string hint = "服务拒绝了请求。";
            auto service = o.is_object() ? safe_service_code(o) : "invalid_response";
            if (response.status == 403)
                hint = stage == "minecraft" ? "Minecraft Services 拒绝访问，请核对本应用的 API "
                                              "授权和账号状态。不会替换为其他启动器的应用身份。"
                                            : "服务拒绝访问，请核对账号与应用权限。";
            if (response.status == 429)
                hint = "请求过于频繁，请稍后手动重试。";
            if (response.status >= 500)
                hint = "服务暂时不可用，请稍后手动重试。";
            if (service == "invalid_grant")
                hint = "登录会话失效，请重新登录。";
            if (service == "invalid_client" || service == "unauthorized_client")
                hint = "应用注册配置无效，请检查 SECL 的 Client ID 和回调地址。";
            throw Error(stage, service, hint, response.status, url);
        }
        if (!o.is_object())
            throw Error(stage, "invalid_response", "服务返回了无效的 JSON 数据。", response.status,
                        url);
        return o;
    };
    auto required = [&](const Json &o, const char *key, const char *stage) {
        auto s = o.value(key, "");
        if (!token_ok(s))
            throw Error(stage, "invalid_response", "服务返回的身份数据不完整。");
        secrets.push_back(s);
        return s;
    };
    auto grant =
        request("microsoft", "https://login.microsoftonline.com/consumers/oauth2/v2.0/token",
                "client_id=" + url_encode(client) +
                    (refresh ? "&grant_type=refresh_token&refresh_token=" + url_encode(code)
                             : "&grant_type=authorization_code&code=" + url_encode(code) +
                                   "&redirect_uri=" + url_encode(redirect) +
                                   "&code_verifier=" + url_encode(verifier)) +
                    "&scope=" + url_encode("XboxLive.signin"),
                "", "application/x-www-form-urlencoded");
    auto access = required(grant, "access_token", "microsoft");
    auto xbox = request("xbox", "https://user.auth.xboxlive.com/user/authenticate",
                        Json{{"Properties",
                              {{"AuthMethod", "RPS"},
                               {"SiteName", "user.auth.xboxlive.com"},
                               {"RpsTicket", "d=" + access}}},
                             {"RelyingParty", "http://auth.xboxlive.com"},
                             {"TokenType", "JWT"}}
                            .dump());
    auto xtoken = required(xbox, "Token", "xbox");
    auto users = xbox.value("DisplayClaims", Json::object()).value("xui", Json::array());
    if (!users.is_array() || users.empty() || !users[0].is_object())
        throw Error("xbox", "invalid_claims", "Xbox 未返回玩家身份。");
    auto uhs = required(users[0], "uhs", "xbox");
    auto xsts = request(
        "xsts", "https://xsts.auth.xboxlive.com/xsts/authorize",
        Json{{"Properties", {{"SandboxId", "RETAIL"}, {"UserTokens", Json::array({xtoken})}}},
             {"RelyingParty", "rp://api.minecraftservices.com/"},
             {"TokenType", "JWT"}}
            .dump());
    auto xstoken = required(xsts, "Token", "xsts");
    auto claims = xsts.value("DisplayClaims", Json::object()).value("xui", Json::array());
    if (!claims.is_array() || claims.empty() || claims[0].value("uhs", "") != uhs)
        throw Error("xsts", "identity_mismatch", "Xbox 返回的身份不一致。");
    auto mc = request(
        "minecraft", "https://api.minecraftservices.com/launcher/login",
        Json{{"xtoken", "XBL3.0 x=" + uhs + ";" + xstoken}, {"platform", "PC_LAUNCHER"}}.dump());
    Identity result;
    result.token = required(mc, "access_token", "minecraft");
    auto expiry = mc.value("expires_in", 0LL);
    if (expiry < 60 || expiry > 604800)
        throw Error("minecraft", "invalid_expiry", "Minecraft 凭据有效期无效。");
    result.expires = now() + expiry;
    auto license = request("ownership",
                           "https://api.minecraftservices.com/entitlements/license?requestId=" +
                               random_url_token(),
                           "", result.token);
    for (auto &item : license.value("items", Json::array()))
        if (item.is_object() && item.value("name", "") == "game_minecraft")
            result.owned = true;
    if (!result.owned)
        throw Error("ownership", "not_owned",
                    "未检测到 Minecraft: Java Edition "
                    "游戏资格，请在官网核对购买或订阅状态。");
    auto profile =
        request("profile", "https://api.minecraftservices.com/minecraft/profile", "", result.token);
    result.name = profile.value("name", "");
    result.uuid = profile.value("id", "");
    result.xuid = claims[0].value("xid", "");
    result.client_id = client;
    if (!std::regex_match(result.uuid, std::regex("[0-9a-fA-F]{32}")) ||
        !std::regex_match(result.name, std::regex("[A-Za-z0-9_]{1,16}")))
        throw Error("profile", "invalid_profile", "Minecraft 角色信息无效，无法启动游戏。");
    return result;
}
Identity authenticate_code(const std::string &client, const std::string &redirect,
                           const std::string &verifier, const std::string &code, const Transport &t,
                           const std::function<void(std::string)> &progress) {
    return authenticate_impl(client, redirect, verifier, code, t, progress, false);
}
Identity authenticate_refresh(const std::string &client, const std::string &refresh,
                              const Transport &t,
                              const std::function<void(std::string)> &progress) {
    return authenticate_impl(client, "", "", refresh, t, progress, true);
}

} // namespace solar
