// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace solar::engine {
namespace {
// Metadata is untrusted. In particular, a Maven hyphen can create another
// nested comparison list. Exceeding these limits means unknown, not mismatch.
constexpr size_t max_version_length = 1024;
constexpr size_t max_range_length = 16384;
constexpr size_t max_parts = 128;
constexpr size_t max_maven_depth = 64;

std::string trim(const std::string &s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    return first == s.npos ? "" : s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}
bool digit(char c) { return c >= '0' && c <= '9'; }
bool ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool digits(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), digit);
}
std::string number(const std::string &s) {
    auto first = s.find_first_not_of('0');
    return first == s.npos ? "0" : s.substr(first);
}
int lexical(const std::string &a, const std::string &b) { return a == b ? 0 : a < b ? -1 : 1; }
int numeric(const std::string &a, const std::string &b) {
    return a.size() == b.size() ? lexical(a, b) : a.size() < b.size() ? -1 : 1;
}
bool known_version(const std::string &s) {
    if (s.empty() || s.size() > max_version_length || s.find("${") != s.npos ||
        s.find("@VERSION@") != s.npos)
        return false;
    return std::none_of(s.begin(), s.end(), [](unsigned char c) { return c <= ' ' || c == 127; });
}
std::vector<std::string> split(const std::string &s, char separator) {
    std::vector<std::string> result;
    size_t begin = 0;
    for (;;) {
        auto end = s.find(separator, begin);
        result.push_back(s.substr(begin, end == s.npos ? end : end - begin));
        if (end == s.npos || result.size() > max_parts)
            break;
        begin = end + 1;
    }
    return result;
}

struct Semantic {
    std::vector<uint32_t> components;
    std::vector<std::string> prerelease;
    bool has_prerelease = false;
    bool valid = false;
};
Semantic semantic(std::string s) {
    Semantic result;
    if (!known_version(s))
        return result;
    auto build = s.find('+');
    if (build != s.npos)
        s.resize(build); // Fabric allows arbitrary (including empty) build metadata.
    auto dash = s.find('-');
    if (dash != s.npos) {
        result.has_prerelease = true;
        auto pre = s.substr(dash + 1);
        if (!pre.empty()) {
            result.prerelease = split(pre, '.');
            if (result.prerelease.size() > max_parts)
                return {};
            for (const auto &part : result.prerelease)
                if (part.empty() || !std::all_of(part.begin(), part.end(), [](char c) {
                        return digit(c) || ascii_letter(c) || c == '-';
                    }))
                    return {};
        }
        s.resize(dash);
    }
    auto parts = split(s, '.');
    if (parts.size() > max_parts)
        return {};
    for (const auto &part : parts) {
        if (!digits(part))
            return {};
        // Loader components use signed Java ints. Avoid both overflow and
        // guessing numeric order for versions the loader treats as opaque.
        auto canonical = number(part);
        if (numeric(canonical, "2147483647") > 0)
            return {};
        uint32_t value = 0;
        for (char c : canonical)
            value = value * 10 + unsigned(c - '0');
        result.components.push_back(value);
    }
    result.valid = true;
    return result;
}
uint32_t component(const Semantic &v, size_t i) {
    return i < v.components.size() ? v.components[i] : 0;
}
int compare_semantic(const Semantic &a, const Semantic &b) {
    for (size_t i = 0; i < std::max(a.components.size(), b.components.size()); ++i) {
        auto x = component(a, i), y = component(b, i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (a.has_prerelease != b.has_prerelease)
        return a.has_prerelease ? -1 : 1;
    for (size_t i = 0; i < std::max(a.prerelease.size(), b.prerelease.size()); ++i) {
        if (i == a.prerelease.size() || i == b.prerelease.size())
            return i == a.prerelease.size() ? -1 : 1;
        auto &x = a.prerelease[i];
        auto &y = b.prerelease[i];
        if (x == y)
            continue;
        // Fabric's unsigned-integer identifiers have no redundant leading 0s.
        bool xn = digits(x) && (x.size() == 1 || x[0] != '0');
        bool yn = digits(y) && (y.size() == 1 || y[0] != '0');
        if (xn != yn)
            return xn ? -1 : 1;
        return xn ? numeric(x, y) : lexical(x, y);
    }
    return 0;
}
bool opaque_operand(const std::string &s) {
    if (!known_version(s) || s == "-" || s == "." ||
        s.find_first_of("[](),|&!<>=^~*\\") != s.npos)
        return false;
    // These are malformed numeric predicates, rather than intentional opaque
    // versions such as "release/abc" or "v1.2".
    if ((s.size() > 1 && (s[0] == '-' || s[0] == '+') && digit(s[1])) ||
        (digit(s[0]) && (s.find("..") != s.npos || s.back() == '.')))
        return false;
    return true;
}

int semantic_term(const std::string &token, const std::string &version,
                  const Semantic &current, bool quilt) {
    if (token == "*")
        return 1;
    std::string op, operand = token;
    for (const auto *prefix : {">=", "<=", ">", "<", "=", "^", "~"}) {
        std::string p(prefix);
        if (token.compare(0, p.size(), p) == 0) {
            op = p;
            operand = token.substr(p.size());
            break;
        }
    }
    if (!known_version(operand))
        return -1;
    auto core_end = operand.find_first_of("-+");
    auto core = operand.substr(0, core_end);
    auto parts = split(core, '.');
    auto wildcard = std::find_if(parts.begin(), parts.end(), [](const std::string &p) {
        return p == "x" || p == "X" || p == "*";
    });
    if (wildcard != parts.end()) {
        auto index = size_t(wildcard - parts.begin());
        if (parts.size() > max_parts || index == 0 || (!op.empty() && op != "=") ||
            operand.find('-') != operand.npos || !current.valid)
            return -1;
        for (auto i = wildcard; i != parts.end(); ++i)
            if (*i != "x" && *i != "X" && *i != "*")
                return -1;
        if (quilt && (op == "=" || parts.back() != "x" || index + 1 != parts.size() ||
                      core_end != operand.npos))
            return -1;
        // Build the fixed prefix explicitly; repeated wildcard components must
        // not change the prefix to compare.
        std::string fixed;
        for (size_t i = 0; i < index; ++i) {
            if (i)
                fixed += '.';
            fixed += parts[i];
        }
        auto prefix = semantic(fixed);
        if (!prefix.valid)
            return -1;
        if (quilt) {
            auto upper = prefix;
            if (upper.components.back() == 2147483647U)
                return -1;
            ++upper.components.back();
            return compare_semantic(current, prefix) >= 0 && compare_semantic(current, upper) < 0;
        }
        for (size_t i = 0; i < index; ++i)
            if (component(current, i) != prefix.components[i])
                return 0;
        return 1;
    }
    auto want = semantic(operand);
    if (!want.valid) {
        if ((!op.empty() && op != "=") || !opaque_operand(operand))
            return -1;
        return version == operand;
    }
    if (!current.valid) {
        if (op.empty() || op == "=")
            return version == operand;
        return -1;
    }
    if (quilt && op.empty())
        op = "^";
    int comparison = compare_semantic(current, want);
    if (op == "^" || op == "~") {
        if (!quilt)
            return comparison >= 0 && component(current, 0) == component(want, 0) &&
                   (op == "^" || component(current, 1) == component(want, 1));
        auto upper = want;
        size_t index = op == "^" ? 0 : 1;
        upper.components.resize(index + 1, 0);
        if (upper.components[index] == 2147483647U)
            return -1;
        ++upper.components[index];
        upper.has_prerelease = false;
        upper.prerelease.clear();
        return comparison >= 0 && compare_semantic(current, upper) < 0;
    }
    return op == ">=" ? comparison >= 0 : op == "<=" ? comparison <= 0
           : op == ">" ? comparison > 0 : op == "<" ? comparison < 0
                                                   : comparison == 0;
}
int semantic_range(const std::string &range, const std::string &version, bool quilt) {
    // Fabric: https://wiki.fabricmc.net/documentation:fabric_mod_json_spec
    // Quilt: V1ModMetadataReader.readVersionSpecifier in QuiltMC/quilt-loader.
    if (range.empty())
        return quilt ? -1 : 1;
    auto current = semantic(version);
    if (quilt) {
        // Quilt uses JSON {all:[...]} / {any:[...]} for compound constraints.
        if (range.find_first_of(" \t\r\n|") != range.npos)
            return -1;
        return semantic_term(range, version, current, true);
    }
    bool unknown = false, matched = false;
    size_t begin = 0, terms = 0;
    for (;;) {
        auto end = range.find("||", begin);
        auto branch = trim(range.substr(begin, end == range.npos ? end : end - begin));
        if (branch.empty())
            return -1;
        std::istringstream input(branch);
        std::string token;
        bool branch_unknown = false, branch_matches = true;
        while (input >> token) {
            if (++terms > max_parts)
                return -1;
            auto result = semantic_term(token, version, current, false);
            branch_unknown |= result < 0;
            branch_matches &= result == 1;
        }
        unknown |= branch_unknown;
        matched |= branch_matches;
        if (end == range.npos)
            break;
        begin = end + 2;
    }
    return matched ? 1 : unknown ? -1 : 0;
}

// Maven 3 ComparableVersion's number/qualifier/list ordering, including its
// digit-to-letter boundaries. This deliberately is not SemVer ordering:
// https://maven.apache.org/pom.html#version-order-specification
struct MavenItem {
    enum Type { Number, Qualifier, List } type = List;
    std::string value;
    std::vector<MavenItem> children;
};
std::string qualifier(std::string s, bool followed_by_digit) {
    if (followed_by_digit && s.size() == 1) {
        if (s == "a") s = "alpha";
        if (s == "b") s = "beta";
        if (s == "m") s = "milestone";
    }
    if (s == "ga" || s == "final" || s == "release") return "";
    return s == "cr" ? "rc" : s;
}
int qualifier_rank(const std::string &s) {
    const std::vector<std::string> known{"alpha", "beta", "milestone", "rc", "snapshot", "", "sp"};
    auto found = std::find(known.begin(), known.end(), s);
    return int(found - known.begin());
}
int compare_qualifier(const std::string &a, const std::string &b) {
    auto x = qualifier_rank(a), y = qualifier_rank(b);
    return x == y ? lexical(a, b) : x < y ? -1 : 1;
}
int compare_maven(const MavenItem *a, const MavenItem *b);
bool maven_null(const MavenItem &item) {
    return item.type == MavenItem::Number ? item.value == "0"
           : item.type == MavenItem::Qualifier ? item.value.empty()
                                              : item.children.empty();
}
int compare_maven(const MavenItem *a, const MavenItem *b) {
    if (!a)
        return b ? -compare_maven(b, nullptr) : 0;
    if (!b) {
        if (a->type == MavenItem::Number)
            return a->value == "0" ? 0 : 1;
        if (a->type == MavenItem::Qualifier)
            return compare_qualifier(a->value, "");
        for (const auto &child : a->children) {
            int c = compare_maven(&child, nullptr);
            if (c)
                return c;
        }
        return 0;
    }
    if (a->type != b->type) {
        auto rank = [](MavenItem::Type t) { return t == MavenItem::Number ? 2 : t == MavenItem::List ? 1 : 0; };
        return rank(a->type) < rank(b->type) ? -1 : 1;
    }
    if (a->type == MavenItem::Number)
        return numeric(a->value, b->value);
    if (a->type == MavenItem::Qualifier)
        return compare_qualifier(a->value, b->value);
    for (size_t i = 0; i < std::max(a->children.size(), b->children.size()); ++i) {
        int c = compare_maven(i < a->children.size() ? &a->children[i] : nullptr,
                              i < b->children.size() ? &b->children[i] : nullptr);
        if (c)
            return c;
    }
    return 0;
}
bool maven_version(std::string version, MavenItem &root) {
    if (!known_version(version) || !std::all_of(version.begin(), version.end(), [](char c) {
            return digit(c) || ascii_letter(c) || c == '.' || c == '-' || c == '+' || c == '_';
        }) || (!digit(version[0]) && !ascii_letter(version[0])))
        return false;
    for (char &c : version)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    root = {};
    std::vector<MavenItem *> stack{&root};
    MavenItem *list = &root;
    size_t count = 0;
    auto add = [&](bool is_digit, const std::string &s, bool followed = false) {
        MavenItem item;
        item.type = is_digit ? MavenItem::Number : MavenItem::Qualifier;
        item.value = is_digit ? number(s) : qualifier(s, followed);
        list->children.push_back(std::move(item));
        return ++count <= max_parts;
    };
    auto nest = [&]() {
        if (stack.size() >= max_maven_depth || ++count > max_parts)
            return false;
        list->children.emplace_back();
        list = &list->children.back();
        stack.push_back(list);
        return true;
    };
    bool is_digit = false;
    size_t start = 0;
    for (size_t i = 0; i < version.size(); ++i) {
        char c = version[i];
        if (c == '.' || c == '-') {
            if (!add(i == start || is_digit, i == start ? "0" : version.substr(start, i - start)))
                return false;
            start = i + 1;
            if (c == '-' && !nest())
                return false;
        } else if (digit(c)) {
            if (!is_digit && i > start) {
                if ((!list->children.empty() && !nest()) ||
                    !add(false, version.substr(start, i - start), true) || !nest())
                    return false;
                start = i;
            }
            is_digit = true;
        } else {
            if (is_digit && i > start) {
                if (!add(true, version.substr(start, i - start)) || !nest())
                    return false;
                start = i;
            }
            is_digit = false;
        }
    }
    if (start < version.size()) {
        if ((!is_digit && !list->children.empty() && !nest()) ||
            !add(is_digit, version.substr(start)))
            return false;
    }
    for (auto i = stack.rbegin(); i != stack.rend(); ++i) {
        auto &children = (*i)->children;
        for (size_t k = children.size(); k > 0; --k) {
            auto &item = children[k - 1];
            if (maven_null(item))
                children.erase(children.begin() + k - 1);
            else if (item.type != MavenItem::List)
                break;
        }
    }
    return true;
}

int maven_range(const std::string &range, const std::string &version) {
    if (range == "*")
        return 1;
    if (range.empty() || (range[0] != '[' && range[0] != '('))
        return -1; // Bare Maven versions are soft recommendations, not exact constraints.
    MavenItem current;
    if (!maven_version(version, current))
        return -1;
    bool matched = false, previous_has_upper = false;
    MavenItem previous_upper;
    size_t start = 0, count = 0;
    while (start < range.size()) {
        if (++count > max_parts || (range[start] != '[' && range[start] != '('))
            return -1;
        auto end = range.find_first_of(")]", start + 1);
        if (end == range.npos)
            return -1;
        auto body = range.substr(start + 1, end - start - 1);
        auto comma = body.find(',');
        bool lower_inclusive = range[start] == '[', upper_inclusive = range[end] == ']';
        if (body.find_first_of("[]()") != body.npos)
            return -1;
        MavenItem lower, upper;
        bool has_lower = true, has_upper = true;
        if (comma == body.npos) {
            MavenItem exact;
            if (!lower_inclusive || !upper_inclusive || !maven_version(trim(body), exact))
                return -1;
            matched |= compare_maven(&current, &exact) == 0;
            lower = upper = exact;
        } else {
            if (body.find(',', comma + 1) != body.npos)
                return -1;
            auto lower_text = trim(body.substr(0, comma));
            auto upper_text = trim(body.substr(comma + 1));
            has_lower = !lower_text.empty();
            has_upper = !upper_text.empty();
            if ((lower_text.empty() && lower_inclusive) || (upper_text.empty() && upper_inclusive) ||
                (!lower_text.empty() && !maven_version(lower_text, lower)) ||
                (!upper_text.empty() && !maven_version(upper_text, upper)))
                return -1;
            if (!lower_text.empty() && !upper_text.empty()) {
                int order = compare_maven(&lower, &upper);
                if (order > 0 || (order == 0 && (!lower_inclusive || !upper_inclusive)))
                    return -1;
            }
            int l = lower_text.empty() ? 1 : compare_maven(&current, &lower);
            int u = upper_text.empty() ? -1 : compare_maven(&current, &upper);
            matched |= (l > 0 || (l == 0 && lower_inclusive)) &&
                       (u < 0 || (u == 0 && upper_inclusive));
        }
        if (count > 1 && (!previous_has_upper || !has_lower ||
                          compare_maven(&lower, &previous_upper) < 0))
            return -1; // Maven union intervals must be ordered and not overlap.
        previous_has_upper = has_upper;
        previous_upper = std::move(upper);
        start = end + 1;
        while (start < range.size() && std::isspace(static_cast<unsigned char>(range[start])))
            ++start;
        if (start == range.size())
            break;
        if (range[start++] != ',')
            return -1;
        while (start < range.size() && std::isspace(static_cast<unsigned char>(range[start])))
            ++start;
        if (start == range.size())
            return -1;
    }
    return matched;
}
} // namespace

int mod_version_matches(const std::string &raw, const std::string &version,
                        const std::string &dialect) {
    if (raw.size() > max_range_length || !known_version(version))
        return -1;
    auto range = trim(raw);
    if (dialect == "fabric" || dialect == "quilt")
        return semantic_range(range, version, dialect == "quilt");
    if (dialect == "maven")
        return maven_range(range, version);
    return -1;
}

int version_matches(const std::string &raw, const std::string &version) {
    if (raw.size() > max_range_length)
        return -1;
    auto range = trim(raw);
    return mod_version_matches(raw, version,
                               !range.empty() && (range[0] == '[' || range[0] == '(')
                                   ? "maven" : "fabric");
}
} // namespace solar::engine
