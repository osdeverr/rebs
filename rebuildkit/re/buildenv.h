#pragma once
#include "target.h"

#include <string>
#include <unordered_map>
#include <vector>

#include <unordered_set>

namespace re
{
	class BuildEnv : public ILangLocator
	{
	public:
		Target& LoadRootTarget(const std::string& path);

		std::vector<Target*> GetTargetsInDependencyOrder();

		void AddLangProvider(std::string_view name, ILangProvider* provider);
		ILangProvider* GetLangProvider(std::string_view name) override;

	private:
		std::vector<Target> mRootTargets;
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
