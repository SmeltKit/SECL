// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "solar/engine.hpp"
#include <map>

namespace solar::engine::resume {
using HeaderMap = std::map<std::string, std::string>;
struct Request {
    std::string url;
    std::int64_t offset = 0;
    std::string if_range;
};
struct Response {
    int status = 0;
    HeaderMap headers;
    std::string location;
};
using HeaderSink = std::function<void(const Response &)>;
using BodySink = std::function<void(const char *, std::size_t)>;
// A streaming native-only seam. Headers precede body chunks; transport failures
// may throw after delivering a prefix. It is never populated from an FFI request.
using Exchange = std::function<Response(const Request &, const HeaderSink &, const BodySink &)>;
using UrlPolicy = std::function<void(const std::string &)>;
Exchange curl_exchange(const Task &task, bool loopback_test = false);
void transfer(const Json &descriptor, const fs::path &target, const Task &task,
              const std::vector<std::string> &candidates, const UrlPolicy &, const Exchange &);
// Native tests can use an explicit cache root and maintenance time. Production
// maintenance is rate limited, skips OS-locked entries, and never touches targets.
void prune_cache(const fs::path &root, const std::string &active_key = {},
                 std::int64_t time = 0, std::uint64_t budget = 2ULL * 1024 * 1024 * 1024);
} // namespace solar::engine::resume
