// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#pragma once
#include "core.hpp"
namespace solar {
// Only Microsoft account metadata is persisted. Tokens belong in the OS vault.
Json load_accounts(const fs::path &directory);
void save_accounts(const fs::path &directory, const Json &accounts, const std::string &selected);
std::string credential_namespace(const fs::path &directory);
class CredentialVault {
    std::string service_;
    std::string operation(const std::string &key, const std::string &secret, int kind) const;

  public:
    explicit CredentialVault(std::string service) : service_(std::move(service)) {}
    std::string read(const std::string &key) const { return operation(key, "", 0); }
    void write(const std::string &key, const std::string &secret) const {
        operation(key, secret, 1);
    }
    void remove(const std::string &key) const { operation(key, "", 2); }
};
class SecretRedactor {
    std::vector<std::string> secrets_;
    std::string pending_;
    size_t longest_ = 1;

  public:
    explicit SecretRedactor(std::vector<std::string>);
    std::string append(const std::string &, bool finish = false);
};
class GameLogDecoder {
    std::string pending_;

  public:
    std::string append(const std::string &, bool finish = false);
};
} // namespace solar
