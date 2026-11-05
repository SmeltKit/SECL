// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace solar::engine;

struct Case {
    std::string dialect, range, version;
    int expected;
};

int main() {
    try {
        // Loader semantics are intentionally different from npm SemVer and
        // from each other; these expectations follow the loader specifications.
        const std::vector<Case> cases{
            {"fabric", ">=1.2.0 <2.0.0", "1.2.3", 1},
            {"fabric", ">=1.2.0 <2.0.0", "2.0.0", 0},
            {"fabric", "=1.2", "1.2.0.0", 1},
            {"fabric", "1.2.3", "1.2.4", 0},
            {"fabric", "1.2.3+build.one", "1.2.3+build.two", 1},
            {"fabric", "=01.002.0003", "1.2.3", 1},
            {"fabric", "^0.2.3", "0.3.0", 1},
            {"fabric", "^0.2.3", "1.0.0-alpha", 0},
            {"fabric", "^1.2.3", "1.99.0-beta", 1},
            {"fabric", "^1.2.3", "2.0.0-alpha", 0},
            {"fabric", "~1", "1.0.9", 1},
            {"fabric", "~1", "1.1", 0},
            {"fabric", "~1.2.3", "1.2.99", 1},
            {"fabric", "~1.2.3", "1.3.0-alpha", 0},
            {"fabric", "1.x", "1.0.0-alpha", 1},
            {"fabric", "1.2.X", "1.2.0-alpha", 1},
            {"fabric", "=1.2.*", "1.2.7", 1},
            {"fabric", "1.x.x", "1.8.9", 1},
            {"fabric", "1.2.3.x", "1.2.3.4", 1},
            {"fabric", "1.2.x", "1.3.0-alpha", 0},
            {"fabric", ">=1.2-", "1.2-alpha", 1},
            {"fabric", "<1.2", "1.2-", 1},
            {"fabric", ">1.2-alpha.9", "1.2-alpha.10", 1},
            {"fabric", ">1.2-alpha.999999999999999999999999999999", "1.2-alpha.1000000000000000000000000000000", 1},
            {"fabric", "<1.2-alpha.beta", "1.2-alpha.1", 1},
            {"fabric", "<1.2-alpha.1", "1.2-alpha", 1},
            {"fabric", "<1.2-alpha", "1.2-", 1},
            {"fabric", "release/abc", "release/abc", 1},
            {"fabric", "=release/abc", "release/def", 0},
            {"fabric", "v1.2", "v1.2", 1},
            {"fabric", "v1.2", "1.2", 0},
            {"fabric", ">=release/abc", "release/def", -1},
            {"fabric", ">=1.0", "release/abc", -1},
            {"fabric", "<1.0 >=release/abc", "2.0", -1},
            {"fabric", "<1 || >=2", "2.5", 1},
            {"fabric", "<1 || >=2", "1.5", 0},
            {"fabric", "<1 || >=release/abc", "1.5", -1},
            {"fabric", "=1.5 || >=release/abc", "1.5", 1},
            {"fabric", "*", "opaque-version", 1},
            {"fabric", "", "1.2", 1},
            {"fabric", ">=1.0", "2147483648", -1},
            {"fabric", ">=2147483648", "1.0", -1},

            {"quilt", "1.2.3", "1.9", 1},
            {"quilt", "1.2.3", "2.0", 0},
            {"quilt", "1.2.3", "1.2.2", 0},
            {"quilt", "=1.2.3", "1.2.4", 0},
            {"quilt", "^0.2.3", "0.9", 1},
            {"quilt", "^1.2", "2.0-alpha", 1},
            {"quilt", "~1", "1.0.9", 1},
            {"quilt", "~1", "1.1", 0},
            {"quilt", "~1.2.3", "1.3-alpha", 1},
            {"quilt", "1.2.x", "1.2-alpha", 0},
            {"quilt", "1.2.x", "1.2.5", 1},
            {"quilt", "1.2.x", "1.3-alpha", 1},
            {"quilt", "release/abc", "release/abc", 1},
            {"quilt", "=release/abc", "release/def", 0},
            {"quilt", ">=1 <2", "1.5", -1},
            {"quilt", "1 || 2", "1", -1},
            {"quilt", "1.2.X", "1.2.3", -1},
            {"quilt", "^2147483647", "2147483647", -1},
            {"quilt", "", "1.2", -1},

            {"maven", "[1.0,2.0)", "1.5", 1},
            {"maven", "[1.0,2.0)", "2.0", 0},
            {"maven", "[1.0,2.0]", "2.0", 1},
            {"maven", "(1.0,2.0)", "1.0", 0},
            {"maven", "[1.2]", "1.2.0.0", 1},
            {"maven", "[1]", "1.0", 1},
            {"maven", "[1]", "1.0.0-ga", 1},
            {"maven", "[1]", "1-final", 1},
            {"maven", "[1]", "1-release", 1},
            {"maven", "[1]", "1-0", 1},
            {"maven", "[1.0-rc1]", "1.0-CR1", 1},
            {"maven", "[1.0-alpha1]", "1.0-a1", 1},
            {"maven", "[1.0-beta1]", "1.0-b1", 1},
            {"maven", "[1.0-milestone1]", "1.0-m1", 1},
            {"maven", "(,1.0],[1.2,)", "0.9", 1},
            {"maven", "(,1.0],[1.2,)", "1.0", 1},
            {"maven", "(,1.0],[1.2,)", "1.1", 0},
            {"maven", "(,1.0],[1.2,)", "1.2", 1},
            {"maven", "(,1.1),(1.1,)", "1.1", 0},
            {"maven", "(,1.1),(1.1,)", "1.1-sp", 1},
            {"maven", "[1.0-rc1,1.0)", "1.0-SNAPSHOT", 1},
            {"maven", "(,1.0)", "1.0-RC2", 1},
            {"maven", "(1.0,)", "1.0-sp", 1},
            {"maven", "(1.0-sp,)", "1.0-unknown", 1},
            {"maven", "(1.0,)", "1.0-1", 1},
            {"maven", "(,1.0.1)", "1.0-2", 1},
            {"maven", "(,1.0-X3)", "1.0.X2", 1},
            {"maven", "[1.00000000000000000000000000000000002]", "1.2", 1},
            {"maven", "[999999999999999999999999999999999999,)", "1000000000000000000000000000000000000", 1},
            {"maven", "1.2", "1.3", -1},
            {"maven", ">=1.0", "1.2", -1},
            {"maven", "[1,2)", "${version}", -1},
            {"maven", "[${version},2)", "1.2", -1},
            {"maven", "[2,1]", "1.5", -1},
            {"maven", "(1,1)", "1.5", -1},
            {"maven", "[1,2),", "1.5", -1},
            {"maven", "[1,2),garbage", "1.5", -1},
            {"maven", "[1,,2]", "1.5", -1},
            {"maven", "(1)", "1", -1},
            {"maven", "[,2]", "1", -1},
            {"maven", "[1,]", "1", -1},
            {"maven", "[1,2", "1", -1},
            {"maven", "[1.x,2)", "1.5", 1}, // x is a Maven qualifier, not a wildcard.
            {"maven", "[1,2]", "opaque/version", -1},
            {"maven", "[1,3],[2,4]", "1.5", -1},
            {"maven", "[3,4],[1,2]", "3.5", -1},
            {"maven", "[1,2] || [3,4]", "1.5", -1},
            {"maven", "[1] [2]", "1", -1},
            {"maven", "", "1", -1},
        };
        size_t checked = 0;
        for (const auto &test : cases) {
            auto actual = mod_version_matches(test.range, test.version, test.dialect);
            if (actual != test.expected)
                throw std::runtime_error(test.dialect + " " + test.range + " @ " + test.version +
                                         ": expected " + std::to_string(test.expected) + ", got " +
                                         std::to_string(actual));
            ++checked;
        }
        const std::vector<std::string> malformed{
            ">", "==1.0", "!=1.0", ">= 1.0", "1.0 - 2.0", "1.0,2.0", "1.x.2",
            "1..2", "1.2.", "1.*garbage", ">=1.x", "^1.x", "1.x-alpha",
            "1 ||", "|| 1", "1 | 2", "1 && 2", "[1,2)", "-1.2",
        };
        for (const auto &range : malformed) {
            if (mod_version_matches(range, "3.0", "fabric") != -1)
                throw std::runtime_error("Malformed Fabric range must remain unknown: " + range);
            ++checked;
        }
        for (const auto &dialect : {"fabric", "quilt", "maven", "unsupported"}) {
            for (const auto &version : {std::string(), std::string("${version}"), std::string(2048, '9')}) {
                if (mod_version_matches("*", version, dialect) != -1)
                    throw std::runtime_error("Indeterminate version must remain unknown");
                ++checked;
            }
            if (mod_version_matches(std::string(20000, '1'), "1", dialect) != -1)
                throw std::runtime_error("Excessive range must remain unknown");
            ++checked;
        }
        std::string nested = "1", components = "1";
        for (int i = 0; i < 200; ++i) {
            nested += "-1";
            components += ".1";
        }
        if (mod_version_matches("[1,2)", nested, "maven") != -1 ||
            mod_version_matches(">=1", components, "fabric") != -1 ||
            mod_version_matches("[1,2)", components, "maven") != -1)
            throw std::runtime_error("Excessive nesting/components must remain unknown");
        if (version_matches("^0.2.3", "0.3.0") != 1 ||
            version_matches("[1,2)", "1.5") != 1 ||
            version_matches("[1]", "1.0") != 1)
            throw std::runtime_error("Autodetection wrapper must use loader semantics");
        std::cout << "mod version checks: " << checked + 6 << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
