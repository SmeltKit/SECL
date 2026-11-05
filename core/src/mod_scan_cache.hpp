// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "solar/engine.hpp"
namespace solar::engine {
// Internal, process-local cache. Mutation callers must leave use_cache false.
Json scan_mod_file(const fs::path &, const std::string &loader, bool use_cache);
Json mod_scan_cache_stats();
void clear_mod_scan_cache();
}
