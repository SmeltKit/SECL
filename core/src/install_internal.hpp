// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "solar/engine.hpp"
#include <algorithm>
#include <regex>
#include <set>
#include <thread>
namespace solar::engine::detail {
inline void require(bool ok, const std::string &code, const std::string &message) {
    if (!ok)
        throw Error("install", code, message);
}
inline bool id(const std::string &s) {
    return std::regex_match(s, std::regex("[A-Za-z0-9_.+ -]{1,180}")) && s != "." && s != "..";
}
inline std::string trim(std::string s) {
    auto b = s.find_first_not_of(" \t\r\n");
    return b == s.npos ? "" : s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
inline std::string sha1(const std::string &s) {
    std::smatch m;
    require(std::regex_search(s, m, std::regex("[a-fA-F0-9]{40}")), "invalid_checksum",
            "无效的 SHA-1 校验值。");
    auto v = m.str();
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return v;
}
struct Workspace {
    fs::path path;
    explicit Workspace(const fs::path &root) {
        path = checked_path(root, ".solar-work-" + random_url_token());
        fs::create_directories(path);
    }
    ~Workspace() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};
struct Lock {
    fs::path path;
    explicit Lock(const fs::path &root) {
        fs::create_directories(root);
        path = checked_path(root, ".solar-install-lock");
        require(fs::create_directory(path), "instance_locked", "该实例已有安装操作进行中。");
    }
    ~Lock() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};
inline Json merge(const Json &base, const Json &child) {
    auto result = base;
    for (auto &v : child.items())
        if (v.key() != "libraries" && v.key() != "arguments" && v.key() != "inheritsFrom")
            result[v.key()] = v.value();
    auto libs = child.value("libraries", Json::array());
    std::set<std::string> replaced;
    auto key = [](const Json &v) {
        auto name = v.value("name", "");
        std::vector<std::string> p;
        size_t pos = 0;
        while (true) {
            auto n = name.find(':', pos);
            p.push_back(name.substr(pos, n - pos));
            if (n == name.npos)
                break;
            pos = n + 1;
        }
        return p.size() < 3 ? name : p[0] + ":" + p[1] + (p.size() > 3 ? ":" + p[3] : "");
    };
    for (auto &l : libs)
        replaced.insert(key(l));
    for (auto &l : base.value("libraries", Json::array()))
        if (!replaced.count(key(l)))
            libs.push_back(l);
    result["libraries"] = libs;
    auto args = base.value("arguments", Json::object());
    auto child_args = child.value("arguments", Json::object());
    for (auto &v : child_args.items()) {
        auto a = args.value(v.key(), Json::array());
        for (auto &item : v.value())
            a.push_back(item);
        args[v.key()] = a;
    }
    result["arguments"] = args;
    result["_baseId"] = base.value("_baseId", base.value("id", ""));
    return result;
}
inline void process(const Command &command, const Task &t, int seconds = 600) {
    t.check();
    auto child = spawn_game(command);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    for (;;) {
        auto code = game_exit(child);
        if (code != -1) {
            require(code == 0, "processor_exit_" + std::to_string(code),
                    "安装工具退出，代码 " + std::to_string(code) + "。");
            return;
        }
        if (t.cancel->load() || std::chrono::steady_clock::now() > deadline) {
            terminate_process(child);
            throw Error("install", t.cancel->load() ? "cancelled" : "processor_timeout",
                        "安装工具已取消或超时。");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
inline int major(const Json &m) {
    auto v = m.value("javaVersion", Json::object()).value("majorVersion", 0);
    if (v > 0)
        return v;
    std::smatch p;
    auto s = m.value("_baseId", m.value("id", ""));
    if (std::regex_match(s, p, std::regex("1\\.([0-9]+)(?:\\.([0-9]+))?"))) {
        int n = std::stoi(p[1]), patch = p[2].matched ? std::stoi(p[2]) : 0;
        return n > 20 || (n == 20 && patch >= 5) ? 21 : n >= 18 ? 17 : n >= 17 ? 16 : 8;
    }
    throw Error("java", "unknown_requirement", "元数据未提供此版本的 Java 要求。");
}
} // namespace solar::engine::detail
