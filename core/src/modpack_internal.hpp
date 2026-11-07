// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "solar/engine.hpp"

namespace solar::engine {
std::string curseforge_modpack_destination(const std::string &kind, const std::string &filename);
void validate_curseforge_modpack_paths(const Json &files, const Json &overrides);
} // namespace solar::engine
