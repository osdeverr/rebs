#include "cmake_lang_provider.h"

#include <re/target.h>
#include <re/target_cfg_utils.h>

namespace re
{
    CMakeLangProvider::CMakeLangProvider(LocalVarScope* var_scope)
    : mVarScope{var_scope}
    {}

	void CMakeLangProvider::InitInBuildDesc(NinjaBuildDesc& desc)
    {
        // Do nothing
    }

	void CMakeLangProvider::InitLinkTargetEnv(NinjaBuildDesc& desc, Target& target)
    {
        // Init the var scopes
		target.local_var_ctx = *mVarScope->GetContext();

		auto& target_vars = target.target_var_scope.emplace(&target.local_var_ctx, "target", &target);
		auto& vars = target.build_var_scope.emplace(&target.local_var_ctx, "build", &target_vars);

        vars.SetVar("arch", target.config["arch"].Scalar());
        vars.SetVar("configuration", target.config["configuration"].Scalar());
        vars.SetVar("platform", target.config["platform"].Scalar());
        
		std::unordered_map<std::string, std::string> configuration = {
			{ "arch", vars.ResolveLocal("arch") },
			{ "platform", vars.ResolveLocal("platform") },
			{ "config", vars.ResolveLocal("configuration") }
		};

		target.resolved_config = GetResolvedTargetCfg(target, configuration);
    }

	bool CMakeLangProvider::InitBuildTargetRules(NinjaBuildDesc& desc, const Target& target)
    {
        if (auto build_script = target.resolved_config["cmake-out-build-script"])
        {
            if (std::find(desc.subninjas.begin(), desc.subninjas.end(), build_script.Scalar()) == desc.subninjas.end())
                desc.subninjas.push_back(build_script.Scalar());
        }

        if (auto cmake_meta = target.resolved_config["cmake-meta"]["targets"][target.name])
            if (auto location = cmake_meta["location"])
		        desc.artifacts[&target] = location.Scalar();

        return true;
    }

	void CMakeLangProvider::ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file)
    {
        // CMake targets have no source files
    }

	void CMakeLangProvider::CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target)
    {
        // CMake targets are built through custom artifacts
    }
}
