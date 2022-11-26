#include "default_build_context.h"
#include "ninja_gen.h"

#include <re/langs/cxx_lang_provider.h>

#include <re/deps/vcpkg_dep_resolver.h>
#include <re/deps/git_dep_resolver.h>
#include <re/deps/github_dep_resolver.h>
#include <re/deps/arch_coerced_dep_resolver.h>

#include <re/process_util.h>

#include <re/debug.h>

#include <fstream>

namespace re
{
	DefaultBuildContext::DefaultBuildContext()
		: mVars{ &mVarContext, "re" }
	{
		mVars.AddNamespace("env", &mSystemEnvVars);

		mVars.SetVar("version", "1.0");
		mVars.SetVar("platform", "windows");
		mVars.SetVar("platform-closest", "unix");

		mVars.SetVar("cxx-default-include-dirs", ".");
		mVars.SetVar("cxx-default-lib-dirs", ".");

		mVars.SetVar("host-arch", "x64");
	}

	void DefaultBuildContext::LoadDefaultEnvironment(const fs::path& re_path)
	{
		re::PerfProfile _{ __FUNCTION__ };

		mRePath = re_path;

		mEnv = std::make_unique<BuildEnv>(mVars);

		auto& cxx = mLangs.emplace_back(std::make_unique<CxxLangProvider>(mRePath / "data" / "environments" / "cxx", &mVars));
		mEnv->AddLangProvider("cpp", cxx.get());

		auto vcpkg_resolver = std::make_unique<VcpkgDepResolver>(mRePath / "deps" / "vcpkg");
		auto git_resolver = std::make_unique<GitDepResolver>(mEnv.get());
		auto github_resolver = std::make_unique<GithubDepResolver>(git_resolver.get());

		auto ac_resolver = std::make_unique<ArchCoercedDepResolver>(mEnv.get());

		mEnv->AddDepResolver("vcpkg", vcpkg_resolver.get());
		mEnv->AddDepResolver("vcpkg-dep", vcpkg_resolver.get());

		mEnv->AddDepResolver("git", git_resolver.get());
		mEnv->AddDepResolver("github", github_resolver.get());
		mEnv->AddDepResolver("github-ssh", github_resolver.get());

		mEnv->AddDepResolver("arch-coerced", ac_resolver.get());

		mDepResolvers.emplace_back(std::move(vcpkg_resolver));
		mDepResolvers.emplace_back(std::move(git_resolver));
		mDepResolvers.emplace_back(std::move(github_resolver));
		mDepResolvers.emplace_back(std::move(ac_resolver));

		mEnv->LoadCoreProjectTarget(mRePath / "data" / "core-project");
	}

	Target& DefaultBuildContext::LoadTarget(const fs::path& path)
	{
		re::PerfProfile _{ fmt::format(R"({}("{}"))", __FUNCTION__, path.u8string())};

		if (!DoesDirContainTarget(path))
			RE_THROW TargetLoadException(nullptr, "The directory '{}' does not contain a valid Re target.", path.u8string());

		auto& target = mEnv->LoadTarget(path);
		return target;
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

		std::filesystem::create_directories(out_dir);
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
		}

		return desc;
	}

	NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTargetInDir(const fs::path& path)
	{
		auto& target = LoadTarget(path);
		return GenerateBuildDescForTarget(target);
	}

	int DefaultBuildContext::BuildTarget(const NinjaBuildDesc& desc)
	{
		re::PerfProfile _{ fmt::format(R"({}("{}"))", __FUNCTION__, desc.out_dir.u8string()) };

		re::GenerateNinjaBuildFile(desc, desc.out_dir);

		auto path_to_ninja = mRePath / "ninja.exe";

		std::vector<std::wstring> cmdline;

		cmdline.push_back(path_to_ninja.wstring());
		cmdline.push_back(L"-C");
		cmdline.push_back(desc.out_dir.wstring());

		for (auto& dep : mEnv->GetSingleTargetDepSet(desc.pRootTarget))
			mEnv->RunActionsCategorized(dep, &desc, "pre-build");

		int result = RunProcessOrThrowWindows("ninja", cmdline, true, true);

		// Running post-build actions
		for (auto& dep : mEnv->GetSingleTargetDepSet(desc.pRootTarget))
			mEnv->RunActionsCategorized(dep, &desc, "post-build");

		return result;
	}

	void DefaultBuildContext::InstallTarget(const NinjaBuildDesc& desc)
	{
		mEnv->RunInstallActions(desc.pRootTarget, desc);
	}
}
