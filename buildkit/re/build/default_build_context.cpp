#include "default_build_context.h"
#include "boost/algorithm/string/replace.hpp"
#include "ninja_gen.h"
#include "re/error.h"
#include "re/lang_provider.h"
#include "re/target.h"
#include "re/yaml_merge.h"
#include "yaml-cpp/emitter.h"

#include <re/fs.h>
#include <re/langs/cxx/cxx_lang_provider.h>

#include <re/langs/cxx/features/cpp2_translation.h>
#include <re/langs/cxx/features/cxx_header_projection.h>
#include <re/langs/cxx/features/source_translation.h>

#include <re/langs/cmake/cmake_lang_provider.h>
#include <re/langs/cmake/cmake_target_load_middleware.h>

#include <re/deps/arch_coerced_dep_resolver.h>
#include <re/deps/conan_dep_resolver.h>
#include <re/deps/fs_dep_resolver.h>
#include <re/deps/git_dep_resolver.h>
#include <re/deps/github_dep_resolver.h>
#include <re/deps/vcpkg_dep_resolver.h>

#include <re/deps_version_cache.h>

#include <ninja/manifest_parser.h>
#include <ninja/tool_main.h>

#include <re/debug.h>

#include <fmt/color.h>
#include <fmt/format.h>

#include <fstream>

#include <magic_enum.hpp>
#include <unordered_set>

namespace re
{
    DefaultBuildContext::DefaultBuildContext() : mVars{&mVarContext, "re"}
    {
        mVars.AddNamespace("env", &mSystemEnvVars);

        mVars.SetVar("version", "1.0");

#if defined(WIN32)
        mVars.SetVar("platform", "windows");
        mVars.SetVar("platform-closest", "unix");
#elif defined(__linux__)
        mVars.SetVar("platform", "linux");
        mVars.SetVar("platform-closest", "unix");
#elif defined(__APPLE__)
        mVars.SetVar("platform", "osx");
        mVars.SetVar("platform-closest", "unix");
#endif

        mVars.SetVar("host-platform", mVars.ResolveLocal("platform"));

        mVars.SetVar("cxx-default-include-dirs", ".");
        mVars.SetVar("cxx-default-lib-dirs", ".");

        mVars.SetVar("host-arch", "x64");

        mVars.SetVar("generate-build-meta", "false");
        mVars.SetVar("auto-load-uncached-deps", "true");

        mVars.SetVar("msg-level", "info");
        mVars.SetVar("colors", "true");

        UpdateOutputSettings();
    }

    void DefaultBuildContext::LoadDefaultEnvironment(const fs::path &data_path, const fs::path &dynamic_data_path)
    {
        re::PerfProfile _{__FUNCTION__};

        mDataPath = data_path;
        LoadCachedParams(mDataPath / "data");

        mEnv = std::make_unique<BuildEnv>(mVars, this);

        auto &cxx =
            mLangs.emplace_back(std::make_unique<CxxLangProvider>(mDataPath / "data" / "environments" / "cxx", &mVars));
        mEnv->AddLangProvider("cpp", cxx.get());

        auto vcpkg_deps_path = dynamic_data_path / "deps" / "vcpkg";
        fs::create_directories(vcpkg_deps_path);

        auto vcpkg_resolver = std::make_unique<VcpkgDepResolver>(vcpkg_deps_path, this);
        auto git_resolver = std::make_unique<GitDepResolver>(mEnv.get(), this);
        auto github_resolver = std::make_unique<GithubDepResolver>(git_resolver.get());

        auto ac_resolver = std::make_unique<ArchCoercedDepResolver>(mEnv.get());
        auto fs_resolver = std::make_unique<FsDepResolver>(mEnv.get());

        auto conan_resolver = std::make_unique<ConanDepResolver>(this);

        mEnv->AddDepResolver("vcpkg", vcpkg_resolver.get());
        mEnv->AddDepResolver("vcpkg-dep", vcpkg_resolver.get());

        mEnv->AddDepResolver("git", git_resolver.get());
        mEnv->AddDepResolver("github", github_resolver.get());
        mEnv->AddDepResolver("github-ssh", github_resolver.get());

        mEnv->AddDepResolver("arch-coerced", ac_resolver.get());
        mEnv->AddDepResolver("fs", fs_resolver.get());

        mEnv->AddDepResolver("conan", conan_resolver.get());

        mDepResolvers.emplace_back(std::move(vcpkg_resolver));
        mDepResolvers.emplace_back(std::move(git_resolver));
        mDepResolvers.emplace_back(std::move(github_resolver));
        mDepResolvers.emplace_back(std::move(ac_resolver));
        mDepResolvers.emplace_back(std::move(conan_resolver));

        auto global_deps_path = dynamic_data_path / "deps" / "installed";
        fs::create_directories(global_deps_path);

        mGlobalDepResolver = std::make_unique<GlobalDepResolver>(global_deps_path, mEnv.get(), this);
        mEnv->AddDepResolver("global", mGlobalDepResolver.get());

        constexpr auto kHeaderProjRoot = ".re-cache/header-projection";

        auto cxx_header_projection = std::make_unique<CxxHeaderProjection>();
        auto cpp2_translation = std::make_unique<Cpp2Translation>();
        auto source_translation = std::make_unique<SourceTranslation>();

        mEnv->AddTargetFeature(cxx_header_projection.get());
        mEnv->AddTargetFeature(cpp2_translation.get());
        mEnv->AddTargetFeature(source_translation.get());

        mVars.SetVar("cxx-header-projection-root", kHeaderProjRoot);

        mTargetFeatures.emplace_back(std::move(cxx_header_projection));
        mTargetFeatures.emplace_back(std::move(cpp2_translation));
        mTargetFeatures.emplace_back(std::move(source_translation));

        auto &cmake = mLangs.emplace_back(std::make_unique<CMakeLangProvider>(&mVars));
        mEnv->AddLangProvider("cmake", cmake.get());

        auto cmake_middleware = std::make_unique<CMakeTargetLoadMiddleware>(mDataPath / "data" / "cmake-adapter", this);

        mEnv->AddTargetLoadMiddleware(cmake_middleware.get());

        mTargetLoadMiddlewares.emplace_back(std::move(cmake_middleware));

        mEnv->LoadCoreProjectTarget(mDataPath / "data" / "core-project");
    }

    Target &DefaultBuildContext::LoadTarget(const fs::path &path)
    {
        re::PerfProfile _{fmt::format(R"({}("{}"))", __FUNCTION__, path.u8string())};

        auto &target = mEnv->LoadTarget(path);
        return target;
    }

    YAML::Node DefaultBuildContext::LoadCachedParams(const fs::path &path)
    {
        std::ifstream file{path / "re.user.yml"};

        if (file.good())
        {
            auto yaml = YAML::Load(file);

            for (const auto &kv : yaml)
                mVars.SetVar(mVars.Resolve(kv.first.Scalar()), mVars.Resolve(kv.second.Scalar()));

            return yaml;
        }

        return YAML::Node{YAML::Null};
    }

    void DefaultBuildContext::SaveCachedParams(const fs::path &path, const YAML::Node &node)
    {
        std::ofstream file{path / "re.user.yml"};

        YAML::Emitter emitter;
        emitter << node;

        file << emitter.c_str();
    }

    void DefaultBuildContext::ResolveAllTargetDependencies(Target *pRootTarget)
    {
        re::PerfProfile _{fmt::format(R"({}("{}"))", __FUNCTION__, pRootTarget->module)};

        while (true)
        {
            bool has_unresolved_deps = false;

            struct WantedDependency
            {
                Target *target;
                TargetDependency *dep;
            };

            std::unordered_map<std::string, std::vector<WantedDependency>> wanted_deps;

            auto targets = mEnv->GetSingleTargetLocalDepSet(pRootTarget);

            for (auto &target : targets)
            {
                for (auto &dep : target->dependencies)
                {
                    auto id = fmt::format("{}:{}", dep.ns.empty() ? "local" : dep.ns, dep.name);
                    wanted_deps[id].push_back({target, &dep});

                    if (dep.resolved.empty())
                    {
                        has_unresolved_deps = true;
                    }
                }
            }

            for (auto &[id, wants] : wanted_deps)
            {
                fmt::print(fmt::emphasis::bold, "{}:\n", id);

                for (auto &want : wants)
                {
                    fmt::print("    wanted by {} ({}{})\n", want.target->module, want.dep->version_kind_str,
                               want.dep->version);
                    fmt::print("\n");
                }
            }

            has_unresolved_deps = false;

            if (!has_unresolved_deps)
                break;
        }
    }

    NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTarget(Target &target, Target *build_target)
    {
        re::PerfProfile _{fmt::format(R"({}("{}"))", __FUNCTION__, target.module)};

        NinjaBuildDesc desc;
        desc.pRootTarget = &target;
        desc.pBuildTarget = build_target ? build_target : desc.pRootTarget;

        // ResolveAllTargetDependencies(desc.pBuildTarget);

        auto version_cache_path = target.root->path / "re-deps-lock.json";

        {
            std::ifstream file(version_cache_path);

            if (!file.fail() && file.good() && file.is_open())
            {
                std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                mDepsVersionCache = std::make_unique<DepsVersionCache>(nlohmann::json::parse(contents));
            }
            else
            {
                mDepsVersionCache = std::make_unique<DepsVersionCache>(nlohmann::json::object({}));
            }

            mEnv->SetDepsVersionCache(mDepsVersionCache.get());
        }

        fs::remove_all(target.root->path / ".re-cache" / "header-projection");

        for (auto dep : mEnv->GetSingleTargetLocalDepSet(desc.pBuildTarget))
        {
            dep->var_parent = &mVars;
            mEnv->InitializeTargetLinkEnvWithDeps(dep, desc);
        }

        auto deps = mEnv->GetSingleTargetDepSet(desc.pBuildTarget);

        for (auto dep : deps)
        {
            dep->var_parent = &mVars;

            mEnv->InitializeTargetLinkEnvWithDeps(dep, desc);
            // mVars.AddNamespace("target." + target.module, &target);

            mEnv->RunActionsCategorized(dep, nullptr, "pre-configure");
            mEnv->RunAutomaticStructuredTasks(dep, nullptr, "pre-configure");

            for (auto &[key, object] : dep->features)
            {
                object->ProcessTargetPostInit(*dep);
            }
        }

        deps = mEnv->GetSingleTargetDepSet(desc.pBuildTarget);

        mEnv->PopulateBuildDescWithDeps(desc.pBuildTarget, desc);

        auto &vars = target.build_var_scope.value();

        auto root_arch = vars.ResolveLocal("arch");

        auto out_dir = target.root->path / "out";

        if (auto entry = target.GetCfgEntry<std::string>("out-dir"))
        {
            out_dir = vars.Resolve(*entry);

            if (target.path <= fs::current_path() || out_dir.u8string().front() == '.')
                out_dir = target.path / out_dir;
        }

        constexpr auto kDefaultDirTriplet = "${arch}-${platform}-${configuration}";
        out_dir /= vars.Resolve(
            target.GetCfgEntry<std::string>("out-dir-triplet", CfgEntryKind::Recursive).value_or(kDefaultDirTriplet));

        fs::create_directories(out_dir);
        out_dir = fs::canonical(out_dir);

        std::ofstream create_temp{out_dir / ".re-ignore-this"};

        desc.out_dir = out_dir;

        for (auto &dep : deps)
        {
            if (!dep->build_var_scope)
                continue;

            LocalVarScope module_name_scope{&dep->build_var_scope.value(), dep->module};

            auto artifact_out_format =
                dep->GetCfgEntry<std::string>("out-artifact-dir", CfgEntryKind::Recursive).value_or("build/${module}");
            auto object_out_format =
                dep->GetCfgEntry<std::string>("out-object-dir", CfgEntryKind::Recursive).value_or("obj/${module}");

            module_name_scope.SetVar("module", dep->module);
            module_name_scope.SetVar("src", dep->path.u8string());
            module_name_scope.SetVar("out", out_dir.u8string());
            module_name_scope.SetVar("root", desc.pRootTarget->path.u8string());

            auto artifact_dir = module_name_scope.Resolve(artifact_out_format);
            auto object_dir = module_name_scope.Resolve(object_out_format);

            desc.init_vars["re_target_artifact_directory_" + GetEscapedModulePath(*dep)] = artifact_dir;
            desc.init_vars["re_target_object_directory_" + GetEscapedModulePath(*dep)] = object_dir;

            auto &full_src_dir = dep->path;
            auto full_artifact_dir = desc.out_dir / artifact_dir;
            auto full_object_dir = desc.out_dir / object_dir;

            dep->build_var_scope->SetVar("src-dir", full_src_dir.u8string());
            dep->build_var_scope->SetVar("artifact-dir", full_artifact_dir.u8string());
            dep->build_var_scope->SetVar("object-dir", full_object_dir.u8string());

            if (auto artifact = dep->build_var_scope->GetVarNoRecurse("build-artifact"))
                dep->build_var_scope->SetVar("main-artifact", (full_artifact_dir / *artifact).u8string());

            // dep->build_var_scope->SetVar("root-out-dir", out_dir.u8string());
            // dep->build_var_scope->SetVar("root-dir", desc.pRootTarget->path.u8string());

            auto &meta = desc.meta["targets"][full_src_dir.u8string()];

            meta["src-dir"] = full_src_dir.u8string();
            meta["artifact-dir"] = full_artifact_dir.u8string();
            meta["object-dir"] = full_object_dir.u8string();

            if (auto artifact = dep->build_var_scope->GetVarNoRecurse("build-artifact"))
                meta["main-artifact"] = (full_artifact_dir / *artifact).u8string();

            meta["is-external-dep"] = module_name_scope.GetVar("is-external-dep").value_or("false") == "true";

            auto &unused_sources = (meta["unused-sources"] = nlohmann::json::array());

            for (auto &src : dep->unused_sources)
                unused_sources.push_back(src.path.generic_u8string());
        }

        for (auto &dep : deps)
        {
            if (!dep->build_var_scope)
                continue;

            for (auto &other_dep : deps)
            {
                if (!other_dep->build_var_scope)
                    continue;

                dep->build_var_scope->AddNamespace("target/" + other_dep->module, &*other_dep->build_var_scope);
            }
        }

        // Resolve all the paths
        for (auto it = desc.artifacts.begin(); it != desc.artifacts.end(); it++)
        {
            it.value() = it->first->build_var_scope->Resolve(it->second.generic_u8string());
        }

        desc.meta["root_target"] = target.root->module;

        if (mVars.GetVarNoRecurse("no-meta").value_or("false") != "true")
        {
            auto &data = mDepsVersionCache->GetData();

            if (!data.empty())
            {
                std::ofstream file(version_cache_path);
                file << data.dump(4);
            }
        }

        return desc;
    }

    NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTargetInDir(const fs::path &path)
    {
        auto &target = LoadTarget(path);
        return GenerateBuildDescForTarget(target);
    }

    void DefaultBuildContext::SaveTargetMeta(const NinjaBuildDesc &desc)
    {
        auto cache_path = desc.pRootTarget->path / ".re-cache" / "meta";

        fs::create_directories(cache_path);

        std::ofstream file{cache_path / "full.json"};
        file << desc.meta.dump();
        file.close();

        for (auto &dep : mEnv->GetSingleTargetDepSet(desc.pBuildTarget))
        {
            mEnv->RunActionsCategorized(dep, &desc, "meta-available");
            mEnv->RunAutomaticStructuredTasks(dep, &desc, "meta-available");
        }
    }

    int DefaultBuildContext::BuildTarget(const NinjaBuildDesc &desc)
    {
        re::PerfProfile perf{fmt::format(R"({}("{}"))", __FUNCTION__, desc.out_dir.u8string())};

        auto current_path_at_invoke = fs::current_path();

        auto style = fmt::emphasis::bold | fg(fmt::color::aquamarine);

        Info(style, " - Generating build files\n");

        re::GenerateNinjaBuildFile(desc, desc.out_dir);

        if (mVars.GetVarNoRecurse("no-meta").value_or("false") != "true")
            SaveTargetMeta(desc);

        Info(style, " - Running pre-build actions\n");

        for (auto &dep : mEnv->GetSingleTargetDepSet(desc.pBuildTarget))
        {
            for (auto &[key, object] : dep->features)
                object->ProcessTargetPreBuild(*dep);

            mEnv->RunActionsCategorized(dep, &desc, "pre-build");
            mEnv->RunAutomaticStructuredTasks(dep, &desc, "pre-build");
        }

        Info(style, " - Building...\n\n");

        for (auto &subninja : desc.subninjas)
            RunNinjaBuild(subninja, desc.pBuildTarget);

        auto result = RunNinjaBuild(desc.out_dir / "build.ninja", desc.pBuildTarget);

        Info(style, "\n - Running post-build actions\n\n");

        // Running post-build actions
        for (auto &dep : mEnv->GetSingleTargetDepSet(desc.pBuildTarget))
        {
            mEnv->RunActionsCategorized(dep, &desc, "post-build");
            mEnv->RunAutomaticStructuredTasks(dep, &desc, "post-build");
        }

        perf.Finish();

        Info(style, " - Build successful! ({})\n", perf.ToString());

        Info(style, "\n - Built {} artifacts:\n", desc.artifacts.size());

        for (auto &[target, artifact] : desc.artifacts)
        {
            auto style = fmt::emphasis::bold | fg(fmt::color::royal_blue);

            Info(fg(fmt::color::dim_gray), "     {}:\n", target->module);

            fs::path path = artifact.lexically_relative(current_path_at_invoke);
            Info(fg(fmt::color::dim_gray), "       {}\n", path.generic_u8string());
        }

        Info(style, "\n");

        return result;
    }

    int DefaultBuildContext::RunNinjaBuild(const fs::path &script, const Target *root)
    {
        auto out_dir = script.parent_path().u8string();
        auto script_name = script.filename().u8string();

        ::BuildConfig config;
        ninja::Options options;

        switch (int processors = GetProcessorCount())
        {
        case 0:
        case 1:
            config.parallelism = 2;
        case 2:
            config.parallelism = 3;
        default:
            config.parallelism = processors + 2;
        }

        if (auto parallelism = mVars.GetVar("parallelism"))
            config.parallelism = std::stoi(*parallelism);

        class ReAwareStatusPrinter : public ::StatusPrinter
        {
        public:
            ReAwareStatusPrinter(const ::BuildConfig &config, IUserOutput *pOut) : ::StatusPrinter{config}, mOut{pOut}
            {
            }

            virtual ~ReAwareStatusPrinter()
            {
            }

            virtual void Info(const char *msg, ...)
            {
                va_list ap;
                va_start(ap, msg);
                mOut->Info({}, "ninja: {}\n", FormatString(msg, ap));
                va_end(ap);
            }

            virtual void Warning(const char *msg, ...)
            {
                va_list ap;
                va_start(ap, msg);
                mOut->Warn(fmt::fg(fmt::color::yellow), "warning: {}\n", FormatString(msg, ap));
                va_end(ap);
            }

            virtual void Error(const char *msg, ...)
            {
                va_list ap;
                va_start(ap, msg);
                mOut->Error(fmt::fg(fmt::color::pale_violet_red), "error: {}\n", FormatString(msg, ap));
                va_end(ap);
            }

        private:
            IUserOutput *mOut;

            std::string FormatString(const char *msg, va_list args)
            {
                std::string result;

                result.resize(std::vsnprintf(nullptr, 0, msg, args));
                std::vsnprintf(result.data(), result.size(), msg, args);

                return result;
            }
        };

        ReAwareStatusPrinter status{config, this};

        // status->Info("Running Ninja!");

        options.working_dir = out_dir.c_str();
        options.input_file = script_name.c_str();
        options.dupe_edges_should_err = true;

        if (options.working_dir)
        {
            Info({}, "ninja: Entering directory `{}'\n", out_dir);

            fs::current_path(script.parent_path());
        }

        ninja::NinjaMain ninja("", config);

        ManifestParserOptions parser_opts;
        if (options.dupe_edges_should_err)
        {
            parser_opts.dupe_edge_action_ = kDupeEdgeActionError;
        }
        if (options.phony_cycle_should_err)
        {
            parser_opts.phony_cycle_action_ = kPhonyCycleActionError;
        }

        ManifestParser parser(&ninja.state_, &ninja.disk_interface_, parser_opts);

        std::string err;
        if (!parser.Load(options.input_file, &err))
        {
            RE_THROW TargetBuildException(root, "Failed to load generated config: {}", err);
            exit(1);
        }

        if (!ninja.EnsureBuildDirExists())
            RE_THROW TargetBuildException(root, "ninja.EnsureBuildDirExists() failed");

        if (!ninja.OpenBuildLog() || !ninja.OpenDepsLog())
            RE_THROW TargetBuildException(root, "ninja.OpenBuildLog() || ninja.OpenDepsLog() failed");

        /*
        // Attempt to rebuild the manifest before building anything else
        if (ninja.RebuildManifest(options.input_file, &err, status))
        {
            // In dry_run mode the regeneration will succeed without changing the
            // manifest forever. Better to return immediately.
            if (config.dry_run)
                exit(0);
            // Start the build over with the new manifest.
            continue;
        }
        else if (!err.empty())
        {
            status->Error("rebuilding '%s': %s", options.input_file, err.c_str());
            exit(1);
        }
        */

        std::vector<const char *> targets = {};

        int result = ninja.RunBuild(targets.size(), (char **)targets.data(), &status);

        if (result)
            RE_THROW TargetBuildException(root, "Ninja build failed: exit_code={}", result);

        if (g_metrics)
            ninja.DumpMetrics();

        Info({}, "\n");

        return result;
    }

    void DefaultBuildContext::InstallTarget(const NinjaBuildDesc &desc)
    {
        mEnv->RunInstallActions(desc.pBuildTarget, desc);
    }

    void DefaultBuildContext::DoPrint(UserOutputLevel level, fmt::text_style style, std::string_view text)
    {
        auto is_problem = (level <= UserOutputLevel::Warn);

        if (mOutLevel < level && !(mOutLevel == UserOutputLevel::Problems && is_problem))
            return;

        // auto level_str = magic_enum::enum_name(level);

        auto stream = is_problem ? stderr : stdout;

        if (mOutColors)
            fmt::print(stream, style, "{}", text);
        else
            fmt::print(stream, "{}", text);
    }

    void DefaultBuildContext::UpdateOutputSettings()
    {
        auto no_case_pred = [](char lhs, char rhs) { return std::tolower(lhs) == std::tolower(rhs); };

        mOutLevel = magic_enum::enum_cast<UserOutputLevel>(mVars.ResolveLocal("msg-level"), no_case_pred)
                        .value_or(UserOutputLevel::Info);
        mOutColors = mVars.ResolveLocal("colors") == "true";
    }

    void DefaultBuildContext::ApplyTemplateInDirectory(const fs::path &dir, std::string_view template_name)
    {
        auto template_dir = mDataPath / "data" / "templates" / template_name;

        if (!fs::exists(template_dir))
            RE_THROW Exception("Template '{}' does not exist (dir={})", template_name, template_dir.generic_u8string());

        bool should_merge_configs = (fs::exists(template_dir / "re.yml") && fs::exists(dir / "re.yml"));

        if (should_merge_configs)
        {
            fs::copy(dir / "re.yml", dir / "re.yml._old");
            fs::remove(dir / "re.yml");
        }

        CopyTemplateToDirectory(dir, template_dir);

        if (should_merge_configs)
        {
            std::ifstream t1{dir / "re.yml._old"};
            std::ifstream t2{template_dir / "re.yml"};

            auto old_config = YAML::Load(t1);
            auto new_config = YAML::Load(t2);

            std::ofstream out{dir / "re.yml"};

            YAML::Emitter emitter;
            emitter << MergeYamlNodes(old_config, new_config);

            out << emitter.c_str();

            Warn(fg(fmt::color::light_yellow),
                 "WARN: Merged the existing re.yml with the one specified in the '{}'"
                 "template. Old re.yml saved in `re.yml._old`.",
                 template_name);
        }

        Info(fg(fmt::color::blue_violet), "Applied template '{}' in directory '{}'\n", template_name,
             dir.generic_u8string());
    }

    void DefaultBuildContext::CreateTargetFromTemplate(const fs::path &out_path, std::string_view template_name,
                                                       std::string_view target_name)
    {
        auto template_dir = mDataPath / "data" / "templates" / template_name;

        if (!fs::exists(template_dir))
            RE_THROW Exception("Template '{}' does not exist (dir={})", template_name, template_dir.generic_u8string());

        if (!fs::exists(template_dir / "re.yml"))
            RE_THROW Exception("Template '{}' does not support creating targets from it", template_name);

        if (fs::exists(out_path / "re.yml"))
            RE_THROW Exception("Path '{}' already contains a Re target! If you want to overwrite it, please delete the "
                               "old target re.yml first.");

        CopyTemplateToDirectory(out_path, template_dir);

        std::ifstream t{out_path / "re.yml"};
        std::string content{(std::istreambuf_iterator<char>(t)), (std::istreambuf_iterator<char>())};
        t.close();

        boost::replace_all(content, "{{template-target-name}}", target_name);

        std::ofstream out{out_path / "re.yml"};
        out << content;
        out.close();

        Info(fg(fmt::color::blue_violet), "Created new target '{}' from template '{}' in directory '{}'\n", target_name,
             template_name, out_path.generic_u8string());
    }

    void DefaultBuildContext::CopyTemplateToDirectory(const fs::path &dir, const fs::path &template_dir)
    {
        auto deps_path = template_dir / "template_deps.json";
        auto has_deps = fs::exists(deps_path);

        if (has_deps)
        {
            std::ifstream t{deps_path};
            auto json = nlohmann::json::parse(t);

            for (auto &v : json)
                ApplyTemplateInDirectory(dir, v.get<std::string>());
        }

        fs::copy(template_dir, dir, fs::copy_options::recursive | fs::copy_options::update_existing);

        if (has_deps)
            fs::remove(dir / "template_deps.json");
    }
} // namespace re
