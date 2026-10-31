// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/core.hpp"
#include <fstream>
#include <iostream>
#include <set>
using namespace solar;
static int checks = 0;
static void require(bool ok, const std::string &label) {
    ++checks;
    if (!ok)
        throw std::runtime_error("FAILED: " + label);
}
static void fails(const std::function<void()> &f, const std::string &code) {
    try {
        f();
        throw std::runtime_error("Expected failure: " + code);
    } catch (const Error &e) {
        require(e.code == code, "error code " + code);
    }
}
int main() {
    try {
        require(sha256_base64url("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
                    "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
                "RFC 7636 PKCE S256");
        auto state = random_url_token();
        require(state.size() == 43 && state != random_url_token(), "cryptographic random state");
        auto req = "GET /oauth/callback?state=" + state +
                   "&code=test%2Bcode HTTP/1.1\r\nHost: localhost\r\n\r\n";
        require(callback_code(req, state) == "test+code", "correct callback and percent decoding");
        fails([&] { callback_code(req, "wrong"); }, "state_mismatch");
        fails(
            [&] {
                callback_code("GET /oauth/callback?state=" + state + "&state=" + state +
                                  "&code=x HTTP/1.1\r\n\r\n",
                              state);
            },
            "duplicate_parameter");
        fails(
            [&] {
                callback_code("GET /oauth/callback?state=" + state +
                                  "&error=access_denied HTTP/1.1\r\n\r\n",
                              state);
            },
            "authorization_denied");
        fails(
            [&] { callback_code("GET /other?state=" + state + "&code=x HTTP/1.1\r\n\r\n", state); },
            "invalid_callback");
        require(redact("prefix SECRET SECRET end", {"SECRET"}) ==
                    "prefix [REDACTED] [REDACTED] end",
                "secret redaction");
        std::vector<HttpResponse> responses = {
            {200, R"({"access_token":"MICROSOFT_TOKEN"})"},
            {200, R"({"Token":"XBOX_TOKEN","DisplayClaims":{"xui":[{"uhs":"userhash"}]}})"},
            {200,
             R"({"Token":"XSTS_TOKEN","DisplayClaims":{"xui":[{"uhs":"userhash","xid":"12345"}]}})"},
            {200, R"({"access_token":"MINECRAFT_TOKEN","expires_in":3600})"},
            {200, R"({"items":[{"name":"game_minecraft"}]})"},
            {200, R"({"id":"0123456789abcdef0123456789abcdef","name":"VerifiedPlayer"})"}};
        auto run = [&](std::vector<HttpResponse> data, std::vector<std::string> &steps) {
            size_t i = 0;
            return authenticate_code(
                "own-client", "http://localhost/callback", "VERIFIER", "CODE",
                [&](const std::string &url, const std::string &body, const std::string &bearer,
                    const std::string &) {
                    require(url.rfind("https://", 0) == 0, "HTTPS endpoints only");
                    require(body.find("client_secret") == body.npos, "no client secret");
                    if (i == 0)
                        require(body.find("code_verifier=VERIFIER") != body.npos, "PKCE exchange");
                    if (i == 4 || i == 5)
                        require(bearer == "MINECRAFT_TOKEN", "Minecraft bearer scope");
                    if (i >= data.size())
                        throw std::runtime_error("Unexpected network request");
                    return data[i++];
                },
                [&](std::string s) { steps.push_back(s); });
        };
        std::vector<std::string> steps;
        auto identity = run(responses, steps);
        require(identity.owned && identity.name == "VerifiedPlayer" &&
                    identity.token == "MINECRAFT_TOKEN",
                "profile only succeeds after all checks");
        require(steps == std::vector<std::string>{"microsoft", "xbox", "xsts", "minecraft",
                                                  "ownership", "profile"},
                "authentication stages");
        auto bad = responses;
        bad[4] = {200, R"({"items":[]})"};
        steps.clear();
        fails([&] { run(bad, steps); }, "not_owned");
        require(steps.back() == "ownership", "no profile on missing ownership");
        for (int status : {403, 429, 503}) {
            bad = responses;
            bad[3] = {status, R"({"error":"FORBIDDEN","access_token":"DO_NOT_DISPLAY"})"};
            steps.clear();
            try {
                run(bad, steps);
                throw std::runtime_error("Expected HTTP failure");
            } catch (const Error &e) {
                require(e.status == status && e.stage == "minecraft",
                        "preserve HTTP code and phase");
                require(e.json().dump().find("DO_NOT_DISPLAY") == std::string::npos,
                        "no response token leakage");
                require(steps.size() == 4, "no automatic retries or fallback identity");
            }
        }
        bad = responses;
        bad[2].body = R"({"Token":"XSTS_TOKEN","DisplayClaims":{"xui":[{"uhs":"another"}]}})";
        steps.clear();
        fails([&] { run(bad, steps); }, "identity_mismatch");
        bad = responses;
        bad[5].body = R"({"id":"invalid","name":"Player"})";
        steps.clear();
        fails([&] { run(bad, steps); }, "invalid_profile");
        Platform linux{"linux", "x86_64", "6.0"};
        require(rules_allow(Json::array(), linux), "empty rules allow");
        require(!rules_allow(Json::array({{{"action", "allow"}, {"os", {{"name", "windows"}}}}}),
                             linux),
                "OS rules enforced");
        require(!rules_allow(
                    Json::array({{{"action", "allow"}, {"features", {{"is_demo_user", true}}}}}),
                    linux),
                "no demo mode bypass");
        require(split_arguments("--name \"two words\" --flag") ==
                    std::vector<std::string>{"--name", "two words", "--flag"},
                "legacy argument tokenization");
        fails([] { split_arguments("--x \"broken"); }, "unclosed_quote");
        auto folder = fs::temp_directory_path() / ("solar-test-" + random_url_token());
        fs::create_directories(folder / "versions/v1");
        std::ofstream(folder / "versions/v1/v1.json")
            << R"({"id":"v1","type":"release","mainClass":"net.minecraft.Main"})";
        auto found = scan_versions(folder);
        require(found.size() == 1 && found[0]["id"] == "v1", "detect installed metadata");
        std::ofstream(folder / "versions/v1/v1.json") << R"({"inheritsFrom":"../../escape"})";
        require(scan_versions(folder)[0]["available"] == false,
                "invalid metadata shown, not silently dropped");
        fails([&] { load_version(folder, "../../escape"); }, "invalid_inheritance");
        Identity empty;
        fails([&] { launch_command(folder, "v1", "/not/java", empty, linux); }, "login_required");
        identity.expires = now() - 1;
        fails([&] { launch_command(folder, "v1", "/not/java", identity, linux); },
              "login_required");
        std::ofstream(folder / "truncated.jar") << "not zip";
        fails([&] { extract_natives(folder / "truncated.jar", folder / "natives"); },
              "zip_unsupported");
        fs::remove_all(folder);
        std::cout << "PASS: " << checks
                  << " checks, including PKCE, callbacks, real pipeline contract, "
                     "entitlement gates, HTTP errors, metadata and launch gates.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
