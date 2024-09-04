#include "cxx_lang_provider.h"

#include <re/buildenv.h>
#include <re/target.h>

#include <re/target_cfg_utils.h>
#include <re/yaml_merge.h>

#include <fstream>
#include <futile/futile.h>
#include <ulib/fmt/list.h>
#include <ulib/format.h>
#include <ulib/strutility.h>

namespace re
{
    namespace
    {
        inline TargetConfig GetRecursiveMapCfg(const Target &leaf, std::string_view key)
        {
            auto result = TargetConfig{ulib::yaml::value_t::map};
            auto p = &leaf;

            while (p)
            {
                if (auto map = p->GetCfgEntry<TargetConfig>(key))
                    result = MergeYamlNodes(*map, result);

                p = p->parent;
            }

            return result;
        }

        inline TargetConfig GetRecursiveSeqCfg(const Target &leaf, std::string_view key)
        {
            auto result = TargetConfig{ulib::yaml::value_t::sequence};
            auto p = &leaf;

            while (p)
            {
                if (auto map = p->GetCfgEntry<TargetConfig>(key))
                    result = MergeYamlNodes(*map, result);

                p = p->parent;
            }

            return result;
        }

        inline void AppendIncludeDirs(const Target &target, const TargetConfig &cfg,
                                      std::unordered_set<std::string> &dirs, const LocalVarScope &vars)
        {
            if (target.type != TargetType::Project && !cfg.search("no-auto-include-dirs"))
            {
                auto root_path = cfg["cxx-root-include-path"].scalar();
                // fmt::print("AppendIncludeDirs: {} => root: {}\n", target.module, root_path);
                dirs.insert(root_path);
            }

            if (auto extra_includes = cfg.search("cxx-include-dirs"))
                if (extra_includes->is_sequence())
                    for (const auto &v : *extra_includes)
                    {
                        const auto &v_s = v.scalar();

                        auto dir = fs::path{vars.Resolve(v_s)};

                        if (!dir.is_absolute())
                            dir = target.path / dir;

                        dirs.insert(dir.u8string());
                    }
        }

        inline void AppendLinkFlags(const Target &target, const TargetConfig &cfg, const std::string &cxx_lib_dir_tpl,
                                    std::vector<std::string> &out_flags, std::vector<std::string> &out_deps,
                                    const LocalVarScope &vars)
        {
            auto link_lib_dirs = cfg.search("cxx-lib-dirs");
            if (link_lib_dirs && link_lib_dirs->is_sequence())
                for (const auto &dir : *link_lib_dirs)
                {
                    auto formatted = vars.Resolve(dir.scalar());
                    out_flags.push_back(fmt::format(cxx_lib_dir_tpl, fmt::arg("directory", formatted)));
                }

            auto extra_link_deps = cfg.search("cxx-link-deps");
            if (extra_link_deps && extra_link_deps->is_sequence())
                for (const auto &dep : *extra_link_deps)
                    out_deps.push_back(fmt::format("\"{}\"", vars.Resolve(dep.scalar())));
        }

    } // namespace

    CxxLangProvider::CxxLangProvider(const fs::path &env_search_path, LocalVarScope *var_scope)
        : mEnvSearchPath{env_search_path}, mVarScope{var_scope}
    {
    }

    void CxxLangProvider::InitInBuildDesc(NinjaBuildDesc &desc)
    {
        // Do nothing
    }

    void CxxLangProvider::InitLinkTargetEnv(NinjaBuildDesc &desc, Target &target)
    {
        auto path = GetEscapedModulePath(target);

        target.local_var_ctx = *mVarScope->GetContext();

        auto &target_vars = target.target_var_scope.emplace(&target.local_var_ctx, "target", &target);
        auto &vars = target.build_var_scope.emplace(&target.local_var_ctx, "build", &target_vars);

        std::unordered_map<std::string, std::string> cond_desc = {
            {"target-type", TargetTypeToString(target.type)},      {"platform", vars.ResolveLocal("platform")},
            {"host-platform", vars.ResolveLocal("host-platform")}, {"config", vars.ResolveLocal("configuration")},
            {"load-context", vars.ResolveLocal("load-context")},   {"runtime", vars.ResolveLocal("runtime")}};

        target.resolved_config = GetResolvedTargetCfg(target, cond_desc);

        // Choose and load the correct build environment.

        auto env_cfg = target.resolved_config.search("cxx-env");
        if (!env_cfg)
            RE_THROW TargetLoadException(&target, "C++ environment type not specified anywhere in the target tree");

        auto &env_cached_name = desc.state["re_cxx_env_for_" + path];
        env_cached_name = vars.Resolve(env_cfg->scalar());

        /////////////////////////////////////////////////////////////////

        //
        // This is guaranteed to either give us a working environment or to mess up the build.
        //
        CxxBuildEnvData &env = LoadEnvOrThrow(env_cached_name, target);

        if (auto vars_cfg = env.search("vars"))
            for (const auto &kv : vars_cfg->items())
            {
                auto key = kv.name();
                auto value = kv.value().scalar();

                vars.SetVar(key, value);

                /*
                // If a global var like that doesn't exist, create it with this one's value.
                if (!mVarScope->GetVar(key))
                    mVarScope->SetVar(key, vars.Resolve(value));
                    */
            }

        for (const auto &kv : env["default-flags"].items())
            vars.SetVar(ulib::string{"platform-default-flags-"} + kv.name(), vars.Resolve(kv.value().scalar()));

        cond_desc["arch"] = vars.ResolveLocal("arch");
        cond_desc["cxx-env"] = env_cached_name;
        cond_desc["cxxenv"] = env_cached_name;

        target.resolved_config = GetResolvedTargetCfg(target, cond_desc);

        target.LoadConditionalDependencies();

        vars.SetVar("src-dir", target.path.u8string());

        if (!vars.GetVar("root-dir"))
            vars.SetVar("root-dir", desc.pRootTarget->path.u8string());

        vars.SetVar("target-root", vars.GetVar("root-dir").value());
        vars.SetVar("build-root", desc.pRootTarget->path.u8string());

        if (auto withs_ref = target.resolved_config.search("with"))
        {
            auto withs = *withs_ref;
            for (auto &with : withs)
            {
                fs::path path = vars.Resolve(with.scalar());

                if (!path.is_absolute())
                    path = target.path / path;

                auto config = ulib::yaml::parse(futile::open(path).read());

                MergeYamlNode(target.config, config);
                target.resolved_config = GetResolvedTargetCfg(target, cond_desc);
            }
        }

        auto &meta = desc.meta["targets"][target.path.u8string()];
        auto &cxx = meta["cxx"];

        meta["type"] = TargetTypeToString(target.type);
        meta["module"] = target.module;
        meta["links_with"] = "cxx";

        cxx["toolchain"] = env_cached_name;

        for (auto &[k, v] : cond_desc)
            cxx[k] = v;

        std::string filename = vars.Resolve(target.GetCfgEntry<std::string>("artifact-name").value_or(target.module));

        std::string extension = "";

        if (auto out_ext = target.resolved_config.search("out-ext"))
            if (out_ext->is_scalar())
                extension = out_ext->scalar();

        if (!extension.empty())
        {
            filename.append(".");
            filename.append(extension);
        }

        vars.SetVar("build-artifact", filename);

        // fmt::print("InitLinkEnv: Setting default root path: {} => {}", target.module, target.path.u8string());
        target.resolved_config["cxx-root-include-path"] = target.path.u8string();

        // Forward the C++ build tools definitions to the build system
        for (const auto &kv : env["tools"].items())
        {
            auto name = kv.name();
            auto tool_path = vars.Resolve(kv.value().scalar());

            desc.tools.push_back(BuildTool{ulib::string{"cxx_"} + name + "_" + path, tool_path});

            meta["tools"][name] = tool_path;
            vars.SetVar(ulib::string{"cxx.tool."} + name, tool_path);
        }
    }

    bool CxxLangProvider::InitBuildTargetRules(NinjaBuildDesc &desc, const Target &target)
    {
        auto path = GetEscapedModulePath(target);

        auto &vars = *target.build_var_scope;

        auto &env_name = desc.state.at("re_cxx_env_for_" + path);
        auto &env = mEnvCache.at(env_name);

        /////////////////////////////////////////////////////////////////

        // for (auto& [k, v] : configuration)
        //	fmt::print(" *** '{}' -> {}={}\n", target.module, k, v);

        auto &config = target.resolved_config;
        if (!config["enabled"].get<bool>())
            return false;

        auto &meta = desc.meta["targets"][target.path.u8string()]["cxx"];

        TargetConfig definitions = config["cxx-compile-definitions"];
        TargetConfig definitions_pub = config["cxx-compile-definitions-public"];

        if (!definitions.is_map())
            definitions = TargetConfig{ulib::yaml::value_t::map};
        if (!definitions_pub.is_map())
            definitions_pub = TargetConfig{ulib::yaml::value_t::map};

        // Make the local definitions supersede all platform ones
        for (const auto &def : env["platform-definitions"].items())
            if (!definitions.search(def.name()))
                definitions[def.name()] = def.value();

        std::vector<const Target *> include_deps;
        PopulateTargetDependencySetNoResolve(&target, include_deps);

        /////////////////////////////////////////////////////////////////

        std::string flags_base = fmt::format("$target_custom_flags ", path);

        const auto &templates = env["templates"];

        std::vector<std::string> extra_flags;

        std::string cpp_std = config["cxx-standard"].scalar();

        if (cpp_std == "20" && env_name == "gcc") // HACK
            cpp_std = "2a";

        meta["standard"] = "c++" + cpp_std;

        extra_flags.push_back(ulib::format(templates["cxx-module-output"].scalar(),
                                           fmt::arg("directory", fmt::format("$re_target_object_directory_{}", path))));

        auto cxx_include_dir = templates["cxx-include-dir"].scalar();
        auto cxx_module_lookup_dir = templates["cxx-module-lookup-dir"].scalar();

        std::vector<std::string> deps_list;
        std::vector<std::string> extra_link_flags;

        auto cxx_lib_dir = templates["cxx-lib-dir"].scalar();

        std::unordered_set<std::string> include_dirs;

        std::vector<std::string> global_link_deps;

        for (auto &target : include_deps)
        {
            auto &config = target->resolved_config;

            if (!config.is_map())
            {
                // fmt::print(" Target '{}' does not have a resolved config.\n", target->module);
                continue;
            }

            // auto &dependency_defines = config["cxx-compile-definitions-public"];

            if (auto dependency_defines = config.search("cxx-compile-definitions-public"))
            {
                if (dependency_defines->is_map())
                    for (const auto &kv : dependency_defines->items())
                    {
                        auto name = kv.name();
                        auto value = kv.value().scalar();

                        if (!definitions_pub.search(name))
                            definitions_pub[name] = value;
                    }
            }

            AppendIncludeDirs(*target, config, include_dirs, vars);

            // TODO: Make this only work with modules enabled???
            extra_flags.push_back(ulib::format(cxx_module_lookup_dir,
                                               fmt::arg("directory", fmt::format("$builddir/{}", target->module))));

            // Link stuff

            auto res_path = GetEscapedModulePath(*target);
            bool has_any_eligible_sources = (desc.state["re_cxx_target_has_objects_" + res_path] == "1");

            if (target->type == TargetType::StaticLibrary && has_any_eligible_sources)
            {
                deps_list.push_back("\"$cxx_artifact_" + res_path + "\"");
            }

            auto dep_vars = LocalVarScope{&target->local_var_ctx, "dep-target", &target->GetBuildVarScope().first};

            dep_vars.SetVar("arch", vars.ResolveLocal("arch"));

            AppendLinkFlags(*target, config, cxx_lib_dir, extra_link_flags, deps_list, dep_vars);

            if (auto deps = config.search("cxx-global-link-deps"))
                if (deps->is_sequence())
                    for (const auto &dep : *deps)
                        global_link_deps.push_back(fmt::format("-l{}", vars.Resolve(dep.scalar())));
        }

        auto parse_build_flags = [&extra_flags, &extra_link_flags, &vars, &target](ulib::yaml extra) {
            constexpr auto kCompiler = "compiler";
            constexpr auto kLinker = "linker";
            constexpr auto kLinkerNoStatic = "linker.nostatic";

            if (extra.is_map())
            {
                if (auto flags = extra.search(kCompiler))
                {
                    if (flags->is_scalar())
                        extra_flags.push_back(vars.Resolve(flags->scalar()));
                    else
                        for (const auto &flag : *flags)
                            extra_flags.push_back(vars.Resolve(flag.scalar()));
                }

                if (auto flags = extra.search(kLinker))
                {
                    if (flags->is_scalar())
                        extra_link_flags.push_back(vars.Resolve(flags->scalar()));
                    else
                        for (const auto &flag : *flags)
                            extra_link_flags.push_back(vars.Resolve(flag.scalar()));
                }

                if (target.type != TargetType::StaticLibrary)
                {
                    if (auto flags = extra.search(kLinkerNoStatic))
                    {
                        if (flags->is_scalar())
                            extra_link_flags.push_back(vars.Resolve(flags->scalar()));
                        else
                            for (const auto &flag : *flags)
                                extra_link_flags.push_back(vars.Resolve(flag.scalar()));
                    }
                }
            }
        };

        ulib::yaml extra_build_flags{ulib::yaml::value_t::map};

        if (const auto extra = config.search("cxx-build-flags"))
            MergeYamlNode(extra_build_flags, *extra);

        if (const auto opts = config.search("cxx-build-options"))
        {
            // if (opts->is_null())
            //     extra_build_flags = ulib::yaml{ulib::yaml::value_t::null};

            if (opts->is_map())
            {
                for (auto &kv : opts->items())
                {
                    if (kv.value().is_null())
                        continue;

                    if (auto def = env["build-options"].search(kv.name()))
                    {
                        if (def->is_map())
                        {
                            if (auto value = def->search(kv.value().scalar()))
                            {
                                MergeYamlNode(extra_build_flags, *value);
                            }
                            else if (auto value = def->search("$value"))
                            {
                                // HACK: Format the value argument

                                auto cloned = *value;

                                for (auto &def_kv : cloned.items())
                                {
                                    if (def_kv.value().is_sequence())
                                    {
                                        for (auto &v : def_kv.value())
                                            v = fmt::format(std::string{v.scalar()},
                                                            fmt::arg("value", kv.value().scalar()));
                                    }
                                    else
                                    {
                                        def_kv.value() = fmt::format(std::string{def_kv.value().scalar()},
                                                                     fmt::arg("value", kv.value().scalar()));
                                    }
                                }

                                MergeYamlNode(extra_build_flags, cloned);
                            }
                            else if (auto default_option = def->search("default"))
                            {
                                MergeYamlNode(extra_build_flags, *default_option);
                            }
                            else
                            {
                                RE_THROW TargetConfigException(&target, "Unknown build option value '{}' = {}",
                                                               kv.name(), kv.value().scalar());
                            }
                        }
                    }
                    else
                    {
                        RE_THROW TargetConfigException(&target, "Unknown build option '{}'", kv.name());
                    }
                }
            }

            // if (opts->is_null())
            //     extra_build_flags = *opts;
        }

        //  ulib::list<ulib::string> list{extra_flags.begin(), extra_flags.end()};
        // fmt::print("ky\n");

        // fmt::print("extra flags: {}\n", ulib::join(extra_flags, ", "));
        // fmt::print("extra build flags: {}\n", extra_build_flags.dump());
        // fmt::print("config: {}\n", config.dump());

        parse_build_flags(extra_build_flags);

        // YAML::Emitter em;
        // em << extra_build_flags;
        // fmt::print("{}\n", em.c_str());

        for (auto &dir : include_dirs)
            extra_flags.push_back(ulib::format(cxx_include_dir, fmt::arg("directory", dir)));

        meta["include_dirs"] = include_dirs;

        for (const auto &kv : definitions_pub.items())
        {
            auto name = kv.name();
            auto value = kv.value().scalar();

            if (!definitions.search(name))
                definitions[name] = value;
        }

        /////////////////////////////////////////////////////////////////

        auto cxx_compile_definition = templates["cxx-compile-definition"].scalar();
        auto cxx_compile_definition_no_value = templates["cxx-compile-definition-no-value"].scalar();

        for (const auto &kv : definitions.items())
        {
            auto name = vars.Resolve(kv.name());

            if (kv.value().is_scalar())
            {
                auto value = vars.Resolve(kv.value().scalar());

                extra_flags.push_back(
                    ulib::format(cxx_compile_definition, fmt::arg("name", name), fmt::arg("value", value)));

                meta["definitions"].push_back(name + "=" + value);
            }
            else
            {
                extra_flags.push_back(
                    ulib::format(cxx_compile_definition_no_value, fmt::arg("name", vars.Resolve(name))));

                meta["definitions"].push_back(name);
            }
        }

        /////////////////////////////////////////////////////////////////

        for (auto &flag : extra_flags)
        {
            flags_base.append(flag);
            flags_base.append(" ");
        }

        /////////////////////////////////////////////////////////////////

        // Create build rules

        auto use_rspfiles = env["use-rspfiles"].get<bool>();

        BuildRule rule_cxx;

        rule_cxx.name = "cxx_compile_" + path;
        rule_cxx.tool = "cxx_compiler_" + path;

        rule_cxx.cmdline =
            fmt::format(vars.Resolve(templates["compiler-cmdline"].scalar()).c_str(), fmt::arg("flags", flags_base),
                        fmt::arg("input", "$in"), fmt::arg("output", "$out"));

        if (use_rspfiles)
        {
            rule_cxx.vars["rspfile_content"] = rule_cxx.cmdline;
            rule_cxx.vars["rspfile"] = "$out.rsp";
            rule_cxx.cmdline = "@$out.rsp";
        }

        rule_cxx.description = "Building C++ source $in";

        if (auto rule_vars = env.search("custom-rule-vars"))
            for (const auto &var : rule_vars->items())
                rule_cxx.vars[var.name()] = vars.Resolve(var.value().scalar());

        std::string extra_link_flags_str = "";

        for (auto &flag : extra_link_flags)
        {
            extra_link_flags_str.append(" ");
            extra_link_flags_str.append(flag);
        }

        std::string deps_input = "";

        for (auto &dep : deps_list)
        {
            deps_input.append(dep);
            deps_input.append(" ");
        }

        std::string global_deps_input = "";

        for (auto &dep : global_link_deps)
        {
            global_deps_input.append(dep);
            global_deps_input.append(" ");
        }

        BuildRule rule_link;

        rule_link.name = "cxx_link_" + path;
        rule_link.tool = "cxx_linker_" + path;

        rule_link.cmdline = fmt::format(
            vars.Resolve(templates["linker-cmdline"].scalar()).c_str(),
            fmt::arg("flags", "$target_custom_flags " + extra_link_flags_str), fmt::arg("link_deps", deps_input),
            fmt::arg("global_link_deps", global_deps_input), fmt::arg("input", "$in"), fmt::arg("output", "$out"));

        if (use_rspfiles)
        {
            rule_link.vars["rspfile_content"] = rule_link.cmdline;
            rule_link.vars["rspfile"] = "$out.rsp";
            rule_link.cmdline = "@$out.rsp";
        }

        rule_link.description = "Linking target $out";

        BuildRule rule_lib;

        rule_lib.name = "cxx_archive_" + path;
        rule_lib.tool = "cxx_archiver_" + path;

        rule_lib.cmdline = fmt::format(
            vars.Resolve(templates["archiver-cmdline"].scalar()).c_str(),
            fmt::arg("flags", "$target_custom_flags " + extra_link_flags_str), fmt::arg("link_deps", deps_input),
            fmt::arg("global_link_deps", global_deps_input), fmt::arg("input", "$in"), fmt::arg("output", "$out"));

        if (use_rspfiles)
        {
            rule_lib.vars["rspfile_content"] = rule_lib.cmdline;
            rule_lib.vars["rspfile"] = "$out.rsp";
            rule_lib.cmdline = "@$out.rsp";
        }

        rule_lib.description = "Archiving target $out";

        desc.rules.emplace_back(std::move(rule_cxx));
        desc.rules.emplace_back(std::move(rule_link));
        desc.rules.emplace_back(std::move(rule_lib));

        desc.vars["cxx_path_" + path] = target.path.u8string();
        desc.vars["cxx_config_path_" + path] = target.config_path.u8string();

        return true;
    }

    void CxxLangProvider::ProcessSourceFile(NinjaBuildDesc &desc, const Target &target, const SourceFile &file)
    {
        if (target.type == TargetType::Project)
            return;

        auto path = GetEscapedModulePath(target);
        auto &env = mEnvCache.at(desc.state.at("re_cxx_env_for_" + path));

        bool eligible = false;

        if (auto exts = env.search("supported-extensions"))
            if (exts->is_sequence())
                for (const auto &ext : *exts)
                    if (ext.scalar() == file.extension)
                        eligible = true;

        if (auto exts = env.search("cxx-supported-extensions"))
            if (exts->is_sequence())
                for (const auto &ext : *exts)
                    if (ext.scalar() == file.extension)
                        eligible = true;

        if (!eligible)
            return;

        auto &meta = desc.meta["targets"][target.path.u8string()]["cxx"];

        meta["sources"].push_back(file.path.generic_u8string());

        if (file.extension.front() == 'h') // C/C++ Header File: no need to build it
            return;

        auto local_path = fs::relative(file.path, target.path).generic_u8string();

        auto extension = env["default-extensions"]["object"].scalar();

        BuildTarget build_target;

        build_target.type = BuildTargetType::Object;

        build_target.pSourceTarget = &target;
        build_target.pSourceFile = &file;

        build_target.in = "$cxx_path_" + path + "/" + local_path;
        build_target.out = fmt::format("$builddir/{}/{}.{}", fmt::format("$re_target_object_directory_{}", path),
                                       local_path, extension);
        build_target.rule = "cxx_compile_" + path;

        if (file.extension == "c")
        {
            build_target.vars["target_custom_flags"].append(env["templates"]["compile-as-c"].scalar());

            std::string c_std = target.resolved_config["c-standard"].scalar();

            auto c_std_flag = ulib::format(env["templates"]["c-standard"].scalar(), fmt::arg("version", c_std));
            build_target.vars["target_custom_flags"].append(ulib::string{" "} + c_std_flag);
        }
        else
        {
            std::string cpp_std = target.resolved_config["cxx-standard"].scalar();

            auto cpp_std_flag = ulib::format(env["templates"]["cxx-standard"].scalar(), fmt::arg("version", cpp_std));
            build_target.vars["target_custom_flags"].append(ulib::string{" "} + cpp_std_flag);

            // build_target.vars["target_custom_flags"].append(env["templates"]["compile-as-cpp"].scalar());
        }

        // fmt::print(" [DBG] Target '{}' has object '{}'->'{}'\n", path, build_target.in, build_target.out);

        desc.targets.emplace_back(std::move(build_target));

        desc.state["re_cxx_target_has_objects_" + path] = "1";
    }

    void CxxLangProvider::CreateTargetArtifact(NinjaBuildDesc &desc, const Target &target)
    {
        auto path = GetEscapedModulePath(target);

        bool has_any_eligible_sources = (desc.state["re_cxx_target_has_objects_" + path] == "1");
        if (!has_any_eligible_sources)
            return;

        auto &env = mEnvCache.at(desc.state.at("re_cxx_env_for_" + path));

        BuildTarget link_target;

        link_target.type = BuildTargetType::Artifact;
        link_target.pSourceTarget = &target;
        link_target.out = "$builddir/" + fmt::format("$re_target_artifact_directory_{}", path) + "/" +
                          target.build_var_scope->ResolveLocal("build-artifact");
        link_target.rule = "cxx_link_" + path;

        switch (target.type)
        {
        case TargetType::StaticLibrary:
            link_target.rule = "cxx_archive_" + path;
            break;
        case TargetType::SharedLibrary:
            link_target.vars["target_custom_flags"].append(" ");
            link_target.vars["target_custom_flags"].append(env["templates"]["link-as-shared-library"].scalar());
            break;
        case TargetType::Project:
            link_target.rule = "phony";
            break;
        }

        for (auto &build_target : desc.targets)
            if (build_target.pSourceTarget == &target && build_target.pSourceFile)
            {
                link_target.in.append(build_target.out);
                link_target.in.append(" ");
            }

        std::vector<const Target *> link_deps;
        PopulateTargetDependencySetNoResolve(&target, link_deps);

        for (auto &dep : link_deps)
            if (dep != &target)
            {
                auto artifact = desc.vars["cxx_artifact_" + GetEscapedModulePath(*dep)];
                if (!artifact.empty())
                    link_target.deps.push_back(artifact);
            }

        link_target.deps.push_back("$cxx_config_path_" + path);

        BuildTarget alias_target;

        alias_target.type = BuildTargetType::Alias;
        alias_target.in = link_target.out;
        alias_target.out = target.module;
        alias_target.rule = "phony";

        desc.vars["cxx_artifact_" + path] = link_target.out;

        auto full_artifact_path = fs::path{"${artifact-dir}"} / "${build-artifact}";
        desc.artifacts[&target] = full_artifact_path;

        desc.targets.emplace_back(std::move(link_target));
        desc.targets.emplace_back(std::move(alias_target));
    }

    CxxBuildEnvData &CxxLangProvider::LoadEnvOrThrow(std::string_view name, const Target &invokee)
    {
        if (mEnvCache.find(name.data()) != mEnvCache.end())
            return mEnvCache[name.data()];

        try
        {
            // std::ifstream stream{ (mEnvSearchPath / name.data() / ".yml") };

            auto &data = (mEnvCache[name.data()] = ulib::yaml::parse(
                              futile::open(mEnvSearchPath.u8string() + "/" + name.data() + ".yml").read()));

            if (auto inherits = data.search("inherits"))
                for (const auto &v : *inherits)
                {
                    auto &other = LoadEnvOrThrow(v.scalar(), invokee);

                    for (const auto &pair : other.items())
                        if (!data.search(pair.name()))
                            data[pair.name()] = pair.value();
                }

            return data;
        }
        catch (const std::exception &e)
        {
            RE_THROW TargetBuildException(&invokee, "failed to load C++ environment {}: {}", name, e.what());
        }
    }
} // namespace re
