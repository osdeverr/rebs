#include "fs_dep_resolver.h"

#include <re/target_cfg_utils.h>

namespace re
{
	Target* FsDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		auto [scope, context] = target.GetBuildVarScope();

		auto re_arch = scope.ResolveLocal("arch");
		auto re_platform = scope.ResolveLocal("platform");
		auto re_config = scope.ResolveLocal("configuration");

		auto triplet = fmt::format("-{}-{}-{}", re_arch, re_platform, re_config);

		auto cache_path = dep.name + triplet;

		std::string cutout_filter = "";

		fs::path path = dep.name;

		if (dep.filters.size() >= 1 && dep.filters[0].front() == '/')
		{
			cutout_filter = dep.filters[0].substr(1);
			cache_path += cutout_filter;
			path /= cutout_filter;
		}

		if (auto& cached = mTargetCache[cache_path])
			return cached.get();

		auto& result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(path));

		result->root_path = target.root_path;

		result->config["arch"] = re_arch;
		result->config["platform"] = re_platform;
		result->config["configuration"] = re_config;

		result->var_parent = target.var_parent;
		result->local_var_ctx = context;
		result->build_var_scope.emplace(&result->local_var_ctx, "build", &scope);

		result->module += triplet;

		result->LoadDependencies();
		result->LoadMiscConfig();
		result->LoadSourceTree();

		mLoader->RegisterLocalTarget(result.get());
		return result.get();
	}

	bool FsDepResolver::SaveDependencyToPath(const TargetDependency& dep, const fs::path& path)
	{
        fs::create_directories(path);

		fs::path dep_path = dep.name;
		fs::copy(dep_path, path, fs::copy_options::recursive | fs::copy_options::overwrite_existing);

		return true;
	}
}
