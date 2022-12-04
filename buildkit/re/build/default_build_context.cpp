#include "default_build_context.h"
#include "ninja_gen.h"

#include <re/langs/cxx_lang_provider.h>

#include <re/deps/vcpkg_dep_resolver.h>
#include <re/deps/git_dep_resolver.h>
#include <re/deps/github_dep_resolver.h>
#include <re/deps/arch_coerced_dep_resolver.h>
#include <re/deps/fs_dep_resolver.h>

#include <ninja/tool_main.h>
#include <ninja/manifest_parser.h>

#include <re/debug.h>

#include <fmt/format.h>
#include <fmt/color.h>

#include <fstream>

namespace re
{
	DefaultBuildContext::DefaultBuildContext()
		: mVars{ &mVarContext, "re" }
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
 
		mVars.SetVar("cxx-default-include-dirs", ".");
		mVars.SetVar("cxx-default-lib-dirs", ".");

		mVars.SetVar("host-arch", "x64");

		mVars.SetVar("generate-build-meta", "false");
		mVars.SetVar("auto-load-uncached-deps", "true");
	}

	void DefaultBuildContext::LoadDefaultEnvironment(const fs::path& data_path, const fs::path& dynamic_data_path)
	{
		re::PerfProfile _{ __FUNCTION__ };

		mDataPath = data_path;

		mEnv = std::make_unique<BuildEnv>(mVars);

		auto& cxx = mLangs.emplace_back(std::make_unique<CxxLangProvider>(mDataPath / "data" / "environments" / "cxx", &mVars));
		mEnv->AddLangProvider("cpp", cxx.get());

		auto vcpkg_resolver = std::make_unique<VcpkgDepResolver>(dynamic_data_path / "deps" / "vcpkg");
		auto git_resolver = std::make_unique<GitDepResolver>(mEnv.get());
		auto github_resolver = std::make_unique<GithubDepResolver>(git_resolver.get());

		auto ac_resolver = std::make_unique<ArchCoercedDepResolver>(mEnv.get());
		auto fs_resolver = std::make_unique<FsDepResolver>(mEnv.get());

		mEnv->AddDepResolver("vcpkg", vcpkg_resolver.get());
		mEnv->AddDepResolver("vcpkg-dep", vcpkg_resolver.get());

		mEnv->AddDepResolver("git", git_resolver.get());
		mEnv->AddDepResolver("github", github_resolver.get());
		mEnv->AddDepResolver("github-ssh", github_resolver.get());

		mEnv->AddDepResolver("arch-coerced", ac_resolver.get());
		mEnv->AddDepResolver("fs", fs_resolver.get());

		mDepResolvers.emplace_back(std::move(vcpkg_resolver));
		mDepResolvers.emplace_back(std::move(git_resolver));
		mDepResolvers.emplace_back(std::move(github_resolver));
		mDepResolvers.emplace_back(std::move(ac_resolver));

		mEnv->LoadCoreProjectTarget(mDataPath / "data" / "core-project");
	}

	Target& DefaultBuildContext::LoadTarget(const fs::path& path)
	{
		re::PerfProfile _{ fmt::format(R"({}("{}"))", __FUNCTION__, path.u8string())};

		if (!DoesDirContainTarget(path))
			RE_THROW TargetLoadException(nullptr, "The directory '{}' does not contain a valid Re target.", path.u8string());

		auto& target = mEnv->LoadTarget(path);
		return target;
	}

	YAML::Node DefaultBuildContext::LoadCachedParams(const fs::path& path)
	{
		std::ifstream file{ path / "re.user.yml" };

		if (file.good())
		{
			auto yaml = YAML::Load(file);

			for (const auto& kv : yaml)
				mVars.SetVar(mVars.Resolve(kv.first.Scalar()), mVars.Resolve(kv.second.Scalar()));

			return yaml;
		}

		return YAML::Node{ YAML::Null };
	}

	void DefaultBuildContext::SaveCachedParams(const fs::path& path, const YAML::Node& node)
	{
		std::ofstream file{ path / "re.user.yml" };

		YAML::Emitter emitter;
		emitter << node;

		file << emitter.c_str();
	}

	NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTarget(Target& target)
	{
		re::PerfProfile _{ fmt::format(R"({}("{}"))", __FUNCTION__, target.module) };

		NinjaBuildDesc desc;
		desc.pRootTarget = &target;

		for (auto dep : mEnv->GetSingleTargetLocalDepSet(&target))
		{
			dep->var_parent = &mVars;
			mEnv->InitializeTargetLinkEnvWithDeps(dep, desc);
		}

		auto deps = mEnv->GetSingleTargetDepSet(desc.pRootTarget);

		for (auto dep : deps)
		{
			dep->var_parent = &mVars;

			mEnv->InitializeTargetLinkEnvWithDeps(dep, desc);
			mVars.AddNamespace("target." + target.module, &target);

			mEnv->RunActionsCategorized(dep, nullptr, "pre-configure");
		}

		deps = mEnv->GetSingleTargetDepSet(desc.pRootTarget);

		mEnv->PopulateBuildDescWithDeps(&target, desc);

		auto& vars = target.build_var_scope.value();

		auto root_arch = vars.ResolveLocal("arch");

		auto out_dir = target.path / "out";

		if (auto entry = target.GetCfgEntry<std::string>("out-dir"))
		{
			out_dir = vars.Resolve(*entry);
			if (!out_dir.is_absolute())
				out_dir = target.path / out_dir;
		}

		constexpr auto kDefaultDirTriplet = "${arch}-${platform}-${configuration}";
		out_dir /= vars.Resolve(target.GetCfgEntry<std::string>("out-dir-triplet", CfgEntryKind::Recursive).value_or(kDefaultDirTriplet));

		fs::create_directories(out_dir);
		std::ofstream create_temp{ out_dir / ".re-ignore-this" };

		desc.out_dir = out_dir;

		for (auto& dep : deps)
		{
			if (!dep->build_var_scope)
				continue;

			LocalVarScope module_name_scope{ &dep->build_var_scope.value(), dep->module };

			auto artifact_out_format = dep->GetCfgEntry<std::string>("out-artifact-dir", CfgEntryKind::Recursive).value_or("build/${module}");
			auto object_out_format = dep->GetCfgEntry<std::string>("out-object-dir", CfgEntryKind::Recursive).value_or("obj/${module}");

			module_name_scope.SetVar("module", dep->module);
			module_name_scope.SetVar("src", dep->path.u8string());
			module_name_scope.SetVar("out", out_dir.u8string());

			auto artifact_dir = module_name_scope.Resolve(artifact_out_format);
			auto object_dir = module_name_scope.Resolve(object_out_format);

			desc.init_vars["re_target_artifact_directory_" + GetEscapedModulePath(*dep)] = artifact_dir;
			desc.init_vars["re_target_object_directory_" + GetEscapedModulePath(*dep)] = object_dir;

			dep->build_var_scope->SetVar("src-dir", dep->path.u8string());
			dep->build_var_scope->SetVar("artifact-dir", (desc.out_dir / artifact_dir).u8string());
			dep->build_var_scope->SetVar("object-dir", (desc.out_dir / object_dir).u8string());
		}

		desc.meta["root_target"] = target.module;

		return desc;
	}

	NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTargetInDir(const fs::path& path)
	{
		auto& target = LoadTarget(path);
		return GenerateBuildDescForTarget(target);
	}

	void DefaultBuildContext::SaveTargetMeta(const NinjaBuildDesc& desc)
	{
		auto cache_path = desc.pRootTarget->path / ".re-cache" / "meta";

		fs::create_directories(cache_path);

		std::ofstream file{ cache_path / "full.json" };
		file << desc.meta.dump();
	}

	int DefaultBuildContext::BuildTarget(const NinjaBuildDesc& desc)
	{
		re::PerfProfile _{ fmt::format(R"({}("{}"))", __FUNCTION__, desc.out_dir.u8string()) };

		auto style = fmt::emphasis::bold | fg(fmt::color::aquamarine);

		fmt::print(style, " - Generating build files\n");

		re::GenerateNinjaBuildFile(desc, desc.out_dir);
		SaveTargetMeta(desc);

		fmt::print(style, " - Running pre-build actions\n");

		for (auto& dep : mEnv->GetSingleTargetDepSet(desc.pRootTarget))
			mEnv->RunActionsCategorized(dep, &desc, "pre-build");

		fmt::print(style, " - Building...\n\n");

		auto out_dir = desc.out_dir.u8string();

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

		::Status *status = new StatusPrinter(config);

		// status->Info("Running Ninja!");

		options.working_dir = out_dir.c_str();
		options.input_file = "build.ninja";
		options.dupe_edges_should_err = true;

		if (options.working_dir)
		{
			status->Info("Entering directory `%s'", options.working_dir);

			std::filesystem::current_path(options.working_dir);
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
			RE_THROW TargetBuildException(desc.pRootTarget, "Failed to load generated config: {}", err);
			exit(1);
		}

		if (!ninja.EnsureBuildDirExists())
			RE_THROW TargetBuildException(desc.pRootTarget, "ninja.EnsureBuildDirExists() failed");

		if (!ninja.OpenBuildLog() || !ninja.OpenDepsLog())
			RE_THROW TargetBuildException(desc.pRootTarget, "ninja.OpenBuildLog() || ninja.OpenDepsLog() failed");

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

		std::vector<const char*> targets = {};

		int result = ninja.RunBuild(targets.size(), (char**) targets.data(), status);

		if (result)
			RE_THROW TargetBuildException(desc.pRootTarget, "Ninja build failed: exit_code={}", result);

		if (g_metrics)
			ninja.DumpMetrics();

		/*
		#ifdef WIN32
				auto path_to_ninja = mDataPath / "ninja.exe";

				std::vector<std::wstring> cmdline;

				cmdline.push_back(path_to_ninja.wstring());
				cmdline.push_back(L"-C");
				cmdline.push_back(desc.out_dir.wstring());

				int result = RunProcessOrThrowWindows("ninja", cmdline, true, true);
		#else
				auto path_to_ninja = mDataPath / "ninja";

				std::vector<std::string> cmdline;

				cmdline.push_back(path_to_ninja.u8string());
				cmdline.push_back("-C");
				cmdline.push_back(desc.out_dir.u8string());

				int result = RunProcessOrThrow("ninja", cmdline, true, true);
		#endif
		*/

		fmt::print(style, "\n - Running post-build actions\n\n");

		// Running post-build actions
		for (auto& dep : mEnv->GetSingleTargetDepSet(desc.pRootTarget))
			mEnv->RunActionsCategorized(dep, &desc, "post-build");

		fmt::print(style, " - Build successful!\n");

		return result;
	}

	void DefaultBuildContext::InstallTarget(const NinjaBuildDesc& desc)
	{
		mEnv->RunInstallActions(desc.pRootTarget, desc);
	}
}
