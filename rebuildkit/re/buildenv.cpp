#include "buildenv.h"

namespace re
{
	void PopulateTargetDependencySet(Target* pTarget, std::vector<Target*>& to, std::function<Target* (const TargetDependency&)> dep_resolver)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto& dep : pTarget->dependencies)
		{
			dep.resolved = dep_resolver(dep);

			if (!dep.resolved)
				throw TargetLoadException("unresolved dependency " + dep.name + " in target " + pTarget->module);
		}

		to.push_back(pTarget);

		for (auto& child : pTarget->children)
			PopulateTargetDependencySet(&child, to, dep_resolver);
	}

	void PopulateTargetDependencySetNoResolve(const Target* pTarget, std::vector<const Target*>& to)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto& dep : pTarget->dependencies)
		{
			if (dep.resolved)
				PopulateTargetDependencySetNoResolve(dep.resolved, to);
			else
				throw TargetLoadException("unresolved dependency " + dep.name + " in target " + pTarget->module);
		}

		to.push_back(pTarget);

		for (auto& child : pTarget->children)
			PopulateTargetDependencySetNoResolve(&child, to);
	}

	Target& BuildEnv::LoadRootTarget(const std::string& path)
	{
		auto& target = mRootTargets.emplace_back(Target{ path });

		target.lang_locator = this;

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

	NinjaBuildDesc BuildEnv::GenerateBuildDesc()
	{
		NinjaBuildDesc desc;

		for (auto& [name, provider] : mLangProviders)
			provider->InitInBuildDesc(desc);

		for (auto& target : GetTargetsInDependencyOrder())
		{
			for (auto& provider : target->lang_providers)
			{
				if (provider->InitBuildTarget(desc, *target))
				{
					for (auto& source : target->sources)
						provider->ProcessSourceFile(desc, *target, source);

					provider->CreateTargetArtifact(desc, *target);
				}
			}
		}

		return desc;
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
		PopulateTargetDependencySet(pTarget, to, [this, &to](const TargetDependency& dep) -> Target*
		{
			for (auto& root : mRootTargets)
			{
				if (auto target = GetTargetOrNull(ModulePathCombine(root.module, dep.name)))
				{
					AppendDepsAndSelf(target, to);
					return target;
				}
			}

			return nullptr;
		});
	}
}
