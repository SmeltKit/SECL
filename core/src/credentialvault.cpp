// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/accounts.hpp"
#ifdef __APPLE__
#include <Security/Security.h>
#elif defined(_WIN32)
#include <wincred.h>
#include <windows.h>
#else
#include <libsecret/secret.h>
#include <thread>
#endif
namespace solar {
std::string CredentialVault::operation(const std::string &key, const std::string &secret,
                                       int kind) const {
    if (key.empty() || key.size() > 512 || service_.empty() ||
        (kind == 1 && (secret.empty() || secret.size() > 32768)))
        throw Error("vault", "invalid_credential", "凭据标识或长度无效。");
    std::string result;
#ifdef __APPLE__
    auto string = [](const std::string &s) {
        return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(s.data()),
                                       CFIndex(s.size()), kCFStringEncodingUTF8, false);
    };
    auto service = string(service_), account = string(key);
    if (!service || !account) {
        if (service)
            CFRelease(service);
        if (account)
            CFRelease(account);
        throw Error("vault", "invalid_utf8", "凭据标识编码无效。");
    }
    auto query = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFDictionarySetValue(query, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
    OSStatus status = errSecSuccess;
    if (kind == 0) {
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef value = nullptr;
        status = SecItemCopyMatching(query, &value);
        if (status == errSecSuccess && value && CFGetTypeID(value) == CFDataGetTypeID())
            result.assign(
                reinterpret_cast<const char *>(CFDataGetBytePtr(static_cast<CFDataRef>(value))),
                size_t(CFDataGetLength(static_cast<CFDataRef>(value))));
        if (value)
            CFRelease(value);
    } else if (kind == 2)
        status = SecItemDelete(query);
    else {
        auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(secret.data()),
                                 CFIndex(secret.size()));
        auto update = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                &kCFTypeDictionaryValueCallBacks);
        CFDictionarySetValue(update, kSecValueData, data);
        status = SecItemUpdate(query, update);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, data);
            status = SecItemAdd(query, nullptr);
        }
        CFRelease(update);
        CFRelease(data);
    }
    CFRelease(query);
    CFRelease(service);
    CFRelease(account);
    if (status != errSecSuccess && !(kind == 2 && status == errSecItemNotFound))
        throw Error("vault", "keychain_" + std::to_string(status),
                    "无法访问系统钥匙串；可仅在本次会话中登录。");
#elif defined(_WIN32)
    auto utf8 = service_ + "/" + key;
    auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()),
                                    nullptr, 0);
    if (size <= 0)
        throw Error("vault", "invalid_utf8", "凭据标识编码无效。");
    std::wstring target(size, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), int(utf8.size()), target.data(),
                        size);
    DWORD code = 0;
    if (kind == 0) {
        PCREDENTIALW item = nullptr;
        if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &item)) {
            result.assign(reinterpret_cast<const char *>(item->CredentialBlob),
                          item->CredentialBlobSize);
            CredFree(item);
        } else
            code = GetLastError();
    } else if (kind == 2) {
        if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0))
            code = GetLastError();
        if (code == ERROR_NOT_FOUND)
            code = 0;
    } else {
        if (secret.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            throw Error("vault", "too_large", "凭据超过系统凭据库限制。");
        CREDENTIALW item{};
        item.Type = CRED_TYPE_GENERIC;
        item.TargetName = target.data();
        item.UserName = const_cast<wchar_t *>(L"SolarLauncher");
        item.CredentialBlobSize = DWORD(secret.size());
        item.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.data()));
        item.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (!CredWriteW(&item, 0))
            code = GetLastError();
    }
    if (code)
        throw Error("vault", "credential_" + std::to_string(code),
                    "无法访问系统凭据管理器；可仅在本次会话中登录。");
#else
    static const SecretSchema schema = {"io.smeltkit.solar.accounts",
                                        SECRET_SCHEMA_NONE,
                                        {{"application", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                         {"account", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                         {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
                                        0,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr};
    GError *error = nullptr;
    auto cancel = g_cancellable_new();
    std::atomic_bool done{false};
    std::thread timer([&] {
        for (int n = 0; n < 100 && !done; ++n)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!done)
            g_cancellable_cancel(cancel);
    });
    if (kind == 0) {
        auto value = secret_password_lookup_sync(&schema, cancel, &error, "application",
                                                 service_.c_str(), "account", key.c_str(), nullptr);
        if (value) {
            result = value;
            secret_password_free(value);
        }
    } else if (kind == 1)
        secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "SECL",
                                   secret.c_str(), cancel, &error, "application", service_.c_str(),
                                   "account", key.c_str(), nullptr);
    else
        secret_password_clear_sync(&schema, cancel, &error, "application", service_.c_str(),
                                   "account", key.c_str(), nullptr);
    done = true;
    timer.join();
    g_object_unref(cancel);
    if (error) {
        g_error_free(error);
        throw Error("vault", "secret_service_failed",
                    "无法访问系统 Secret Service；可仅在本次会话中登录。");
    }
#endif
    if (kind == 0 && (result.empty() || result.size() > 32768))
        throw Error("vault", "credential_missing", "未找到有效的已保存凭据，请重新登录。");
    return result;
}
} // namespace solar
