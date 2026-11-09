// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
namespace solar::engine { Json diagnose_log(const std::string &, bool); }
using namespace solar; using namespace solar::engine;
static int checks=0;
static void check(bool value,const char *message) { ++checks; if(!value)throw std::runtime_error(message); }
static Json finding(const Json &result,const char *code) {
    for(const auto &row:result.at("findings")) if(row.at("code")==code)return row;
    throw std::runtime_error(std::string("Missing finding: ")+code);
}
int main() { try {
    const auto healthy=diagnose_log("[INFO] Using Mixin subsystem 0.8\n[INFO] Stopping!\n",false);
    check(healthy.at("status")=="unknown" && healthy.at("findings").empty(),"ordinary Mixin and normal exit are not crash evidence");
    check(diagnose_log("",false).at("status")=="unknown","empty input explicitly unknown");
    const auto heap=diagnose_log("[INFO] Loading\r\njava.lang.OutOfMemoryError: Java heap space\r\n",false);
    const auto heap_hint=finding(heap,"java_heap_exhausted");
    check(heap_hint.at("severity")=="error","heap severity");
    check(heap_hint.at("evidence")[0].at("line")==2,"one-based evidence line numbers");
    check(heap_hint.at("evidence")[0].at("text")=="java.lang.OutOfMemoryError: Java heap space","CRLF evidence preserved without carriage return");
    const auto native=diagnose_log("java.lang.OutOfMemoryError: unable to create native thread",false);
    finding(native,"java_native_memory");
    check(native.at("findings").size()==1,"native memory is not confused with heap memory");
    const auto version=diagnose_log("java.lang.UnsupportedClassVersionError: Example has been compiled by a more recent version of the Java Runtime (class file version 65.0)",false);
    finding(version,"java_version_mismatch");
    finding(diagnose_log("Unsupported class file major version 65",false),"java_version_mismatch");
    const auto combined=diagnose_log("Error: Could not find or load main class demo.Main\nCaused by: java.lang.ClassNotFoundException: demo.Main\nMod A requires version 2 of B, which is missing!\norg.spongepowered.asm.mixin.transformer.throwables.MixinTransformerError: Unexpected critical error\n",true);
    finding(combined,"launch_target_missing"); finding(combined,"class_or_library_missing");
    finding(combined,"mod_dependency_missing"); finding(combined,"mixin_application_failed");
    check(combined.at("findings").size()==4,"different clues retained without absolute attribution");
    check(combined.at("truncated")==true && combined.at("scope")=="log_excerpt","excerpt limitations preserved");
    finding(diagnose_log("Missing or unsupported mandatory dependencies:\n",false),"mod_dependency_missing");
    finding(diagnose_log("java.lang.UnsatisfiedLinkError: Can't load library",false),"class_or_library_missing");
    check(diagnose_log("[INFO] Dependencies loaded; user docs say requires Java",false).at("status")=="unknown","generic dependency chatter does not match");
    std::string repeated;
    for(int i=0;i<100;++i)repeated+="java.lang.OutOfMemoryError: Java heap space "+std::string(3000,'x')+"\n";
    const auto bounded=finding(diagnose_log(repeated,false),"java_heap_exhausted");
    check(bounded.at("evidence").size()==4,"duplicate evidence bounded per rule");
    check(bounded.at("evidence")[0].at("text").get<std::string>().size()<1810,"long evidence bounded");
    std::string utf8="java.lang.OutOfMemoryError: Java heap space ";
    for(int i=0;i<1000;++i)utf8+="测";
    const auto unicode=diagnose_log(utf8,false);
    check(!unicode.dump().empty(),"truncated evidence stays valid UTF-8 JSON");
    std::cout<<checks<<" log diagnostic checks passed\n"; return 0;
} catch(const std::exception &e) {std::cerr<<"Log diagnostic test failure: "<<e.what()<<"\n";return 1;} }
