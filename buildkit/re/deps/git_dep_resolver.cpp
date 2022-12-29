#include "git_dep_resolver.h"

#include <fmt/format.h>
#include <fmt/color.h>

#include <re/process_util.h>
#include <re/fs.h>

#include <re/target_cfg_utils.h>

#include <fstream>

namespace re
{
	Target* GitDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		return ResolveGitDependency(target, dep, dep.name, dep.version);
	}

	Target* GitDepResolver::ResolveGitDependency(const Target& target, const TargetDependency& dep, std::string_view url, std::string_view branch)
	{		
		auto cached_dir = fmt::format("git.{}.{}@{}", dep.ns, dep.name, branch);

		// Hack lol
		for (auto& c : cached_dir)
			if (c == '/' || c == ':')
				c = '_';

		auto [scope, context] = target.GetBuildVarScope();

		auto re_arch = scope.ResolveLocal("arch");
		auto re_platform = scope.ResolveLocal("platform");
		auto re_config = scope.ResolveLocal("configuration");

		auto triplet = fmt::format("{}-{}-{}", re_arch, re_platform, re_config);

		auto cache_path = cached_dir + "-" + triplet;

		if (auto& cached = mTargetCache[cache_path])
			return cached.get();

		auto cache = ".re-cache";
		auto git_cached = target.root_path / cache / cached_dir;

		fs::create_directories(git_cached);

		auto dep_str = dep.ToString();

		if (!fs::exists(git_cached / ".git"))
		{
			if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
				RE_THROW TargetUncachedDependencyException(&target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep_str);

			mOut->Info(
				fmt::emphasis::bold | fg(fmt::color::light_blue),
				"[{}] Restoring package {}...\n",
				target.module,
				dep_str
			);

			auto start_time = std::chrono::high_resolution_clock::now();

			fs::remove(git_cached);
			DownloadGitDependency(url, branch, git_cached);

			auto end_time = std::chrono::high_resolution_clock::now();

			mOut->Info(
				fmt::emphasis::bold | fg(fmt::color::light_blue),
				"\n[{}] Restored package {} ({:.2f}s)\n",
				target.module,
				dep_str,
				std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f
			);
		}
		else
		{
			mOut->Info(
				fmt::emphasis::bold | fg(fmt::color::light_blue),
				"[{}] Package {} already available\n",
				target.module,
				dep_str
			);
		}

		auto& result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(git_cached));

		result->root_path = target.root_path;

		result->config["arch"] = re_arch;
		result->config["platform"] = re_platform;
		result->config["configuration"] = re_config;

		result->var_parent = target.var_parent;
		result->local_var_ctx = context;
		result->build_var_scope.emplace(&result->local_var_ctx, "build", &scope);

		result->module = fmt::format("git.{}.{}", triplet, result->module);

		result->LoadDependencies();
		result->LoadMiscConfig();
		result->LoadSourceTree();

		result->resolved_config = GetResolvedTargetCfg(*result, {
			{ "arch", re_arch },
			{ "platform", re_platform },
			{ "config", re_config }
		});

		result->LoadConditionalDependencies();

		mLoader->RegisterLocalTarget(result.get());
		return result.get();
	}

	void GitDepResolver::DownloadGitDependency(std::string_view url, std::string_view branch, const fs::path& to)
	{
		std::vector<std::string> cmdline = {
			"git", "clone",
			"--depth", "1"
		};

		if (!branch.empty())
		{
			cmdline.emplace_back("--branch");
			cmdline.emplace_back(branch.data());
		}

		cmdline.emplace_back(url.data());
		cmdline.emplace_back(to.u8string());

		RunProcessOrThrow(
			"git", cmdline,
			false,
			true
		);
	}
	
	bool GitDepResolver::SaveDependencyToPath(const TargetDependency& dep, const fs::path& path)
	{
        fs::create_directories(path);
		DownloadGitDependency(dep.name, dep.version, path);
		return true;
	}
}
