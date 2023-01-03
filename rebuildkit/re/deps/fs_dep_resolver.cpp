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

		if (auto& cached = mTargetCache[cache_path])
			return cached.get();

		auto& result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(dep.name));

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

		result->resolved_config = GetResolvedTargetCfg(*result, {
			{ "arch", re_arch },
			{ "platform", re_platform },
			{ "config", re_config }
		});

		result->LoadConditionalDependencies();

		mLoader->RegisterLocalTarget(result.get());
		return result.get();
	}
}