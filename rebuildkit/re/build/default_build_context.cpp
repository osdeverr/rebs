#include "default_build_context.h"
#include "ninja_gen.h"

#include <re/langs/cxx_lang_provider.h>

#include <re/deps/vcpkg_dep_resolver.h>
#include <re/deps/git_dep_resolver.h>
#include <re/deps/github_dep_resolver.h>

#include <re/process_util.h>

namespace re
{
	DefaultBuildContext::DefaultBuildContext()
		: mVars{ &mVarContext, "re" }
	{
		mVars.AddNamespace("env", &mSystemEnvVars);

		mVars.SetVar("version", "1.0");
		mVars.SetVar("platform", "windows");
		mVars.SetVar("platform-closest", "unix");
		mVars.SetVar("arch", "x64");
		mVars.SetVar("configuration", "release");
		mVars.SetVar("cxx-default-include-dirs", ".");
		mVars.SetVar("cxx-default-lib-dirs", ".");
	}

	void DefaultBuildContext::LoadDefaultEnvironment(const fs::path& re_path)
	{
		mRePath = re_path;

		mEnv = std::make_unique<BuildEnv>(mVars);

		auto& cxx = mLangs.emplace_back(std::make_unique<CxxLangProvider>(mRePath / "data" / "environments" / "cxx", &mVarContext));
		mEnv->AddLangProvider("cpp", cxx.get());

		auto& vcpkg_resolver = std::make_unique<VcpkgDepResolver>(mRePath / "deps" / "vcpkg");
		auto& git_resolver = std::make_unique<GitDepResolver>(mEnv.get());
		auto& github_resolver = std::make_unique<GithubDepResolver>(git_resolver.get());

		mDepResolvers.emplace_back(std::move(vcpkg_resolver));
		mDepResolvers.emplace_back(std::move(git_resolver));
		mDepResolvers.emplace_back(std::move(github_resolver));

		mEnv->AddDepResolver("vcpkg", vcpkg_resolver.get());
		mEnv->AddDepResolver("vcpkg-dep", vcpkg_resolver.get());

		mEnv->AddDepResolver("git", git_resolver.get());
		mEnv->AddDepResolver("github", github_resolver.get());
		mEnv->AddDepResolver("github-ssh", github_resolver.get());

		mEnv->LoadCoreProjectTarget(mRePath / "data" / "core-project");
	}

	Target& DefaultBuildContext::LoadTarget(const fs::path& path)
	{
		if (!DoesDirContainTarget(path))
			RE_THROW TargetLoadException(nullptr, "The directory '{}' does not contain a valid Re target.", path.u8string());

		auto& target = mEnv->LoadTarget(path);

		for (auto dep : mEnv->GetSingleTargetDepSet(&target))
			dep->var_parent = &mVars;

		mVars.AddNamespace("target." + target.module, &target);

		return target;
	}

	NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTarget(Target& target)
	{
		LocalVarScope vars{&mVarContext, "", &target};

		auto arch = vars.Resolve("${arch}");
		auto platform = vars.Resolve("${platform}");
		auto configuration = vars.Resolve("${configuration}");

		NinjaBuildDesc desc;
		desc.pRootTarget = &target;

		for (auto& dep : mEnv->GetSingleTargetDepSet(desc.pRootTarget))
			mEnv->RunActionsCategorized(dep, nullptr, "pre-configure");

		auto out_dir = target.path / "out" / fmt::format("{}-{}", arch, platform) / configuration;

		if (auto entry = target.GetCfgEntry<std::string>("output-directory"))
			out_dir = fmt::format(*entry, fmt::arg("arch", arch), fmt::arg("platform", platform));

		std::filesystem::create_directories(out_dir);

		desc.out_dir = out_dir;
		desc.artifact_out_format = target.GetCfgEntry<std::string>("artifact-dir-format", CfgEntryKind::Recursive).value_or("build");
		desc.object_out_format = target.GetCfgEntry<std::string>("object-dir-format", CfgEntryKind::Recursive).value_or("{module}");

		mEnv->PopulateBuildDescWithDeps(&target, desc);
		return desc;
	}

	NinjaBuildDesc DefaultBuildContext::GenerateBuildDescForTargetInDir(const fs::path& path)
	{
		auto& target = LoadTarget(path);
		return GenerateBuildDescForTarget(target);
	}

	int DefaultBuildContext::BuildTarget(const NinjaBuildDesc& desc)
	{
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
