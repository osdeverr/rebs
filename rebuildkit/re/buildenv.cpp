#include "buildenv.h"

namespace re
{
	Target& BuildEnv::LoadRootTarget(const std::string& path)
	{
		auto& target = mRootTargets.emplace_back(Target{ path });

		target.lang_locator = this;

		target.lang_providers.push_back(GetLangProvider("c"));
		target.lang_providers.push_back(GetLangProvider("cpp"));

		target.LoadDependencies();
		target.LoadMiscConfig();
		target.LoadSourceTree();

		// mTargetMap.clear();
		PopulateTargetMap(&target);
		return target;
	}

	std::vector<Target*> BuildEnv::GetTargetsInDependencyOrder()
	{
		std::vector<Target*> result;

		for (auto& target : mRootTargets)
			AppendDepsAndSelf(&target, result);

		return result;
	}

	void BuildEnv::AddLangProvider(std::string_view name, ILangProvider* provider)
	{
		mLangProviders[name.data()] = provider;
	}

	ILangProvider* BuildEnv::GetLangProvider(std::string_view name)
	{
		return mLangProviders[name.data()];
	}

	void BuildEnv::PopulateTargetMap(Target* pTarget)
	{
		if (mTargetMap[pTarget->module] != nullptr)
			throw TargetLoadException("target " + pTarget->module + " defined more than once");

		mTargetMap[pTarget->module] = pTarget;

		for (auto& child : pTarget->children)
			PopulateTargetMap(&child);
	}

	void BuildEnv::AppendDepsAndSelf(Target* pTarget, std::vector<Target*>& to)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto& dep : pTarget->dependencies)
		{
			for (auto& root : mRootTargets)
			{
				if (auto target = GetTargetOrNull(ModulePathCombine(root.module, dep.name)))
				{
					AppendDepsAndSelf(target, to);
					break;
				}
			}
		}

		to.push_back(pTarget);

		for (auto& child : pTarget->children)
			AppendDepsAndSelf(&child, to);
	}
}
