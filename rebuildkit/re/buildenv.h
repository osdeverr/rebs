#pragma once
#include "target.h"
#include "build_desc.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include <unordered_set>

namespace re
{
	void PopulateTargetDependencySet(Target* pTarget, std::vector<Target*>& to, std::function<Target* (const TargetDependency&)> dep_resolver = {});
	void PopulateTargetDependencySetNoResolve(const Target* pTarget, std::vector<const Target*>& to);

	class BuildEnv : public ILangLocator
	{
	public:
		Target& LoadCoreProjectTarget(const std::string& path);

		Target& LoadTarget(const std::string& path);

		std::vector<Target*> GetTargetsInDependencyOrder();

		void AddLangProvider(std::string_view name, ILangProvider* provider);
		ILangProvider* GetLangProvider(std::string_view name) override;

		NinjaBuildDesc GenerateBuildDesc();

	private:
		std::unique_ptr<Target> mTheCoreProjectTarget;

		std::vector<std::unique_ptr<Target>> mRootTargets;
		std::unordered_map<std::string, Target*> mTargetMap;

		std::unordered_map<std::string, ILangProvider*> mLangProviders;

		inline Target* GetTargetOrNull(const std::string& module)
		{
			auto it = mTargetMap.find(module);

			if (it != mTargetMap.end())
				return it->second;
			else
				return nullptr;
		}

		void PopulateTargetMap(Target* pTarget);

		void AppendDepsAndSelf(Target* pTarget, std::vector<Target*>& to);
	};
}
