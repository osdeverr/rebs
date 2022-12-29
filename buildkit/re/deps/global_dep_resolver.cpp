#include "global_dep_resolver.h"
#include <re/target_cfg_utils.h>

#include <fstream>

namespace re
{    
    Target* GlobalDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
    {
        std::string tag = dep.version;

        auto package_path = mPackagesPath / dep.name;
        
        if (!fs::exists(package_path))
            RE_THROW TargetDependencyException(&target, "Missing global package '{}'", dep.name);

        if(tag.empty())
        {
            // Fetch the default version from the config file
            std::ifstream file{package_path / "default-tag.txt"};
            file >> tag;
        }

        auto target_path = package_path / tag;
            
        if (!fs::exists(target_path))
            RE_THROW TargetDependencyException(&target, "Missing version '{}' for global package '{}'", tag, dep.name);
            
		auto [scope, context] = target.GetBuildVarScope();

		auto re_arch = scope.ResolveLocal("arch");
		auto re_platform = scope.ResolveLocal("platform");
		auto re_config = scope.ResolveLocal("configuration");

		auto triplet = fmt::format("{}-{}-{}", re_arch, re_platform, re_config);
        
		auto cache_path = dep.ToString() + "-" + triplet;

		if (auto& cached = mTargetCache[cache_path])
			return cached.get();

        // fmt::print("DEBUG: Loading global stuff '{}'\n", target_path.u8string());
        
		auto& result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(target_path));

		result->root_path = target.root_path;

		result->config["arch"] = re_arch;
		result->config["platform"] = re_platform;
		result->config["configuration"] = re_config;

		result->var_parent = target.var_parent;
		result->local_var_ctx = context;
		result->build_var_scope.emplace(&result->local_var_ctx, "build", &scope);

		result->module = fmt::format("global.{}.{}", triplet, result->module);

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
        
		mOut->Info(
			fmt::emphasis::bold | fg(fmt::color::yellow),
			"[{}] Using installed package {}\n",
			target.module,
			dep.ToString()
		);

		return result.get();
    }

    void GlobalDepResolver::InstallGlobalPackage(const TargetDependency& dep, const TargetDependency& as)
    {
        std::string tag = as.version.empty() ? dep.version : as.version;

        if (tag.empty())
            tag = "default";

        auto resolver = mLoader->GetDepResolver(dep.ns);

        if (!resolver)
            RE_THROW TargetDependencyException(nullptr, "Unknown dependency type '{}'", dep.ns);

        auto target_path = mPackagesPath / as.name / tag;

        auto result_dep = fmt::format("global:{}@{}", as.name, tag);

		mOut->Info(
			fmt::emphasis::bold | fg(fmt::color::yellow),
			"Installing package {} as {}...\n",
			dep.ToString(), result_dep
		);

        if (fs::exists(target_path))
        {
		    mOut->Info(
		    	fmt::emphasis::bold | fg(fmt::color::yellow),
		    	"! Will remove existing package {}\n",
		    	result_dep
		    );

            fs::remove_all(target_path);
        }

        if (!resolver->SaveDependencyToPath(dep, target_path))
            RE_THROW TargetDependencyException(nullptr, "Dependency type '{}' is not supported for global packages", dep.ns);

		mOut->Info(
			fmt::emphasis::bold | fg(fmt::color::yellow),
			" ! Installed package {} as {}\n",
			dep.ToString(), result_dep
		);
        
        std::ofstream file{mPackagesPath / as.name / "default-tag.txt"};
        file << tag;
    }
    
    void GlobalDepResolver::SelectGlobalPackageTag(const TargetDependency& dep, const std::string& new_tag)
    {
        auto package_path = mPackagesPath / dep.name;
        
        if (!fs::exists(package_path))
            RE_THROW TargetDependencyException(nullptr, "Missing global package '{}'", dep.name);

        auto target_path = package_path / new_tag;
            
        if (!fs::exists(target_path))
            RE_THROW TargetDependencyException(nullptr, "Missing version '{}' for global package '{}'", new_tag, dep.name);
            
        std::ofstream file{mPackagesPath / dep.name / "default-tag.txt"};
        file << new_tag;

		mOut->Info(
			fmt::emphasis::bold | fg(fmt::color::yellow),
			" ! Selected tag {} as default for package {}\n",
			new_tag, dep.ToString()
		);
    }
    
    std::vector<std::pair<std::string, bool>> GlobalDepResolver::GetGlobalPackageInfo(const TargetDependency& dep)
    {
        std::vector<std::pair<std::string, bool>> result;

        auto package_path = mPackagesPath / dep.name;
        
        if (!fs::exists(package_path))
            RE_THROW TargetDependencyException(nullptr, "Missing global package '{}'", dep.name);

        std::string default_tag;

        // Fetch the default version from the config file
        std::ifstream file{package_path / "default-tag.txt"};
        file >> default_tag;

        for(auto& entry : fs::directory_iterator{package_path})
        {
            if (entry.is_directory() && fs::exists(entry.path() / "re.yml"))
            {
                auto name = entry.path().filename().u8string();
                result.push_back(std::make_pair(name, name == default_tag));
            }
        }

        return result;
    }
    
    std::vector<std::pair<std::string, std::string>> GlobalDepResolver::GetGlobalPackageList()
    {
        return {};
    }
}
