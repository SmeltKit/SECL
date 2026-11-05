// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
#include "solar/engine.hpp"
#include <iostream>
using namespace solar; using namespace solar::engine;
static int assertions=0;
static void check(bool value,const char *why){++assertions;if(!value)throw std::runtime_error(why);}
static Json mod(std::string id,std::string version="1.0.0",Json requirements=Json::object()) {
 return {{"known",true},{"enabled",true},{"loader","fabric"},{"id",id},{"name",id},{"filename",id+".jar"},
 {"version",version},{"ids",Json::array({id})},{"requirements",requirements},{"sha512",id+version}};
}
static Json atom(const std::string &id,Json range="*",bool optional=false) {
 return {{"op","atom"},{"id",id},{"range",range},{"dialect","quilt"},{"optional",optional}};
}
static Json clause(const std::string &kind,Json expression) {return {{"kind",kind},{"expression",expression}};}
static Json nested(Json parent,Json child) {
 child["load_type"]="if_possible"; parent["components"].push_back(child);return parent;
}
static bool code(const Json &report,const std::string &name) {
 for(const auto &i:report.at("issues"))if(i.at("code")==name)return true;return false;
}
static bool selected(const Json &report,const std::string &id,const std::string &version="") {
 for(const auto &i:report.at("selection"))if(i.at("id")==id && (version.empty() || i.at("version")==version))return true;return false;
}
int main(){try{
 Json context{{"loader","fabric"},{"loader_version","0.16.10"},{"version","1.20.1"},{"java_major",17}};
 auto p1=nested(mod("container_a"),mod("shared","1.0.0"));
 auto p2=nested(mod("container_b"),mod("shared","2.0.0"));
 auto a=mod("consumer_a","1",{{"shared","<2"}}),b=mod("consumer_b","1",{{"shared",">=2"}});
 auto impossible=check_mods(context,Json::array({p1,p2,a,b}));
 check(!impossible["errors"].empty(),"incompatible consumers cannot use independent embedded versions");
 check(impossible["selection_status"]=="conflict","inconsistent global graph is explicitly unsatisfiable");
 b["requirements"]["shared"]=">=1";
 auto possible=check_mods(context,Json::array({p1,p2,a,b}));
 check(possible["errors"].empty(),"overlapping constraints have a single common provider");
 check(selected(possible,"shared","1.0.0") && !selected(possible,"shared","2.0.0"),"one selected version satisfies both consumers");
 p2["components"][0]["requirements"]={{"missing_child_dep","*"}};
 auto inactive=check_mods(context,Json::array({p1,p2,a,b}));
 check(inactive["errors"].empty(),"unselected embedded candidate dependencies do not cause false missing errors");
 auto breaker=mod("breaker");breaker["breaks"]={{"shared",">=2"}};
 auto forward=check_mods(context,Json::array({p1,p2,breaker,b}));
 auto reverse=check_mods(context,Json::array({b,breaker,p2,p1}));
 check(forward["errors"].empty() && reverse["errors"].empty(),"nested breaks do not depend on directory iteration order");
 check(selected(forward,"shared","1.0.0") && selected(reverse,"shared","1.0.0"),"breaks constrain the selected shared provider");
 auto alias=mod("library","1.0.0");alias["ids"].push_back("alias");alias["provided_versions"]={{"library","1.0.0"},{"alias","2.0.0"}};
 auto consumer=mod("alias_user","1",{{"alias",">=2"},{"library","<2"}});
 check(check_mods(context,Json::array({alias,consumer}))["errors"].empty(),"alias-specific provided versions are honored");
 auto alias2=mod("other_library");alias2["ids"].push_back("alias");
 check(code(check_mods(context,Json::array({alias,alias2})),"duplicate_id"),"two root providers cannot own the same alias");
 auto q=mod("quilt_root");q["loader"]="quilt";
 q["dependency_clauses"]=Json::array({clause("depends",{{"op","any"},{"children",Json::array({atom("absent"),atom("present")})}})});
 auto present=mod("present");present["loader"]="quilt";
 auto quilt=context;quilt["loader"]="quilt";quilt["loader_version"]="0.28.0";
 check(check_mods(quilt,Json::array({q,present}))["errors"].empty(),"Quilt dependency alternatives are OR");
 q["dependency_clauses"]=Json::array({clause("depends",atom("builtin:minecraft","=1.20.1")),
  clause("depends",atom("builtin:java",">=17")),clause("depends",atom("org.quiltmc:quilt_loader",">=0.20"))});
 check(check_mods(quilt,Json::array({q}))["errors"].empty(),"Quilt qualified builtin aliases resolve exact runtime groups");
 q["dependency_clauses"]=Json::array({clause("depends",atom("org.quiltmc:fabricloader",">=0.15"))});
 auto fabric_shim=check_mods(quilt,Json::array({q}));
 check(fabric_shim["errors"].empty() && code(fabric_shim,"quilt_fabric_loader"),"qualified Fabric shim version remains explicit uncertainty on Quilt");
 q["dependency_clauses"]=Json::array({clause("depends",atom("other.group:minecraft"))});
 check(code(check_mods(quilt,Json::array({q})),"missing_dependency"),"arbitrary group prefixes cannot impersonate builtin aliases");
 q["dependency_clauses"]=Json::array({clause("depends",atom("mixinextras",">=0.3"))});
 auto bundled=check_mods(quilt,Json::array({q}));
 check(bundled["errors"].empty() && code(bundled,"loader_bundled_unknown"),"loader-bundled library is not falsely declared missing without artifact inspection");
 auto cond=atom("missing_a");auto unless=atom("missing_b");unless["unless"]=atom("present");cond["unless"]=unless;
 q["dependency_clauses"]=Json::array({clause("depends",cond)});
 check(check_mods(quilt,Json::array({q,present}))["errors"].empty(),"nested unless uses positive dependency semantics");
 q["dependency_clauses"]=Json::array({clause("breaks",atom("absent","*",true))});
 check(check_mods(quilt,Json::array({q}))["errors"].empty(),"optional break does not match an absent mod");
 q["dependency_clauses"]=Json::array({clause("breaks",{{"op","all"},{"children",Json::array({atom("present"),atom("absent")})}})});
 check(check_mods(quilt,Json::array({q,present}))["errors"].empty(),"grouped break requires every member");
 q["dependency_clauses"]=Json::array({clause("breaks",atom("present"))});
 check(code(check_mods(quilt,Json::array({q,present})),"declared_conflict"),"Quilt explicit break matches selected alias");
 q["environment"]="dedicated_server";q["dependency_clauses"]=Json::array({clause("depends",atom("absent"))});
 auto server=check_mods(quilt,Json::array({q}));
 check(server["errors"].empty() && code(server,"server_only"),"dedicated-server Quilt mods do not create client requirements");
 auto opaque=mod("opaque","opaque-build");auto opaque_user=mod("opaque_user","1",{{"opaque",">=1.0"}});
 auto unknown=check_mods(context,Json::array({opaque,opaque_user}));
 check(unknown["errors"].empty() && code(unknown,"unknown_range"),"unsupported comparison becomes unknown rather than a hard mismatch");
 auto foreign=mod("foreign");foreign["loader"]="forge";
 auto root=nested(mod("root"),foreign);root["breaks"]={{"foreign","*"}};
 auto ignored=check_mods(context,Json::array({root}));
 check(ignored["errors"].empty() && !selected(ignored,"foreign"),"unselected wrong-loader child does not emit unconditional hard errors");
 auto legacy=mod("legacy","1",{{"embedded","*"}});legacy["ids"].push_back("embedded");legacy["embedded_hashes"]={{"embedded","hash"}};legacy["provided_versions"]={{"embedded","1"}};
 auto limited=check_mods(context,Json::array({legacy}));
 check(limited["errors"].empty() && code(limited,"embedded_metadata_unknown"),"selected legacy aggregate retains uncertainty about child dependencies");
 auto optical=mod("optifine");optical["loader"]="optifine";optical["sha512"]="verified";
 auto forge=context;forge["loader"]="forge";forge["loader_version"]="47.2.18";forge["optifine"]={{"sha512","verified"},{"version","HD_U_I6"}};
 check(check_mods(forge,Json::array({optical}))["errors"].empty(),"managed OptiFine is not falsely treated as a forbidden runtime mod");
 auto iris=mod("iris");iris["loader"]="forge";
 check(code(check_mods(forge,Json::array({iris,optical})),"known_rendering_conflict"),"managed component still conflicts with selected Iris");
 auto unidentified=Json{{"known",false},{"valid_archive",true},{"name","unidentified"},{"filename","unidentified.jar"}};
 auto requiring=mod("requiring","1",{{"maybe_provided","*"}});
 auto cautious=check_mods(context,Json::array({unidentified,requiring}),true);
 check(cautious["errors"].empty() && code(cautious,"dependency_provider_unknown"),"unknown readable archive cannot prove absence of its possible aliases");
 unidentified["valid_archive"]=false;
 check(code(check_mods(context,Json::array({unidentified,requiring}),true),"missing_dependency"),"corrupt archive cannot satisfy an absent dependency");
 auto language=mod("language");language["loader"]="forge";language["language_loader"]={{"id","javafml"},{"range","[48,)"},{"version_source","forge_major"}};
 check(code(check_mods(forge,Json::array({language})),"language_loader_version"),"Forge JavaFML major is checked separately");
 language["loader"]="neoforge";language["language_loader"]["version_source"]="fml";auto neo=forge;neo["loader"]="neoforge";neo["loader_version"]="20.6.1";
 auto no_fml=check_mods(neo,Json::array({language}));
 check(no_fml["errors"].empty() && code(no_fml,"language_loader_unknown"),"NeoForge version is not confused with language-loader version");
 auto jar_parent=[](const std::string &id,const std::string &version,const std::string &range,bool with_mod=false) {
  auto parent=mod(id);parent["loader"]="forge";
  Json artifact{{"group","example"},{"artifact","shared"},{"version",version},{"range",range}};
  auto child=mod("nested_mod_"+version,version);child["loader"]="forge";child["load_type"]="always";child["jarjar"]=artifact;
  if(!with_mod){child["library"]=true;child["ids"]=Json::array();child["id"]="";}
  parent["components"]=Json::array({child});parent["jarjar_libraries"]=Json::array({artifact});return parent;
 };
 auto jj1=jar_parent("jar_container1","1.0","[1,3)");
 auto jj2=jar_parent("jar_container2","2.0","[2,3)");
 auto negotiated=check_mods(forge,Json::array({jj1,jj2}));
 check(negotiated["errors"].empty(),"JarJar plain libraries negotiate by artifact identity and shared ranges");
 jj1["jarjar_libraries"][0]["range"]="[1,2)";
 check(!check_mods(forge,Json::array({jj1,jj2}))["errors"].empty(),"disjoint JarJar artifact ranges cannot each accept different versions");
 auto jm1=jar_parent("jar_mod_container1","1.0","[1,3)",true);
 auto jm2=jar_parent("jar_mod_container2","2.0","[2,3)",true);
 auto chosen_mod=check_mods(forge,Json::array({jm1,jm2}));
 check(chosen_mod["errors"].empty() && selected(chosen_mod,"nested_mod_2.0") && !selected(chosen_mod,"nested_mod_1.0"),"JarJar artifact choice activates only the selected version's mod IDs");
 jm1["components"][0]["requirements"]={{"absent_in_unused_artifact","*"}};
 check(check_mods(forge,Json::array({jm1,jm2}))["errors"].empty(),"unselected JarJar artifact dependencies do not leak into active graph");
 auto missing_one=mod("missing_one","1",{{"absent_one","*"}});
 auto missing_two=mod("missing_two","1",{{"absent_two","*"}});
 auto independent=check_mods(context,Json::array({missing_one,missing_two,mod("otherwise_healthy")}));
 check(independent["errors"].size()==2,"independent impossible root requirements are all reported despite the first SAT contradiction");
 bool healthy_review=false;
 for(const auto &issue:independent["issues"]) if(issue.value("code","")=="dependency_graph_unresolved")
  healthy_review=issue["files"]==Json::array({"otherwise_healthy.jar"});
 check(healthy_review,"unresolved joint graph cannot claim other enabled files are fully compatible");
 // Compare the solver with an exhaustive two-candidate oracle. This exercises
 // alternate ordering and mutually inconsistent positive/negative constraints.
 for (const auto &x:std::vector<std::string>{"*","<2",">=2",">=1 <3","=1.0.0","=2.0.0",">3"})
 for (const auto &y:std::vector<std::string>{"*","<2",">=2",">=1 <3","=1.0.0","=2.0.0",">3"})
 for (const auto &negative:std::vector<std::string>{"","<2",">=2"}) {
  bool expected=false;
  for(const auto &v:std::vector<std::string>{"1.0.0","2.0.0"})
   expected|=version_matches(x,v)==1 && version_matches(y,v)==1 && (negative.empty() || version_matches(negative,v)==0);
  auto first=nested(mod("first_container"),mod("shared","1.0.0"));
  auto second=nested(mod("second_container"),mod("shared","2.0.0"));
  auto one=mod("one","1",{{"shared",x}}),two=mod("two","1",{{"shared",y}}),not_mod=mod("negative");
  if(!negative.empty())not_mod["breaks"]={{"shared",negative}};
  auto report=check_mods(context,Json::array({first,second,one,two,not_mod}));
  check(report["errors"].empty()==expected,"global solver agrees with exhaustive shared-version oracle");
 }
 Json large=Json::array();for(int n=0;n<2055;++n)large.push_back(mod("bounded_"+std::to_string(n)));
 auto bounded=check_mods(context,large);
 check(bounded["selection_status"]=="unknown" && code(bounded,"dependency_check_limit"),"bounded analysis reports uncertainty rather than claiming no conflicts");
 check(bounded["errors"].empty(),"resource limits do not invent a hard incompatibility");
 std::cout<<"PASS "<<assertions<<" mod constraint checks\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<" after "<<assertions<<" checks\n";return 1;}}
