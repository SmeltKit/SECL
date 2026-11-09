// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <cctype>
#include <sstream>
namespace solar::engine {
namespace {
std::string lower_ascii(std::string value) {
    for (auto &c : value) c = char(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
struct LogRule {
    const char *code, *severity, *title, *suggestion;
    std::function<bool(const std::string &)> matches;
};
bool has(const std::string &line, const char *needle) { return line.find(needle) != line.npos; }
} // namespace
// A bounded, local heuristic over an already redacted log excerpt. Findings are
// evidence-backed hints, never a claim that a named mod caused a crash.
Json diagnose_log(const std::string &text, bool truncated) {
    const std::vector<LogRule> rules{
        {"java_heap_exhausted", "error", "日志包含 Java 堆内存不足线索",
         "检查实例内存设置和可用系统内存；适当调整最大内存，并排查大型资源包或模组占用。不要直接分配全部内存。",
         [](const std::string &s) { return (has(s,"outofmemoryerror") &&
             (has(s,"java heap space") || has(s,"gc overhead limit exceeded"))) ||
             has(s,"outofmemoryerror: requested array size exceeds vm limit"); }},
        {"java_native_memory", "error", "日志包含本机内存或线程不足线索",
         "检查系统可用内存、进程和线程数量；过大的 Java 堆也会挤占本机内存，不要仅增大最大内存。",
         [](const std::string &s) { return has(s,"native memory allocation (malloc) failed") ||
             has(s,"there is insufficient memory for the java runtime environment") ||
             (has(s,"outofmemoryerror") && (has(s,"unable to create native thread") ||
                 has(s,"unable to create new native thread") || has(s,"direct buffer memory"))); }},
        {"java_version_mismatch", "error", "日志包含 Java 字节码版本不匹配线索",
         "核对所选 Java 与游戏、加载器和模组的版本要求，使用兼容的 Java。不要仅按日志中的类名删除模组。",
         [](const std::string &s) { return has(s,"unsupportedclassversionerror") ||
             has(s,"has been compiled by a more recent version of the java runtime") ||
             has(s,"unsupported class file major version"); }},
        {"launch_target_missing", "error", "日志包含启动主类或文件缺失线索",
         "检查实例版本配置、主类及依赖文件是否完整；可重新安装对应版本组件，先保留存档和配置。",
         [](const std::string &s) { return has(s,"could not find or load main class") ||
             has(s,"unable to access jarfile"); }},
        {"class_or_library_missing", "warning", "日志包含类或本机库加载失败线索",
         "核对加载器、模组前置和本机库是否适配；查看相邻日志。部分可选功能也会记录此异常，不能据此确认崩溃原因。",
         [](const std::string &s) { return has(s,"noclassdeffounderror") ||
             has(s,"classnotfoundexception") || has(s,"unsatisfiedlinkerror"); }},
        {"mod_dependency_missing", "error", "日志明确报告缺少模组依赖",
         "按日志要求安装匹配游戏版本和加载器的前置模组，并核对版本范围；可先运行本地模组检查。",
         [](const std::string &s) { return has(s,"missing mandatory dependencies") ||
             has(s,"missing or unsupported mandatory dependencies") ||
             has(s,"missing mandatory dependency") ||
             ((has(s,"requires ") || has(s,"depends on ")) &&
              (has(s,"which is missing") || has(s,"not installed") || has(s,"is not present"))) ||
             (has(s,"mandatory dependencies") && has(s,"not installed")); }},
        {"mixin_application_failed", "warning", "日志包含 Mixin 应用失败线索",
         "检查游戏、加载器及相关模组版本，并查看首个异常和相邻日志。可在副本中逐步排查；Mixin 类名不等于责任模组。",
         [](const std::string &s) { return has(s,"mixin apply failed") ||
             has(s,"mixinapplyerror") || has(s,"invalidmixinexception") ||
             has(s,"mixintransformererror") || has(s,"critical injection failure"); }}
    };
    Json evidence = Json::array();
    for (size_t i=0;i<rules.size();++i) evidence.push_back(Json::array());
    std::istringstream input(text);
    std::string line;
    size_t line_number=0;
    while (std::getline(input,line)) {
        ++line_number;
        if (!line.empty() && line.back()=='\r') line.pop_back();
        const auto lower=lower_ascii(line);
        for (size_t i=0;i<rules.size();++i) {
            if (evidence[i].size()>=4 || !rules[i].matches(lower)) continue;
            auto excerpt=line;
            // Cut at a UTF-8 boundary so diagnostic evidence is valid JSON too.
            if (excerpt.size()>1800) {
                size_t end=1800;
                while(end>0 && (static_cast<unsigned char>(excerpt[end])&0xc0)==0x80) --end;
                excerpt.resize(end); excerpt+="…";
            }
            evidence[i].push_back({{"line",line_number},{"text",excerpt}});
        }
    }
    Json findings=Json::array();
    for (size_t i=0;i<rules.size();++i) if (!evidence[i].empty()) {
        const auto &r=rules[i];
        findings.push_back({{"code",r.code},{"severity",r.severity},{"title",r.title},
                            {"suggestion",r.suggestion},{"evidence",evidence[i]}});
    }
    return {{"status",findings.empty()?"unknown":"matched"},{"findings",findings},
            {"truncated",truncated},{"scope","log_excerpt"},{"line_count",line_number}};
}
} // namespace solar::engine
