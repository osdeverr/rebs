#include "buildenv.h"
#include "re/buildenv.h"
#include "re/vars.h"

#include <fmt/color.h>
#include <fmt/format.h>

#include <re/debug.h>
#include <re/error.h>
#include <re/fs.h>
#include <re/process_util.h>
#include <re/target_cfg_utils.h>
#include <re/target_feature.h>

// #include <boost/algorithm/string.hpp>
#include <ulib/format.h>
#include <ulib/split.h>
#include <ulib/string.h>

#include <ulib/env.h>

#include <iostream>
#include <ulib/json.h>

#include <futile/futile.h>

namespace re
{
    void PopulateTargetChildSet(Target *pTarget, ulib::list<Target *> &to)
    {
        to.push_back(pTarget);

        for (auto &child : pTarget->children)
            PopulateTargetChildSet(child.get(), to);
    }

    void PopulateTargetDependencySet(Target *pTarget, ulib::list<Target *> &to, TargetDepResolver dep_resolver,
                                     bool throw_on_missing)
    {
        if (std::find(to.begin(), to.end(), pTarget) != to.end())
            return;

        // std::cout << pTarget->resolved_config << std::endl;

        if (pTarget->resolved_config.is_map() && !pTarget->resolved_config["enabled"].get<bool>())
        {
            RE_TRACE(" PopulateTargetDependencySet: Skipping '{}' because it's not enabled\n", pTarget->module);
            return;
        }

        // for (auto &[name, dep] : pTarget->used_mapping)
        // {
        //     RE_TRACE(" PopulateTargetDependencySet: Attempting to resolve uses-mapping '{}' <- '{}'\n",
        //     pTarget->module,
        //              dep->ToString());

        //     fmt::print("dep: {}\n", dep->ToString());

        //     if (dep->resolved.empty() && !dep_resolver(*pTarget, *dep, dep->resolved))
        //     {
        //         fmt::print("failed dep: {}\n", dep->ToString());
        //         RE_TRACE("     failed\n");

        //         if (throw_on_missing)
        //             RE_THROW TargetDependencyException(pTarget, "unresolved uses-map dependency {}", dep->name);
        //     }
        // }

        for (auto &dep : pTarget->dependencies)
        {
            RE_TRACE(" PopulateTargetDependencySet: Attempting to resolve '{}' <- '{}'\n", pTarget->module,
                     dep.ToString());

            if (dep.resolved.empty() && !dep_resolver(*pTarget, dep, dep.resolved))
            {
                RE_TRACE("     failed\n");

                if (throw_on_missing)
                    RE_THROW TargetDependencyException(pTarget, "unresolved dependency {}", dep.ToString());
            }
            else
            {
                for (auto &t : dep.resolved)
                {
                    PopulateTargetDependencySet(t, to, dep_resolver, throw_on_missing);

                    ulib::list<Target *> kids;
                    PopulateTargetChildSet(t, kids);

                    for (auto &needed : kids)
                    {
                        // fmt::print(" * Target '{}' depends on '{}'\n", pTarget->module, needed->module);
                        needed->dependents.insert(pTarget);
                    }

                    /*
                                        ulib::list<Target *> dependents_needed;
                                        PopulateTargetDependencySet(t, dependents_needed, dep_resolver,
                       throw_on_missing);

                                        for (auto &needed : dependents_needed)
                                            needed->dependents.insert(pTarget);
                    */
                }

                RE_TRACE("     done\n");
            }
        }

        for (auto &child : pTarget->children)
            PopulateTargetDependencySet(child.get(), to, dep_resolver, throw_on_missing);

        to.push_back(pTarget);
    }

    void PopulateTargetDependencySetNoResolve(const Target *pTarget, std::vector<const Target *> &to)
    {
        if (std::find(to.begin(), to.end(), pTarget) != to.end())
            return;

        if (pTarget->resolved_config.is_map() && !pTarget->resolved_config["enabled"].get<bool>())
        {
            RE_TRACE(" PopulateTargetDependencySetNoResolve: Skipping '{}' because it's not enabled\n",
                     pTarget->module);
            return;
        }

        to.push_back(pTarget);

        for (auto &dep : pTarget->dependencies)
        {
            RE_TRACE(" PopulateTargetDependencySetNoResolve - {} <- {} @ {}\n", pTarget->module, dep.ToString(),
                     (const void *)&dep);

            if (dep.resolved.empty())
                RE_THROW TargetDependencyException(pTarget, "unresolved dependency '{}'", dep.ToString());

            for (auto &t : dep.resolved)
                PopulateTargetDependencySetNoResolve(t, to);
        }

        for (auto &child : pTarget->children)
            PopulateTargetDependencySetNoResolve(child.get(), to);
    }

    BuildEnv::BuildEnv(LocalVarScope &scope, IUserOutput *pOut) : mVars{&scope, "build"}, mOut{pOut}
    {
        mVars.SetVar("platform", "${env:RE_PLATFORM | $re:platform-string}");
        mVars.SetVar("platform-closest", "${build:platform}");
        mVars.SetVar("arch", "${build:platform}");
    }

    std::unique_ptr<Target> BuildEnv::LoadFreeTarget(const fs::path &path, const Target *ancestor,
                                                     const TargetDependency *dep_source)
    {
        // {
        //     const Target *p = ancestor;
        //     if (p)
        //     {
        //         fmt::print("\nancestor:\n");
        //         fmt::print("p->name: {}\n", p->name);
        //         fmt::print("p->path: {}\n", p->path.generic_string());
        //         fmt::print("p->module: {}\n", p->module);
        //         fmt::print("p->type: {}\n", TargetTypeToString(p->type));
        //         fmt::print("p: 0x{:X}\n", uint64_t(p));

        //         if (p->parent)
        //         {
        //             fmt::print("p->parent->name: {}\n", p->parent->name);
        //             fmt::print("p->parent->path: {}\n", p->parent->path.generic_string());
        //             fmt::print("p->parent->module: {}\n", p->parent->module);
        //             fmt::print("p->parent->type: {}\n", TargetTypeToString(p->parent->type));
        //             fmt::print("p->parent: 0x{:X}\n", uint64_t(p->parent));
        //         }

        //         if (p->root)
        //         {
        //             fmt::print("p->root->name: {}\n", p->root->name);
        //             fmt::print("p->root->path: {}\n", p->root->path.generic_string());
        //             fmt::print("p->root->module: {}\n", p->root->module);
        //             fmt::print("p->root->type: {}\n", TargetTypeToString(p->root->type));
        //             fmt::print("p->root: 0x{:X}\n", uint64_t(p->root));
        //         }

        //         fmt::print("\n");
        //     }
        // }

        std::unique_ptr<Target> target = nullptr;

        for (auto &middleware : mTargetLoadMiddlewares)
        {
            if (middleware->SupportsTargetLoadPath(path))
            {
                // fmt::print("middleware->SupportsTargetLoadPath({})\n", path.generic_u8string());
                target = middleware->LoadTargetWithMiddleware(path, ancestor, dep_source);
                break;
            }
            // else
            //	fmt::print("!middleware->SupportsTargetLoadPath({})\n", path.generic_u8string());
        }

        // Default behavior in absence of any suitable middleware is to simply construct the target as normal.
        if (!target)
        {
            if (!fs::exists(path / "re.yml"))
                RE_THROW TargetLoadException(nullptr, "The directory '{}' does not contain a valid Re target.",
                                             path.u8string());

            target = std::make_unique<Target>(path, mTheCoreProjectTarget.get());
        }

        if (mRootTargets.size() > 0)
            target->parent = target->root = mRootTargets.front().get();
        else
            target->parent = mTheCoreProjectTarget.get();

        target->root = target.get();

        return target;
    }

    Target &BuildEnv::LoadTarget(const fs::path &path)
    {
        auto target = LoadFreeTarget(path);
        target->root_path = target->path;

        target->LoadDependencies();
        target->LoadMiscConfig();
        target->LoadSourceTree();

        // mTargetMap.clear();
        PopulateTargetMap(target.get());

        target->config["load-context"] = "standalone";

        auto &moved = mRootTargets.emplace_back(std::move(target));
        return *moved.get();
    }

    void BuildEnv::RegisterLocalTarget(Target *pTarget)
    {
        PopulateTargetMap(pTarget);
    }

    bool BuildEnv::CanLoadTargetFrom(const fs::path &path)
    {
        // Check middlewares first - they may load non-Re targets just fine
        for (auto &middleware : mTargetLoadMiddlewares)
            if (middleware->SupportsTargetLoadPath(path))
                return true;

        // If no middlewares picked up the path along the way, check if there's a re.yml there.
        return (fs::exists(path / "re.yml"));
    }

    Target &BuildEnv::LoadCoreProjectTarget(const fs::path &path)
    {
        mTheCoreProjectTarget = LoadFreeTarget(path);
        return *mTheCoreProjectTarget;
    }

    Target *BuildEnv::GetCoreTarget()
    {
        return mTheCoreProjectTarget.get();
    }

    ulib::list<Target *> BuildEnv::GetSingleTargetDepSet(Target *pTarget)
    {
        ulib::list<Target *> result;
        AppendDepsAndSelf(pTarget, result);
        return result;
    }

    ulib::list<Target *> BuildEnv::GetSingleTargetLocalDepSet(Target *pTarget)
    {
        ulib::list<Target *> result;
        AppendDepsAndSelf(pTarget, result, false, false);
        return result;
    }

    ulib::list<Target *> BuildEnv::GetTargetsInDependencyOrder()
    {
        ulib::list<Target *> result;

        for (auto &target : mRootTargets)
            AppendDepsAndSelf(target.get(), result);

        return result;
    }

    void BuildEnv::AddLangProvider(ulib::string_view name, ILangProvider *provider)
    {
        mLangProviders[name.data()] = provider;
    }

    ILangProvider *BuildEnv::GetLangProvider(ulib::string_view name)
    {
        return mLangProviders[name.data()];
    }

    ILangProvider *BuildEnv::InitializeTargetLinkEnv(Target *target, NinjaBuildDesc &desc)
    {
        auto link_cfg = target->GetCfgEntry<TargetConfig>("link-with", re::CfgEntryKind::Recursive)
                            .value_or(ulib::yaml{ulib::yaml::value_t::null});
        std::optional<std::string> link_language;

        if (link_cfg.is_map())
        {
            if (auto value = link_cfg.search(TargetTypeToString(target->type)))
            {
                if (!value->is_null())
                    link_language = value->scalar();
            }
            else if (auto value = link_cfg.search("default"))
            {
                if (!value->is_null())
                    link_language = value->scalar();
            }
        }
        else if (!link_cfg.is_null())
        {
            link_language = link_cfg.scalar();
        }

        ILangProvider *link_provider = link_language ? GetLangProvider(*link_language) : nullptr;

        if (link_language && !link_provider)
            RE_THROW TargetLoadException(target, "unknown link-with language {}", *link_language);

        if (link_provider && desc.state["link_initialized_" + target->module] != "1")
        {
            link_provider->InitLinkTargetEnv(desc, *target);
            desc.state["link_initialized_" + target->module] = "1";
        }

        for (auto &[name, object] : target->features)
        {
            if (!object)
            {
                auto it = mTargetFeatures.find(name);

                if (it == mTargetFeatures.end())
                    RE_THROW TargetLoadException(target, "unknown target feature {}", name);
                else
                    object = it->second;
            }
        }

        return link_provider;
    }

    void BuildEnv::InitializeTargetLinkEnvWithDeps(Target *target, NinjaBuildDesc &desc)
    {
        ulib::list<Target *> deps;
        AppendDepsAndSelf(target, deps, false, false);

        for (auto &dep : deps)
            InitializeTargetLinkEnv(dep, desc);
    }

    void BuildEnv::PopulateBuildDesc(Target *target, NinjaBuildDesc &desc)
    {
        auto langs = target->GetCfgEntry<TargetConfig>("langs", CfgEntryKind::Recursive)
                         .value_or(TargetConfig{ulib::yaml::value_t::sequence});

        ILangProvider *link_provider = InitializeTargetLinkEnv(target, desc);

        if (target->resolved_config.is_map() && !target->resolved_config["enabled"].get<bool>())
        {
            RE_TRACE(" PopulateBuildDesc: Skipping '{}' because it's not enabled\n", target->module);
            return;
        }

        for (const auto &lang : langs)
        {
            auto lang_id = lang.scalar();

            auto provider = GetLangProvider(lang_id);
            if (!provider)
                RE_THROW TargetLoadException(target, "unknown language {}", lang_id);

            if (provider->InitBuildTargetRules(desc, *target))
            {
                for (auto &source : target->sources)
                    provider->ProcessSourceFile(desc, *target, source);
            }
        }

        if (link_provider)
            link_provider->CreateTargetArtifact(desc, *target);
    }

    void BuildEnv::PopulateBuildDescWithDeps(Target *target, NinjaBuildDesc &desc)
    {
        for (auto &dep : GetSingleTargetDepSet(target))
            PopulateBuildDesc(dep, desc);
    }

    void BuildEnv::PopulateFullBuildDesc(NinjaBuildDesc &desc)
    {
        // auto re_arch = std::getenv("RE_ARCH");
        // auto re_platform = std::getenv("RE_PLATFORM");

        /*
        desc.vars["re_build_platform"] = mVars.Substitute("${platform}");
        desc.vars["re_build_platform_closest"] = mVars.Substitute("${platform-closest}");
        desc.vars["re_build_arch"] = mVars.Substitute("${arch}");
        */

        for (auto &[name, provider] : mLangProviders)
            provider->InitInBuildDesc(desc);
    }

    void BuildEnv::PerformCopyToDependentsImpl(const Target &target, const Target *dependent,
                                               const NinjaBuildDesc *desc, const fs::path &from, ulib::string_view to)
    {
        auto path = GetEscapedModulePath(*dependent);

        RE_TRACE("    for dependent '{}':\n", dependent->module);

        if (desc->HasArtifactsFor(path))
        {
            auto to_dep = desc->out_dir / desc->GetArtifactDirectory(path);

            auto context = dependent->local_var_ctx;
            context["self"] = &*target.build_var_scope;

            auto scope = LocalVarScope{&context, "_", &*dependent->build_var_scope, "target"};

            auto to_resolved = scope.Resolve(to);

            auto &from_path = from;
            fs::path to_path = to_dep / to_resolved;

            if (to_resolved.back() == '/')
                fs::create_directories(to_path);
            else
                fs::create_directories(to_path.parent_path());

            RE_TRACE("        copying from '{}' to '{}'\n", from_path.u8string(), to_path.u8string());

            if (fs::exists(to_dep))
            {
                fs::copy(from_path, to_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);

                RE_TRACE("            done\n");
            }
            else
            {
                RE_TRACE("            !!! no to_dep dir\n");
            }
        }
        else
        {
            RE_TRACE("        no artifacts\n");
        }

        for (auto &inner_dep : dependent->dependents)
            PerformCopyToDependentsImpl(target, inner_dep, desc, from, to);
    }

    void BuildEnv::RunTargetAction(const NinjaBuildDesc *desc, const Target &target, ulib::string_view type,
                                   const TargetConfig &data)
    {
        if (type == "copy")
        {
            auto from = target.build_var_scope->Resolve(data["from"].scalar());
            auto to = target.build_var_scope->Resolve(data["to"].scalar());

            auto from_path = fs::path{from};
            auto to_path = fs::path{to};

            if (!from_path.is_absolute())
                from_path = target.path / from_path;

            if (!to_path.is_absolute())
                to_path = desc->out_dir / desc->GetArtifactDirectory(GetEscapedModulePath(target)) / to_path;

            fs::copy(from_path, to_path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        }
        else if (type == "copy-to-deps")
        {
            auto from = target.build_var_scope->Resolve(data["from"].scalar());
            auto to = data["to"].scalar();

            auto from_path = fs::path{from};

            if (!from_path.is_absolute())
                from_path = target.path / from_path;

            if (fs::exists(from_path))
            {
                for (auto &dependent : target.dependents)
                {
                    PerformCopyToDependentsImpl(target, dependent, desc, from_path, to);
                }
            }
        }
        else if (type == "run")
        {
            if (data.is_map())
            {
                auto command = data["command"].scalar();

                ulib::list<ulib::string> args;

                args.push_back(target.build_var_scope->Resolve(command));

                if (auto args_field = data.search("args"))
                    if (args_field->is_sequence())
                        for (auto &arg : *args_field)
                            args.push_back(target.build_var_scope->Resolve(arg.scalar()));

                RunProcessOrThrow(args.front(), {}, args, true, true, target.path.u8string());
            }
            else
            {
                auto command = target.build_var_scope->Resolve(data.scalar());

                ulib::list<ulib::string> args;
                std::istringstream iss{command};
                std::string temp;

                while (iss >> std::quoted(temp, '"', '^'))
                    args.push_back(temp);

                RunProcessOrThrow(args.front(), {}, args, true, true, target.path.u8string());
            }
        }
        else if (type == "shell-run")
        {
            auto command = target.build_var_scope->Resolve(data["command"].scalar());

            std::system(command.data());
        }
        else if (type == "command")
        {
            auto command = target.build_var_scope->Resolve(data.scalar());
            std::system(command.data());
        }
        else if (type == "install")
        {
            auto style = fmt::emphasis::bold | fg(fmt::color::pale_turquoise);

            fs::path artifact_dir = desc->out_dir / desc->GetArtifactDirectory(GetEscapedModulePath(target));
            fs::path from = desc->out_dir / desc->GetArtifactDirectory(GetEscapedModulePath(target));

            if (data.search("from"))
                from /= target.build_var_scope->Resolve(data["from"].scalar());

            auto do_install = [this, &artifact_dir, &from, &target, desc, style](const std::string &path,
                                                                                 bool create_dir) {
                auto to = fs::path{target.build_var_scope->Resolve(path)};

                if (!to.is_absolute())
                    to = artifact_dir / to;

                mOut->Info(style, "     - {}\n", to.u8string());

                if (!fs::exists(to) && create_dir)
                    fs::create_directories(to);

                fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            };

            mOut->Info(style, " * Installed {} to:\n", target.module);

            if (auto to_v = data.search("to"))
            {
                if (to_v->is_sequence())
                    for (const auto &v : *to_v)
                        do_install(v.scalar(), true);
                else
                    do_install(to_v->scalar(), true);
            }
            else if (auto to_v = data.search("to-file"))
            {
                if (to_v->is_sequence())
                    for (const auto &v : *to_v)
                        do_install(v.scalar(), false);
                else
                    do_install(to_v->scalar(), false);
            }

            mOut->Info(style, "\n");
        }
    }

    void BuildEnv::RunPostBuildActions(Target *target, const NinjaBuildDesc &desc)
    {
        RunActionsCategorized(target, &desc, "post-build");
    }

    void BuildEnv::RunInstallActions(Target *target, const NinjaBuildDesc &desc)
    {
        auto from = desc.out_dir / desc.GetArtifactDirectory(target->module);
        InstallPathToTarget(target, from);

        RunActionsCategorized(target, &desc, "post-install");
    }

    void BuildEnv::InstallPathToTarget(const Target *pTarget, const fs::path &from)
    {
        if (auto path = pTarget->GetCfgEntry<TargetConfig>("install", CfgEntryKind::Recursive))
        {
            auto path_str = path->scalar();

            mOut->Info({}, "Installing {} - {} => {}\n", pTarget->module, from.u8string(), path_str);

            if (fs::exists(from))
            {
                fs::copy(from, path_str, fs::copy_options::recursive | fs::copy_options::skip_existing);
            }
        }
    }

    void BuildEnv::AddDepResolver(ulib::string_view name, IDepResolver *resolver)
    {
        mDepResolvers[name.data()] = resolver;
    }

    void BuildEnv::AddTargetFeature(ulib::string_view name, ITargetFeature *feature)
    {
        mTargetFeatures[name.data()] = feature;
    }

    void BuildEnv::AddTargetFeature(ITargetFeature *feature)
    {
        mTargetFeatures[feature->GetName()] = feature;
    }

    void BuildEnv::AddTargetLoadMiddleware(ITargetLoadMiddleware *middleware)
    {
        mTargetLoadMiddlewares.push_back(middleware);
    }

    Target *FindDeepNeighborTarget(const Target *target, ulib::string_view name)
    {
        while (name.size() && name.front() == '.')
            name.remove_prefix(1);

        if (!target || !target->parent)
        {
            return nullptr;
        }

        for (auto &child : target->parent->children)
        {
            // fmt::print("{} -> {}\n", name, child->name);

            ulib::string_view childName = child->name;
            while (childName.starts_with('.'))
                childName.remove_prefix(1);

            if (childName == name)
            {
                return child.get();
            }
        }

        return FindDeepNeighborTarget(target->parent, name);
    }

    Target *GetDeepSiblingDep(const Target *target, ulib::string_view name)
    {
        while (name.starts_with("."))
            name.remove_prefix(1);

        ulib::list<ulib::string> components = name.split(".");
        if (components.empty())
            return nullptr;

        auto result = FindDeepNeighborTarget(target, components[0]);
        for (size_t i = 1; i < components.size(); i++)
        {
            result = result->FindChild(components[i]);
            if (!result)
                return nullptr;
        }

        return result;
    }

    bool BuildEnv::ResolveTargetDependencyImpl(const Target &target, const TargetDependency &dep,
                                               ulib::list<Target *> &out, bool use_external)
    {
        out.clear();

        if (dep.ns.empty())
        {
            auto result = GetDeepSiblingDep(&target, dep.name);
            // fmt::print("ResolveTargetDependencyImpl: target: {}\n", target.name);

            // Arch coercion - this is SOMETIMES very useful
            if (result)
            {
                if (dep.extra_config_hash)
                {
                    auto ecfg_name = ulib::format("ecfg-local.{}.{}", dep.name, dep.extra_config_hash);
                    auto ecfg_existing = GetTargetOrNull(ecfg_name);

                    if (!ecfg_existing)
                    {
                        auto ecfg_target = LoadFreeTarget(result->path);

                        ecfg_target->root_path = ecfg_target->path;
                        ecfg_target->module = ecfg_name;
                        ecfg_target->parent = result->parent;

                        ecfg_target->LoadDependencies();
                        ecfg_target->LoadMiscConfig();
                        ecfg_target->LoadSourceTree();

                        // mTargetMap.clear();
                        PopulateTargetMap(ecfg_target.get());
                        ecfg_existing = ecfg_target.get();

                        mRootTargets.push_back(std::move(ecfg_target));
                    }

                    result = ecfg_existing;
                }

                // fmt::print("target.build_var_scope = {}, result->build_var_scope = {}\n",
                // target.build_var_scope.has_value(), result->build_var_scope.has_value());

                if (target.build_var_scope && result->build_var_scope)
                {
                    auto target_arch = target.build_var_scope->ResolveLocal("arch");
                    auto dep_arch = result->build_var_scope->ResolveLocal("arch");

                    if (target_arch != dep_arch)
                    {
                        if (use_external || true)
                        {
                            fmt::print(" *** Performing arch coercion: {}:{} <- {}:{}\n", target.module, target_arch,
                                       result->module, dep_arch);

                            if (auto resolver = mDepResolvers["arch-coerced"])
                                result = resolver->ResolveCoercedTargetDependency(target, *result);
                            else
                                RE_THROW TargetLoadException(&target,
                                                             "dependency '{}': architecture mismatch (target:{} != "
                                                             "dep:{}) without a multi-arch dep resolver",
                                                             dep.ToString(), target_arch, dep_arch);
                        }
                        else
                            return false;
                    }
                }

                out.emplace_back(result);
                return true;
            }

            return false;
        }

        if (use_external)
        {
            auto handle_single_target_filter_deps = [&out, &dep, &target](Target *result) {
                for (auto &filter : dep.filters)
                {
                    if (filter.front() == '/')
                        continue;

                    ulib::list<ulib::string> parts = filter.split(".");
                    auto temp = result;

                    for (auto &part : parts)
                    {
                        if (!part.empty())
                            temp = temp->FindChild(part);

                        if (!temp)
                            RE_THROW TargetDependencyException(
                                &target,
                                "unresolved partial dependency filter '{}' for '{}' <- '{}' (failed at part '{}')",
                                filter, result->module, dep.ToString(), part);
                    }

                    if (!temp)
                        RE_THROW TargetDependencyException(&target,
                                                           "unresolved partial dependency filter '{}' for '{}' <- '{}'",
                                                           filter, result->module, dep.ToString());

                    out.emplace_back(temp);
                }
            };

            // Special case
            if (dep.ns == "uses")
            {
                auto used = target.GetUsedDependency(dep.name);

                if (!used)
                    RE_THROW TargetDependencyException(&target, "uses-dependency '{}' not found", dep.ToString());

                ulib::list<Target *> result;

                if (!ResolveTargetDependencyImpl(target, *used, result, use_external))
                    RE_THROW TargetDependencyException(&target, "unresolved uses-dependency '{}' <- '{}'",
                                                       dep.ToString(), used->ToString());

                auto resolver = mDepResolvers[used->ns];

                if (!dep.filters.empty() && !(resolver && resolver->DoesCustomHandleFilters()))
                {
                    if (!used->filters.empty())
                    {
                        auto it = result.begin();

                        for (auto &filter : dep.filters)
                        {
                            if (filter.front() == '/')
                                continue;

                            if (std::find(used->filters.begin(), used->filters.end(), filter) == used->filters.end())
                                RE_THROW TargetDependencyException(&target,
                                                                   "invalid filter in uses-dependency '{}' <- '{}': "
                                                                   "'{}' is not part of original filters",
                                                                   dep.ToString(), used->ToString());
                        }

                        for (auto &filter : used->filters)
                        {
                            if (std::find(dep.filters.begin(), dep.filters.end(), filter) != dep.filters.end())
                                out.emplace_back(*it);

                            it++;
                        }

                        if (out.empty())
                            RE_THROW TargetDependencyException(
                                &target, "error in uses-dependency '{}' <- '{}': everything got filtered out!",
                                dep.ToString(), used->ToString());
                    }
                    else
                    {
                        if (result.size() == 1)
                            handle_single_target_filter_deps(result.front());
                        else
                            RE_THROW TargetDependencyException(&target,
                                                               "error in uses-dependency '{}' <- '{}': bad bug!",
                                                               dep.ToString(), used->ToString());
                    }
                    // RE_THROW TargetDependencyException(&target, "error resolving uses-dependency '{}' <- '{}':
                    // filters are not yet implemented", dep.ToString(), used->ToString());
                }
                else
                {
                    out = std::move(result);
                }

                return out.size() > 0;
            }

            if (auto resolver = mDepResolvers[dep.ns])
            {
                auto result = resolver->ResolveTargetDependency(target, dep, mDepsVersionCache);
                result->config["load-context"] = "dep";
                result->config["root-dir"] = result->path.generic_u8string();
                result->config["is-external-dep"] = "true";

                if (result->resolved_config.is_map())
                    result->resolved_config["is-external-dep"] = "true";

                auto [scope, context] = target.GetBuildVarScope();

                // if (scope.ResolveLocal("inherit-caller-in-deps") == "true")
                {
                    // EVIL HACK
                    // if (target.root && !target.root->name.empty())
                    // {
                    //     ulib::string rootParentName = "[nullptr]";
                    //     if (target.root->parent)
                    //         rootParentName = target.root->parent->name;

                    //     fmt::print("EVIL HACK PAUSE. name: {}, root->name: {}, root->parent.name: {}\n", target.name,
                    //                target.root->name, rootParentName);
                    //     // std::system("pause");
                    // }

                    // result->root = target.root;
                    // result->parent = result->root;
                }

                if (!result->resolved_config.is_map())
                {
                    auto re_arch = scope.ResolveLocal("arch");
                    auto re_platform = scope.ResolveLocal("platform");
                    auto re_config = scope.ResolveLocal("configuration");

                    result->resolved_config =
                        GetResolvedTargetCfg(*result, {{"arch", re_arch},
                                                       {"platform", re_platform},
                                                       {"config", re_config},
                                                       {"runtime", scope.ResolveLocal("runtime")}});

                    result->LoadConditionalDependencies();
                }

                if (resolver->DoesCustomHandleFilters() || dep.filters.empty() || dep.filters[0].front() == '/')
                {
                    out.emplace_back(result);
                }
                else
                {
                    handle_single_target_filter_deps(result);
                }

                return out.size() > 0;
            }
            else
                RE_THROW TargetLoadException(&target, "dependency '{}': unknown target namespace '{}'", dep.ToString(),
                                             dep.ns);
        }
        else
        {
            return false;
        }
    }

    void BuildEnv::PopulateTargetMap(Target *pTarget)
    {
        RE_TRACE(" [DBG] Adding to target map: '{}'\n", pTarget->module);

        if (mTargetMap[pTarget->module] != nullptr)
            RE_THROW TargetLoadException(pTarget, "target defined more than once");

        mTargetMap[pTarget->module] = pTarget;

        for (auto &child : pTarget->children)
            PopulateTargetMap(child.get());
    }

    void BuildEnv::AppendDepsAndSelf(Target *pTarget, ulib::list<Target *> &to, bool throw_on_missing,
                                     bool use_external)
    {
        PopulateTargetDependencySet(
            pTarget, to,
            [this, &to, pTarget, throw_on_missing, use_external](const Target &target, const TargetDependency &dep,
                                                                 ulib::list<Target *> &out) {
                return ResolveTargetDependencyImpl(target, dep, out, use_external);
            },
            throw_on_missing);
    }

    void BuildEnv::RunActionList(const NinjaBuildDesc *desc, Target *target, const TargetConfig &list,
                                 ulib::string_view run_type, ulib::string_view default_run_type)
    {
        auto old_path = ulib::getpath();

        if (auto path_cfg = target->resolved_config.search("env-path"))
            if (path_cfg->is_sequence())
                for (const auto &path : *path_cfg)
                {
                    try
                    {
                        auto final_path = target->build_var_scope->Resolve(path.scalar());
                        ulib::add_path(final_path);
                    }
                    catch (re::VarSubstitutionException &ex)
                    {
                        if (!std::filesystem::exists("out"))
                            std::filesystem::create_directory("out");

                        futile::open("out/warnings.log", "a")
                            .write(ulib::format("[RunActionList] [{}]: Can't resolve: {}. Full Exception: {}\n",
                                                target->name, ulib::str(path.scalar()).c_str(), ex.what()));

                        // this->mOut->Trace(fmt::fg(fmt::color::yellow),
                        //                  "[RunActionList] [{}]: Can't resolve: {}\n", target->name,
                        //                  ulib::str(path.scalar()).c_str());
                    }
                }

        if (list.is_sequence())
        {
            for (const auto &v : list)
            {
                for (const auto &kv : v.items())
                {
                    auto type = kv.name();
                    auto &data = kv.value();

                    std::string run = default_run_type;
                    RE_TRACE("{} -> action {}\n", target->module, type);

                    bool should_run = (run_type == default_run_type);

                    if (data.is_map())
                    {
                        if (auto run_val = data.search("on"))
                        {
                            should_run = false;

                            if (run_val->is_scalar())
                                should_run = (run_type == run_val->scalar());
                            else
                                for (const auto &v : *run_val)
                                    if (run_type == v.scalar())
                                    {
                                        should_run = true;
                                        break;
                                    }
                        }
                    }

                    if (should_run)
                        RunTargetAction(desc, *target, type, data);
                }
            }
        }

        ulib::setpath(old_path);
    }

    void BuildEnv::RunActionsCategorized(Target *target, const NinjaBuildDesc *desc, ulib::string_view run_type)
    {
        auto &cfg = (target->resolved_config.is_map() ? target->resolved_config : target->config);
        if (auto actions = cfg.search("actions"))
        {
            if (actions->is_map())
            {
                for (const auto &kv : actions->items())
                {
                    auto type = kv.name();
                    auto &data = kv.value();

                    RunActionList(desc, target, data, run_type, type);
                }
            }
            else
            {
                RunActionList(desc, target, *actions, run_type, "post-build");
            }
        }
    }

    void BuildEnv::RunAutomaticStructuredTasks(Target *target, const NinjaBuildDesc *desc, ulib::string_view stage)
    {
        if (target->parent)
            RunAutomaticStructuredTasks(target->parent, desc, stage);

        auto &cfg = (target->resolved_config.is_map() ? target->resolved_config : target->config);
        if (auto tasks = cfg.search("tasks"))
        {
            for (auto &kv : tasks->items())
            {
                auto name = kv.name();
                auto &task = kv.value();

                if (task.is_map())
                {
                    auto run_field = task.search("run");
                    if (run_field && run_field->scalar() == "always")
                    {
                        RunStructuredTaskData(target, desc, task, name, stage);
                    }
                }
            }
        }
    }

    void BuildEnv::RunStructuredTask(Target *target, const NinjaBuildDesc *desc, ulib::string_view name,
                                     ulib::string_view stage)
    {
        auto &cfg = (target->resolved_config.is_map() ? target->resolved_config : target->config);
        if (auto tasks = cfg.search("tasks"))
        {
            if (auto task = tasks->search(name.data()))
            {
                RunStructuredTaskData(target, desc, *task, name, stage);
            }
        }
    }

    void BuildEnv::RunStructuredTaskData(Target *target, const NinjaBuildDesc *desc, const TargetConfig &task,
                                         ulib::string_view name, ulib::string_view stage)
    {
        // TODO: this is problematic and needs to be improved somehow
        if (auto deps = task.search("deps"))
        {
            for (auto dep_task : *deps)
            {
                if (desc)
                {
                    for (auto &dep : GetSingleTargetDepSet(desc->pBuildTarget))
                        RunStructuredTask(dep, desc, dep_task.scalar(), stage);
                }
                else
                {
                    RunStructuredTask(target, desc, dep_task.scalar(), stage);
                }
            }
        }

        if (auto stage_actions = task.search(stage.data()))
        {
            auto completion_key = ulib::format("{} / {} [{}]", target->module, name, stage);

            if (mCompletedActions.find(completion_key) != mCompletedActions.end())
                return;

            const auto kStyle = fg(fmt::color::blue_violet) | fmt::emphasis::bold;

            auto silent_field = task.search("silent");
            if (!silent_field || silent_field->get<bool>() != true)
            {
                mOut->Info(kStyle, " - Running task ");
                mOut->Info(kStyle | fmt::emphasis::underline, "{}\n\n", completion_key);
            }

            RunActionList(desc, target, *stage_actions, stage, stage.data());

            mCompletedActions.insert(completion_key);
        }
    }

    IDepResolver *BuildEnv::GetDepResolver(ulib::string_view name)
    {
        return mDepResolvers[name];
    }

    void BuildEnv::DebugShowVisualBuildInfo(const Target *pTarget, int depth)
    {
        const auto kStyleRoot = fg(fmt::color::red) | bg(fmt::color::yellow);
        const auto kStyleTargetName = fg(fmt::color::sky_blue);
        const auto kStyleTargetType = fg(fmt::color::dim_gray);
        const auto kStyleChildPrefix = fg(fmt::color::green_yellow);
        const auto kStyleCategoryTitle = fg(fmt::color::dark_gray);
        const auto kStyleDepString = fg(fmt::color::rebecca_purple);
        const auto kStyleDepRes = fg(fmt::color::yellow);

        if (!pTarget)
        {
            mOut->Info({}, "\n");

            for (auto &target : mRootTargets)
            {
                mOut->Info(kStyleChildPrefix, "* ");

                for (auto i = 0; i < depth; i++)
                    mOut->Info({}, "  ");

                DebugShowVisualBuildInfo(target.get(), depth);
            }

            if (mVars.GetVar("target-map").value_or("false") == "true")
            {
                mOut->Info({}, "\n\n");

                mOut->Info(kStyleCategoryTitle, "Target Map:\n");

                for (auto &[_, target] : mTargetMap)
                {
                    DebugShowVisualBuildInfo(target, depth);
                    mOut->Info({}, "\n");
                }
            }

            mOut->Info({}, "\n");
        }
        else
        {
            mOut->Info(kStyleTargetName, "{}", pTarget->module);
            mOut->Info(kStyleTargetType, " {}\n", TargetTypeToString(pTarget->type));

            depth += 2;

            if (pTarget->dependencies.size())
            {
                for (auto i = 0; i < depth; i++)
                    mOut->Info({}, "  ");

                mOut->Info(kStyleCategoryTitle, "Depends on:\n", pTarget->module);

                for (auto &dep : pTarget->dependencies)
                {
                    for (auto i = 0; i < depth + 1; i++)
                        mOut->Info({}, "  ");

                    mOut->Info(kStyleDepString, "{} => ", dep.ToString());

                    mOut->Info(kStyleCategoryTitle, "{{ ");

                    for (auto &res : dep.resolved)
                    {
                        DebugShowVisualBuildInfo(res, depth + 2);
                        // mOut->Info(kStyleDepRes, "{} ", res->module);
                    }

                    mOut->Info(kStyleCategoryTitle, "}}\n");

                    /*
                    for (auto& res : dep.resolved)
                    {
                        for (auto i = 0; i < depth + 2; i++)
                            mOut->Info({}, "  ");

                        DebugShowVisualBuildInfo(res, depth + 2);
                    }
                    */
                }
            }

            if (pTarget->children.size())
            {
                for (auto i = 0; i < depth; i++)
                    mOut->Info({}, "  ");

                mOut->Info(kStyleCategoryTitle, "Children:\n", pTarget->module);

                for (auto &child : pTarget->children)
                {
                    for (auto i = 0; i < depth + 1; i++)
                        mOut->Info({}, "  ");

                    DebugShowVisualBuildInfo(child.get(), depth);
                }
            }

            if (mVars.GetVar("target-configs").value_or("false") == "true")
            {
                mOut->Info({}, "{}", pTarget->resolved_config.dump().c_str());
            }
        }
    }

    void BuildEnv::SetDepsVersionCache(DepsVersionCache *cache)
    {
        mDepsVersionCache = cache;
    }
} // namespace re
