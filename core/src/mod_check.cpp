// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <algorithm>
#include <chrono>
#include <set>

namespace solar::engine {
namespace {
// This is a bounded satisfiability check, not the loader's optimization policy.
// One assignment must satisfy every selected component; independent per-edge
// matches cannot establish that a shared embedded version can actually be used.
struct Cnf {
    struct Clause { std::vector<int> literals; int origin; };
    std::vector<Clause> clauses;
    std::vector<bool> prefer{false, true};
    bool exhausted = false;
    int variable(bool positive = false) {
        if (prefer.size() >= 12000) { exhausted = true; return 1; }
        prefer.push_back(positive); return int(prefer.size()) - 1;
    }
    void add(std::vector<int> values, int origin = -1) {
        if (clauses.size() >= 50000) { exhausted = true; return; }
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        for (int value : values) if (std::binary_search(values.begin(), values.end(), -value)) return;
        clauses.push_back({std::move(values), origin});
    }
    int join(const std::vector<int> &input, bool all) {
        std::vector<int> values;
        for (int v : input) {
            if (v == (all ? -1 : 1)) return v;
            if (v != (all ? 1 : -1)) values.push_back(v);
        }
        if (values.empty()) return all ? 1 : -1;
        if (values.size() == 1) return values.front();
        int out = variable();
        std::vector<int> reverse{all ? out : -out};
        for (int v : values) {
            add({all ? -out : out, all ? v : -v});
            reverse.push_back(all ? -v : v);
        }
        add(std::move(reverse)); return out;
    }
    struct State { std::vector<signed char> values; std::vector<int> reason; };
    std::set<int> proof(int index, const State &state) const {
        std::set<int> origins, visited;
        std::vector<int> pending{index};
        while (!pending.empty() && visited.size() < 12000) {
            int ci = pending.back(); pending.pop_back();
            if (ci < 0 || !visited.insert(ci).second) continue;
            const auto &c = clauses[size_t(ci)];
            if (c.origin >= 0) origins.insert(c.origin);
            for (int lit : c.literals) {
                auto v = size_t(std::abs(lit));
                if (state.values[v] && state.reason[v] >= 0 && state.reason[v] != ci)
                    pending.push_back(state.reason[v]);
            }
        }
        return origins;
    }
    int solve(State state, State &solution, std::set<int> &why, size_t &steps,
              const std::chrono::steady_clock::time_point &deadline, int depth = 0) {
        if (exhausted || depth > 256 || ++steps > 20000 || std::chrono::steady_clock::now() > deadline) return -1;
        bool changed;
        do {
            changed = false;
            for (size_t ci = 0; ci < clauses.size(); ++ci) {
                const auto &c = clauses[ci]; bool satisfied = false;
                int open = 0, last = 0;
                for (int lit : c.literals) {
                    int value = state.values[size_t(std::abs(lit))];
                    if ((lit > 0 && value > 0) || (lit < 0 && value < 0)) { satisfied = true; break; }
                    if (!value) { ++open; last = lit; }
                }
                if (satisfied) continue;
                if (!open) { why = proof(int(ci), state); return 0; }
                if (open == 1) {
                    auto v = size_t(std::abs(last));
                    state.values[v] = last > 0 ? 1 : -1; state.reason[v] = int(ci); changed = true;
                }
            }
            if (++steps > 20000 || std::chrono::steady_clock::now() > deadline) return -1;
        } while (changed);
        size_t variable = 0;
        // Prefer unresolved clauses to unconstrained optional components.
        for (const auto &c : clauses) {
            bool satisfied = false;
            for (int lit : c.literals) {
                int value = state.values[size_t(std::abs(lit))];
                if ((lit > 0 && value > 0) || (lit < 0 && value < 0)) { satisfied = true; break; }
            }
            if (!satisfied) for (int lit : c.literals) if (!state.values[size_t(std::abs(lit))]) {
                variable = size_t(std::abs(lit)); break;
            }
            if (variable) break;
        }
        if (!variable) {
            for (size_t i=1;i<state.values.size();++i) if (!state.values[i]) state.values[i]=prefer[i]?1:-1;
            solution = std::move(state); return 1;
        }
        bool uncertain = false; std::set<int> combined;
        for (int direction : {prefer[variable] ? 1 : -1, prefer[variable] ? -1 : 1}) {
            auto branch = state; branch.values[variable] = static_cast<signed char>(direction);
            branch.reason[variable] = -1;
            std::set<int> failed;
            int result = solve(std::move(branch), solution, failed, steps, deadline, depth+1);
            if (result == 1) return 1;
            uncertain |= result < 0;
            combined.insert(failed.begin(), failed.end());
            if (uncertain) break;
        }
        why = std::move(combined); return uncertain ? -1 : 0;
    }
};
struct Node {
    Json metadata, versions, clauses;
    std::string file, name, id;
    std::vector<int> parents;
    int literal = 0;
    bool root = false;
};
struct Origin { std::string code, message; std::set<std::string> files; };
struct Uncertain { int owner, guard; std::string code, message; };
int range_match(const Json &range, const std::string &version, const std::string &dialect, int depth = 0) {
    if (depth > 32) return -1;
    if (range.is_string()) return mod_version_matches(range.get<std::string>(), version, dialect);
    bool all = range.is_object() && range.size() == 1 && range.contains("all");
    Json values = range.is_array() ? range : range.is_object() && range.size() == 1 &&
        (all || range.contains("any")) ? range.begin().value() : Json();
    if (!values.is_array() || values.empty() || values.size() > 4096) return -1;
    bool unknown = false;
    for (const auto &value : values) {
        int result = range_match(value, version, dialect, depth + 1);
        if (all && result == 0) return 0;
        if (!all && result == 1) return 1;
        unknown |= result < 0;
    }
    return unknown ? -1 : all ? 1 : 0;
}
Json own_versions(const Json &m) {
    if (m.contains("own_provided_versions") && m.at("own_provided_versions").is_object())
        return m.at("own_provided_versions");
    Json values = Json::object();
    auto embedded = m.value("embedded_hashes", Json::object());
    auto primary = m.value("id", "");
    for (const auto &id : m.value("ids", Json::array())) if (id.is_string()) {
        auto key = id.get<std::string>();
        if (key != primary && embedded.contains(key)) continue;
        values[key] = m.value("provided_versions", Json::object()).value(key, m.value("version", ""));
    }
    if (!primary.empty() && !values.contains(primary)) values[primary] = m.value("version", "");
    return values;
}
Json dependency_clauses(const Json &m) {
    if (m.contains("dependency_clauses") && m.at("dependency_clauses").is_array()) return m.at("dependency_clauses");
    Json out = Json::array();
    auto dialect = m.value("loader", "") == "forge" || m.value("loader", "") == "neoforge" ? "maven" : "fabric";
    for (auto field : {"requirements", "breaks", "conflicts"}) {
        auto map = m.value(field, Json::object());
        if (!map.is_object()) {
            out.push_back({{"kind", field == std::string("requirements") ? "depends" : field},
                {"expression", {{"op", "unknown"}}}}); continue;
        }
        for (const auto &item : map.items()) out.push_back({{"kind", field == std::string("requirements") ? "depends" : field},
            {"expression", {{"op", "atom"}, {"id", item.key()}, {"range", item.value()}, {"dialect", dialect}}}});
    }
    return out;
}
void expression_ids(const Json &expression, std::set<std::string> &out, int depth = 0) {
    if (!expression.is_object() || depth > 32) return;
    if (expression.value("op", "") == "atom") out.insert(expression.value("id", ""));
    for (const auto &child : expression.value("children", Json::array())) expression_ids(child, out, depth + 1);
}
}

Json check_mods(const Json &instance, const Json &mods, bool allow_unknown, bool allow_missing) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
    Json errors = Json::array(), warnings = Json::array(), issues = Json::array();
    std::set<std::string> emitted;
    auto emit = [&](bool error, const std::string &code, const std::string &message, const std::set<std::string> &files) {
        Json names = Json::array(); for (const auto &f : files) if (!f.empty()) names.push_back(f);
        auto key = code + "\n" + message + names.dump();
        if (!emitted.insert(key).second) return;
        (error ? errors : warnings).push_back(message);
        issues.push_back({{"severity", error ? "error" : "warning"}, {"code", code}, {"message", message}, {"files", names}});
    };
    const auto loader = instance.value("loader", "vanilla");
    const auto optifine = instance.value("optifine", Json::object());
    std::map<std::string, std::string> runtime{{"minecraft", instance.value("version", "")},
        {"java", instance.value("java_major", 0) > 0 ? std::to_string(instance.value("java_major", 0)) : ""}};
    const std::map<std::string, std::string> loaders{{"fabricloader","fabric"},{"quilt_loader","quilt"},{"forge","forge"},{"neoforge","neoforge"}};
    for (const auto &entry : loaders) if (entry.second == loader) runtime[entry.first] = instance.value("loader_version", "");
    if (loader=="quilt") {
        runtime["builtin:minecraft"]=runtime["minecraft"];
        runtime["builtin:java"]=runtime["java"];
        runtime["org.quiltmc:quilt_loader"]=runtime["quilt_loader"];
    }
    if (!optifine.empty()) runtime["optifine"] = optifine.value("version", "");
    std::vector<Node> nodes;
    bool unidentified_provider=false;
    for (const auto &m:mods) if(m.value("enabled",true) && !m.value("known",false) && m.value("valid_archive",false)) unidentified_provider=true;
    bool node_limit = false;
    std::function<void(const Json &, const std::string &, int)> collect = [&](const Json &m, const std::string &file, int parent) {
        if (nodes.size() >= 2048 || std::chrono::steady_clock::now()>deadline) { node_limit = true; return; }
        if (m.value("environment", "*") == "server" || m.value("environment", "*") == "dedicated_server") {
            if (parent < 0) emit(false,"server_only","仅服务端模组，不参与客户端依赖检查。",{file});
            return;
        }
        if (!m.value("known", false)) {
            emit(parent < 0 && !allow_unknown,"unknown_metadata",m.value("name",file)+"："+m.value("problem","元数据未知"),{file});
            return;
        }
        auto actual = m.value("loader", "");
        Node node{m, own_versions(m), dependency_clauses(m), file, m.value("name",file), m.value("id",""),{},0,parent < 0};
        if (parent >= 0) node.parents.push_back(parent);
        auto index = int(nodes.size()); nodes.push_back(std::move(node));
        for (const auto &child : m.value("components", Json::array())) collect(child,file,index);
        // Old external API callers may supply aggregate IDs without child
        // records. Preserve those as explicit candidates, but never claim that
        // their unprovided dependency metadata was checked.
        if (m.value("components", Json::array()).empty()) {
            auto embedded = m.value("embedded_hashes", Json::object());
            for (const auto &item : embedded.items()) {
                Json child{{"id",item.key()},{"name",item.key()},{"known",true},{"loader",actual},
                    {"ids",Json::array({item.key()})},{"version",m.value("provided_versions",Json::object()).value(item.key(),"")},
                    {"load_type","if_required"},{"metadata_warnings",Json::array({
                        {{"code","embedded_metadata_unknown"},{"message","内嵌组件缺少完整依赖元数据，需由实际加载器确认。"}}})}};
                collect(child,file,index);
            }
        }
    };
    for (const auto &m : mods) if (m.value("enabled",true)) collect(m,m.value("filename",""),-1);
    std::set<std::string> all_files;
    for (const auto &m:mods) if(m.value("enabled",true))all_files.insert(m.value("filename",""));
    if (node_limit) emit(false,"dependency_check_limit","依赖检查达到复杂度上限，请使用加载器日志进一步确认。",all_files);
    Cnf formula; formula.add({1});
    std::vector<Origin> origins;
    std::map<std::string,std::vector<int>> providers;
    std::map<std::string,std::vector<int>> artifacts;
    std::map<std::string,int> roots;
    size_t provider_count=0;
    for (size_t i=0;i<nodes.size();++i) {
        auto &node=nodes[i];
        node.literal=formula.variable(node.root || node.metadata.value("load_type", "if_possible") != "if_required");
        if (node.root) formula.add({node.literal});
        auto disallow=[&](const std::string &code,const std::string &message) {
            auto oi=int(origins.size());origins.push_back({code,message,{node.file}});
            formula.add({-node.literal},oi);
            if (node.root) emit(true,code,message,{node.file});
        };
        const auto actual=node.metadata.value("loader", "");
        if (!node.metadata.value("library",false) && actual!=loader && !(loader=="quilt" && actual=="fabric") && actual!="optifine")
            disallow("loader_mismatch",node.name+"：加载器不匹配（"+actual+" / "+loader+"）。");
        if (actual=="optifine" && !(loader=="forge" && optifine.value("sha512","")==node.metadata.value("sha512","")))
            disallow("unmanaged_optifine","OptiFine 必须由版本组件安装流程验证，不能作为普通模组直接启用。");
        for (const auto &id : node.versions.items()) {
            if (++provider_count>16384 || std::chrono::steady_clock::now()>deadline) {formula.exhausted=true;break;}
            if ((runtime.count(id.key()) && id.key() != "optifine") || loaders.count(id.key()) ||
                (loader=="quilt" && id.key()=="org.quiltmc:fabricloader"))
                disallow("runtime_as_mod","加载器与 Java 是实例版本组件，不能由 mods 文件替代："+id.key());
            auto &list=providers[id.key()];
            if (list.size() >= 128) { formula.exhausted=true; continue; }
            list.push_back(int(i));
            if (node.root) {
                auto old=roots.find(id.key());
                if (old!=roots.end()) emit(true,"duplicate_id","重复模组 ID："+id.key()+"（"+nodes[size_t(old->second)].file+" / "+node.file+"）。",{nodes[size_t(old->second)].file,node.file});
                else roots[id.key()]=int(i);
            }
        }
        const auto jarjar=node.metadata.value("jarjar",Json::object());
        if(!jarjar.empty())artifacts[jarjar.value("group","")+":"+jarjar.value("artifact","")].push_back(int(i));
    }
    for (const auto &entry : providers) {
        if (formula.exhausted) break;
        for (size_t a=0;a<entry.second.size() && !formula.exhausted;++a)
            for (size_t b=a+1;b<entry.second.size() && !formula.exhausted;++b)
                formula.add({-nodes[size_t(entry.second[a])].literal,-nodes[size_t(entry.second[b])].literal});
    }
    for (const auto &entry:artifacts) {
        if(entry.second.size()>128){formula.exhausted=true;break;}
        for(size_t a=0;a<entry.second.size() && !formula.exhausted;++a)
            for(size_t b=a+1;b<entry.second.size() && !formula.exhausted;++b)
                formula.add({-nodes[size_t(entry.second[a])].literal,-nodes[size_t(entry.second[b])].literal});
    }
    for (const auto &node : nodes) if (!node.root) {
        std::vector<int> parents{-node.literal};
        for (int p:node.parents) parents.push_back(nodes[size_t(p)].literal);
        formula.add(std::move(parents));
        if (node.metadata.value("load_type", "") == "always" && !node.id.empty() && !node.metadata.contains("jarjar")) {
            std::vector<int> candidates;
            for (int p:providers[node.id]) candidates.push_back(nodes[size_t(p)].literal);
            int loaded=formula.join(candidates,false);
            for (int p:node.parents) formula.add({-nodes[size_t(p)].literal,loaded});
        }
    }
    std::vector<Uncertain> uncertain;
    auto unknown = [&](int owner,int guard,const std::string &code,const std::string &message) {
        if(uncertain.size()>=12000){formula.exhausted=true;return 1;}
        uncertain.push_back({owner,guard,code,message}); return formula.variable();
    };
    std::function<int(const Json &,int,bool,Origin &,int)> expression = [&](const Json &expr,int owner,bool positive,Origin &origin,int depth) -> int {
        if (formula.exhausted || std::chrono::steady_clock::now()>deadline) {formula.exhausted=true;return 1;}
        if (!expr.is_object() || depth>32)
            return unknown(owner,1,"unknown_range","依赖表达式无法完整核对，需手动确认。");
        auto op=expr.value("op",""); int result;
        if (op=="any" || op=="all") {
            auto children=expr.value("children",Json::array());
            if (!children.is_array() || children.empty() || children.size()>4096)
                return unknown(owner,1,"unknown_range","依赖表达式无法完整核对，需手动确认。");
            std::vector<int> values;
            for (const auto &child:children) {
                auto child_origin=origin;
                values.push_back(expression(child,owner,positive,child_origin,depth+1));
                origin.files.insert(child_origin.files.begin(),child_origin.files.end());
            }
            result=formula.join(values,op=="all");
        } else if (op=="atom") {
            auto id=expr.value("id",""); auto range=expr.value("range",Json("*"));
            auto dialect=expr.value("dialect", nodes[size_t(owner)].metadata.value("loader", "fabric"));
            std::vector<int> matches, present;
            if ((id=="fabricloader" || id=="org.quiltmc:fabricloader") && loader=="quilt") {
                result=unknown(owner,1,"quilt_fabric_loader","Quilt 下的 Fabric Loader 版本约束需由实际加载器确认。");
            } else if (runtime.count(id)) {
                const auto &v=runtime[id];
                int match=v.empty() ? -1 : range_match(range,v,dialect);
                result=match<0 ? unknown(owner,1,v.empty()?"runtime_unknown":"unknown_range",
                    (v.empty()?"运行环境版本未知，无法完整检查：":"无法完整比较版本范围：")+id) : match ? 1 : -1;
                if (match==0 && positive) { origin.code="dependency_version"; origin.message=nodes[size_t(owner)].name+" 前置版本不符合："+id; }
            } else {
                auto found=providers.find(id);
                if (found!=providers.end()) for (int index:found->second) {
                    const auto &candidate=nodes[size_t(index)];
                    present.push_back(candidate.literal); origin.files.insert(candidate.file);
                    int match=range_match(range,candidate.versions.at(id).get<std::string>(),dialect);
                    if (match>0) matches.push_back(candidate.literal);
                    else if (match<0) {
                        int maybe=unknown(owner,candidate.literal,positive?"unknown_range":"unknown_conflict_range",
                            (positive?"无法完整比较版本范围：":"无法完整检查冲突版本范围：")+id);
                        matches.push_back(formula.join({candidate.literal,maybe},true));
                    }
                }
                result=formula.join(matches,false);
                if (present.empty() && loader=="quilt" && id=="mixinextras")
                    result=unknown(owner,1,"loader_bundled_unknown","加载器内置组件及其版本尚未确认："+id);
                if (positive && expr.value("optional",false)) result=formula.join({-formula.join(present,false),result},false);
                if (result==-1 && positive) {
                    if (allow_missing && present.empty()) result=1;
                    else if (allow_unknown && unidentified_provider && present.empty())
                        result=unknown(owner,1,"dependency_provider_unknown","存在未识别组件，无法确认是否缺少前置："+id);
                    else {
                        origin.code=present.empty()?"missing_dependency":"dependency_version";
                        origin.message=nodes[size_t(owner)].name+(present.empty()?" 缺少前置：":" 前置版本不符合：")+id;
                    }
                }
            }
        } else result=unknown(owner,1,"unknown_range","依赖表达式无法完整核对，需手动确认。");
        if (expr.contains("environment")) {
            // Quilt provider extensions do not have uniform enforcement.
            result=unknown(owner,1,"dependency_environment","依赖环境条件尚未完整核对，需手动确认。");
        }
        if (expr.contains("unless")) {
            auto ignored=origin;
            int unless=expression(expr.at("unless"),owner,true,ignored,depth+1);
            result=positive ? formula.join({unless,result},false) : formula.join({-unless,result},true);
        }
        return result;
    };
    for (size_t n=0;n<nodes.size();++n) {
        if (formula.exhausted || std::chrono::steady_clock::now()>deadline) {formula.exhausted=true;break;}
        auto &node=nodes[n];
        // JarJar negotiates artifact coordinates, which are distinct from mod
        // IDs. Selecting one artifact activates only that candidate's mod IDs
        // and dependencies; plain libraries participate without invented IDs.
        for(const auto &required:node.metadata.value("jarjar_libraries",Json::array())) {
            auto coordinate=required.value("group","")+":"+required.value("artifact","");
            std::vector<int> allowed;std::set<std::string> files{node.file};
            for(int index:artifacts[coordinate]) {
                const auto &candidate=nodes[size_t(index)];
                const auto &record=candidate.metadata.at("jarjar");
                files.insert(candidate.file);
                auto match=range_match(required.value("range",Json("")),record.value("version",""),"maven");
                if(match>0)allowed.push_back(candidate.literal);
                else if(match<0) {
                    int maybe=unknown(int(n),candidate.literal,"unknown_range","无法完整比较版本范围："+coordinate);
                    allowed.push_back(formula.join({candidate.literal,maybe},true));
                }
            }
            int oi=int(origins.size());origins.push_back({"dependency_selection_conflict",node.name+" 依赖组合无法同时满足："+coordinate,files});
            int acceptable=formula.join(allowed,false);
            formula.add({-node.literal,acceptable},oi);
            if (node.root && acceptable==-1) {
                const auto &o=origins[size_t(oi)];emit(true,o.code,o.message,o.files);
            }
        }
        for (const auto &raw:node.clauses) {
            auto kind=raw.value("kind","");
            if (kind!="depends" && kind!="breaks" && kind!="conflicts") continue;
            if (raw.value("side","BOTH")=="SERVER") continue;
            auto expr=raw.value("expression",Json());
            std::set<std::string> ids; expression_ids(expr,ids);
            std::string joined; for (const auto &id:ids) { if(!joined.empty())joined+=", "; joined+=id; }
            Origin origin{kind=="depends"?"dependency_selection_conflict":"declared_conflict",
                kind=="depends"?node.name+" 依赖组合无法同时满足："+joined:node.name+" 与 "+joined+" 不兼容。",{node.file}};
            int value=expression(expr,int(n),kind=="depends",origin,0);
            int oi=int(origins.size());origins.push_back(std::move(origin));
            if (kind!="conflicts") {
                const int required=kind=="depends"?value:-value;
                formula.add({-node.literal,required},oi);
                // Each independently impossible root requirement deserves a
                // diagnostic, even if another conflict ends the SAT proof first.
                if (node.root && required==-1) {
                    const auto &o=origins.back();emit(true,o.code,o.message,o.files);
                }
            }
            else uncertain.push_back({int(n),value,"declared_conflict",origins.back().message});
        }
        auto language=node.metadata.value("language_loader",Json::object());
        if (!language.empty()) {
            std::string v;
            if (language.value("version_source","")=="forge_major") {
                auto value=instance.value("loader_version","");
                auto dot=value.find('.'); v=value.substr(0,dot);
            } else if (language.value("version_source","")=="fml") v=instance.value("fml_version","");
            auto id=language.value("id","");
            int match=v.empty() ? -1 : range_match(language.value("range",Json("*")),v,"maven");
            if (match<0) uncertain.push_back({int(n),1,"language_loader_unknown","语言加载器版本尚未确认："+id});
            else if (!match) {
                int oi=int(origins.size());origins.push_back({"language_loader_version",node.name+" 语言加载器版本不符合："+id,{node.file}});
                formula.add({-node.literal},oi);
                if (node.root) {const auto &o=origins.back();emit(true,o.code,o.message,o.files);}
            }
        }
        for (const auto &entry:node.metadata.value("metadata_warnings",Json::array()))
            uncertain.push_back({int(n),1,entry.value("code","metadata_incomplete"),entry.value("message","模组元数据尚未完整核对。")});
    }
    // Known rendering exclusions apply to the selected providers, including a
    // managed OptiFine component which is not an ordinary mods-folder file.
    for (const auto &pair:std::vector<std::pair<std::string,std::string>>{{"iris","optifine"},{"iris","optifabric"},{"sodium","optifine"},{"oculus","optifine"}}) {
        std::vector<int> left,right;std::set<std::string> affected;
        for (int n:providers[pair.first]) {left.push_back(nodes[size_t(n)].literal);affected.insert(nodes[size_t(n)].file);}
        for (int n:providers[pair.second]) {right.push_back(nodes[size_t(n)].literal);affected.insert(nodes[size_t(n)].file);}
        if (runtime.count(pair.second))right.push_back(1);
        if (left.empty() || right.empty())continue;
        int oi=int(origins.size());origins.push_back({"known_rendering_conflict",pair.first+" 与 "+pair.second+" 不能同时启用。",affected});
        formula.add({-formula.join(left,false),-formula.join(right,false)},oi);
    }
    Cnf::State initial{std::vector<signed char>(formula.prefer.size(),0),std::vector<int>(formula.prefer.size(),-1)},solution;
    size_t steps=0;std::set<int> why;
    int verdict=node_limit?-1:formula.solve(std::move(initial),solution,why,steps,deadline);
    if (verdict==0) {
        if (why.empty()) emit(true,"dependency_selection_conflict","依赖组合无法同时满足，请检查同一前置的版本及冲突声明。",all_files);
        else for (int oi:why) {const auto &o=origins[size_t(oi)];emit(true,o.code,o.message,o.files);}
    } else if (verdict<0) emit(false,"dependency_check_limit","依赖检查达到复杂度上限，请使用加载器日志进一步确认。",all_files);
    if (verdict==0) {
        // An unsatisfiable graph has no complete assignment. A single bounded
        // proof need not enumerate every disconnected cause, so do not label
        // other enabled rows as jointly checked / conflict-free.
        std::set<std::string> diagnosed;
        for (const auto &issue:issues) if(issue.value("severity","")=="error")
            for(const auto &file:issue.value("files",Json::array())) if(file.is_string())diagnosed.insert(file.get<std::string>());
        for(const auto &file:all_files) if(!diagnosed.count(file))
            emit(false,"dependency_graph_unresolved","实例存在依赖冲突，尚未完成此组件的联合兼容性核对。",{file});
    }
    auto active=[&](int lit) {if(lit==1)return true;if(lit==-1)return false;
        if(verdict!=1)return true;int v=solution.values[size_t(std::abs(lit))];return lit>0?v>0:v<0;};
    for (const auto &u:uncertain) if (active(nodes[size_t(u.owner)].literal) && active(u.guard))
        emit(false,u.code,u.message,{nodes[size_t(u.owner)].file});
    if (loader=="quilt") {
        auto list=compatibility_rules(fs::u8path(instance.value("game_directory","")));
        for (const auto &rule:list.value("rules",Json::array())) for (const auto &id:rule.value("ids",Json::array())) if(id.is_string()) {
            auto it=providers.find(id.get<std::string>());if(it==providers.end())continue;
            std::set<std::string> files;
            for (int n:it->second) if(active(nodes[size_t(n)].literal))files.insert(nodes[size_t(n)].file);
            if(!files.empty())emit(false,"quilt_rule","Quilt 官方兼容性提醒："+rule.value("name","")+"。未提供精确文件哈希，需核对具体构建。",files);
        }
    }
    Json chosen=Json::array();
    if (verdict==1) for(const auto &node:nodes) if(active(node.literal)) chosen.push_back({{"id",node.id},{"version",node.metadata.value("version","")},{"file",node.file},{"nested",!node.root}});
    return {{"errors",errors},{"warnings",warnings},{"issues",issues},
        {"selection",chosen},{"selection_status",!errors.empty()?"conflict":verdict==1?"satisfiable":verdict==0?"conflict":"unknown"}};
}
} // namespace solar::engine
