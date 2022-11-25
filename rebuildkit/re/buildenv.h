#pragma once
#include "target.h"
#include "build_desc.h"
#include "dep_resolver.h"
#include "target_loader.h"

#include <re/vars.h>

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include <unordered_set>

namespace re
{
	void PopulateTargetDependencySet(Target* pTarget, std::vector<Target*>& to, std::function<Target* (const Target&, const TargetDependency&)> dep_resolver = {}, bool throw_on_missing = true);
	void PopulateTargetDependencySetNoResolve(const Target* pTarget, std::vector<const Target*>& to);

	class BuildEnv : public ILangLocator, public IDepResolver, public ITargetLoader
	{
	public:
		BuildEnv(LocalVarScope& scope);

		Target& LoadCoreProjectTarget(const fs::path& path);

		Target* GetCoreTarget();

		std::unique_ptr<Target> LoadFreeTarget(const fs::path& path);
		Target& LoadTarget(const fs::path& path);
		void RegisterLocalTarget(Target* pTarget);

		std::vector<Target*> GetSingleTargetDepSet(Target* pTarget);
		std::vector<Target*> GetSingleTargetLocalDepSet(Target* pTarget);
		std::vector<Target*> GetTargetsInDependencyOrder();

		void AddLangProvider(std::string_view name, ILangProvider* provider);
		ILangProvider* GetLangProvider(std::string_view name) override;

		ILangProvider* InitializeTargetLinkEnv(Target* target, NinjaBuildDesc& desc);
		void InitializeTargetLinkEnvWithDeps(Target* target, NinjaBuildDesc& desc);

		void PopulateBuildDesc(Target* target, NinjaBuildDesc& desc);
		void PopulateBuildDescWithDeps(Target* target, NinjaBuildDesc& desc);
		void PopulateFullBuildDesc(NinjaBuildDesc& desc);

		void RunTargetAction(const NinjaBuildDesc* desc, const Target& target, const std::string& type, const TargetConfig& data);

		void RunActionsCategorized(Target* target, const NinjaBuildDesc* desc, std::string_view run_type);

		void RunPostBuildActions(Target* target, const NinjaBuildDesc& desc);
		void RunInstallActions(Target* target, const NinjaBuildDesc& desc);

		void AddDepResolver(std::string_view name, IDepResolver* resolver);
		Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep) override;

	private:
		std::unique_ptr<Target> mTheCoreProjectTarget;

		std::vector<std::unique_ptr<Target>> mRootTargets;
		std::unordered_map<std::string, Target*> mTargetMap;

		std::unordered_map<std::string, ILangProvider*> mLangProviders;
		std::unordered_map<std::string, IDepResolver*> mDepResolvers;

		LocalVarScope mVars;

		inline Target* GetTargetOrNull(const std::string& module)
		{
			auto it = mTargetMap.find(module);

			if (it != mTargetMap.end())
				return it->second;
			else
				return nullptr;
		}

		void PopulateTargetMap(Target* pTarget);

		void AppendDepsAndSelf(Target *pTarget, std::vector<Target *> &to, bool throw_on_missing = true, bool use_external = true);

		void RunActionList(const NinjaBuildDesc* desc, Target *target, const TargetConfig &list, std::string_view run_type, const std::string &default_run_type);

		void InstallPathToTarget(const Target *pTarget, const fs::path &from);

		Target* ResolveTargetDependencyImpl(const Target& target, const TargetDependency& dep, bool use_external = true);
	};
}
