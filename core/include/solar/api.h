// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#pragma once
#if defined(_WIN32)
#define SOLAR_API __declspec(dllexport)
#else
#define SOLAR_API __attribute__((visibility("default"))) __attribute__((used))
#endif
#ifdef __cplusplus
extern "C" {
#endif
// UTF-8 JSON in/out. The caller frees each returned allocation with solar_free.
// No credentials or raw game command are returned over this ABI.
SOLAR_API char *solar_command(const char *request_json);
SOLAR_API void solar_free(char *response);
SOLAR_API unsigned int solar_abi_version(void);
#ifdef __cplusplus
}
#endif
