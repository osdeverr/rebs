#include "arch_coerced_dep_resolver.h"

#include <re/target_cfg_utils.h>

namespace re
{
	Target* ArchCoercedDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep, DepsVersionCache* cache)
	{
		RE_THROW TargetDependencyException(&target, "ArchCoercedDepResolver does not support resolving literal dependencies.");
	}

	Target* ArchCoercedDepResolver::ResolveCoercedTargetDependency(const Target& target, const Target& dep)
	{
		auto [scope, context] = target.GetBuildVarScope();

		auto re_arch = scope.ResolveLocal("arch");
		auto re_platform = scope.ResolveLocal("platform");
		auto re_config = scope.ResolveLocal("configuration");

		auto cache_path = fmt::format("arch-coerced.{}.{}", re_arch, dep.module);

		if (auto& cached = mTargetCache[cache_path])
			return cached.get();

		auto& result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(dep.path, &target));

		result->root_path = target.root_path;

		result->config["arch"] = re_arch;
		result->config["platform"] = re_platform;
		result->config["configuration"] = re_config;

		result->var_parent = target.var_parent;
		result->local_var_ctx = context;
		result->build_var_scope.emplace(&result->local_var_ctx, "build", &scope);

		result->module = cache_path;

		result->dep_parent = &target;

		result->LoadDependencies();
		result->LoadMiscConfig();
		result->LoadSourceTree();

		mLoader->RegisterLocalTarget(result.get());
		return result.get();
	}
}
