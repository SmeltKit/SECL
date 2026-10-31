// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#pragma once
#include "json.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace solar {
using Json = nlohmann::json;
namespace fs = std::filesystem;
struct Error : std::runtime_error {
    std::string stage, code, url;
    int status;
    Error(std::string stage, std::string code, std::string message, int status = 0,
          std::string url = "");
    Json json() const;
};
struct HttpResponse {
    int status = 0;
    std::string body;
};
using Transport = std::function<HttpResponse(const std::string &, const std::string &,
                                             const std::string &, const std::string &)>;
struct Identity {
    std::string name, uuid, token, xuid, client_id;
    std::int64_t expires = 0;
    bool owned = false;
};
struct Platform {
    std::string os, arch, version;
};
struct Command {
    fs::path executable, directory;
    std::vector<std::string> arguments;
};
std::int64_t now();
std::string read_file(const fs::path &, std::size_t limit = 16 * 1024 * 1024);
std::string random_url_token();
std::string sha256_base64url(const std::string &);
std::string url_encode(const std::string &);
std::string redact(std::string, const std::vector<std::string> &);
HttpResponse https_request(const std::string &, const std::string &, const std::string &,
                           const std::string &);
std::string callback_code(const std::string &, const std::string &);
Identity authenticate_code(const std::string &client, const std::string &redirect,
                           const std::string &verifier, const std::string &code, const Transport &,
                           const std::function<void(std::string)> &);
Identity authenticate_refresh(const std::string &client, const std::string &refresh,
                              const Transport &, const std::function<void(std::string)> &);
bool rules_allow(const Json &, const Platform &);
std::vector<std::string> split_arguments(const std::string &);
Json detect_javas(const std::string &extra = "");
fs::path java_runtime_executable(const fs::path &, const std::string &properties);
Json scan_versions(const fs::path &);
Json load_version(const fs::path &, const std::string &, int depth = 0);
Command launch_command(const fs::path &, const std::string &, const fs::path &, const Identity &,
                       const Platform &);
void extract_natives(const fs::path &, const fs::path &);
Platform platform();
std::string run_capture(const fs::path &, const std::vector<std::string> &, int timeout_ms = 2500);
std::int64_t spawn_game(const Command &);
int game_exit(std::int64_t);
void terminate_process(std::int64_t);
void move_to_trash(const fs::path &);
void open_browser(const std::string &);
std::string default_game_root();
std::string user_data_root();
} // namespace solar
