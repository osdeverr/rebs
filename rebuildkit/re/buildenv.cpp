#include "buildenv.h"

#include <fmt/format.h>

namespace re
{
	void PopulateTargetDependencySet(Target* pTarget, std::vector<Target*>& to, std::function<Target* (const TargetDependency&)> dep_resolver)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto& child : pTarget->children)
			PopulateTargetDependencySet(child.get(), to, dep_resolver);

		for (auto& dep : pTarget->dependencies)
		{
			dep.resolved = dep_resolver(dep);

			if (!dep.resolved)
				throw TargetLoadException("unresolved dependency " + dep.name + " in target " + pTarget->module);
		}

		to.push_back(pTarget);

		for (auto& child : pTarget->children)
			PopulateTargetDependencySet(child.get(), to, dep_resolver);
	}

	void PopulateTargetDependencySetNoResolve(const Target* pTarget, std::vector<const Target*>& to)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto& child : pTarget->children)
			PopulateTargetDependencySetNoResolve(child.get(), to);

		for (auto& dep : pTarget->dependencies)
		{
			if (dep.resolved)
				PopulateTargetDependencySetNoResolve(dep.resolved, to);
			else
				throw TargetLoadException("unresolved dependency " + dep.name + " in target " + pTarget->module);
		}

		to.push_back(pTarget);

		for (auto& child : pTarget->children)
			PopulateTargetDependencySetNoResolve(child.get(), to);
	}

	Target& BuildEnv::LoadTarget(const std::string& path)
	{
		auto target = std::make_unique<Target>(path, mTheCoreProjectTarget.get());

		target->LoadDependencies();
		target->LoadMiscConfig();
		target->LoadSourceTree();

		// mTargetMap.clear();
		PopulateTargetMap(target.get());

		auto& moved = mRootTargets.emplace_back(std::move(target));
		return *moved.get();
	}

	Target& BuildEnv::LoadCoreProjectTarget(const std::string& path)
	{
		mTheCoreProjectTarget = std::make_unique<Target>(path);
		return *mTheCoreProjectTarget;
	}

	std::vector<Target*> BuildEnv::GetTargetsInDependencyOrder()
	{
		std::vector<Target*> result;

		for (auto& target : mRootTargets)
			AppendDepsAndSelf(target.get(), result);

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

		desc.vars["re_build_platform"] = "windows";
		desc.vars["re_build_platform_closest"] = "windows";
		desc.vars["re_build_arch"] = "x64";

		for (auto& [name, provider] : mLangProviders)
			provider->InitInBuildDesc(desc);

		for (auto& target : GetTargetsInDependencyOrder())
		{
			fmt::print(" [DBG] Generating build desc for target '{}'\n", target->module);

			auto langs = target->GetCfgEntry<TargetConfig>("langs", CfgEntryKind::Recursive).value_or(TargetConfig{YAML::NodeType::Sequence});

			for (const auto& lang : langs)
			{
				auto lang_id = lang.as<std::string>();

				auto provider = GetLangProvider(lang_id);
				if (!provider)
					throw TargetLoadException("unknown language " + lang_id + " in target " + target->module);

				if (provider->InitBuildTarget(desc, *target))
				{
					for (auto& source : target->sources)
						provider->ProcessSourceFile(desc, *target, source);
				}
			}

			auto link_cfg = target->GetCfgEntry<TargetConfig>("link-with", re::CfgEntryKind::Recursive).value_or(YAML::Node{ YAML::NodeType::Null });
			std::optional<std::string> link_language;

			if (link_cfg.IsMap())
			{
				if (auto value = link_cfg[TargetTypeToString(target->type)])
				{
					if (!value.IsNull())
						link_language = value.as<std::string>();
				}
				else if (auto value = link_cfg["default"])
				{
					if (!value.IsNull())
						link_language = value.as<std::string>();
				}
			}
			else if (!link_cfg.IsNull())
			{
				link_language = link_cfg.as<std::string>();
			}

			if (link_language)
			{
				if (auto link_provider = GetLangProvider(*link_language))
					link_provider->CreateTargetArtifact(desc, *target);
				else
					throw TargetLoadException("unknown link-with language " + *link_language + " in target " + target->module);
			}
		}

		return desc;
	}

	void BuildEnv::AddDepResolver(std::string_view name, IDepResolver* resolver)
	{
		mDepResolvers[name.data()] = resolver;
	}

	Target* BuildEnv::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		if (dep.ns.empty() || dep.ns == "local")
			return GetTargetOrNull(dep.name);

		if (auto resolver = mDepResolvers[dep.ns])
			return resolver->ResolveTargetDependency(target, dep);
		else
			throw TargetLoadException("unknown target namespace " + dep.ns);
	}

	void BuildEnv::PopulateTargetMap(Target* pTarget)
	{
		if (mTargetMap[pTarget->module] != nullptr)
			throw TargetLoadException("target " + pTarget->module + " defined more than once");

		// fmt::print(" [DBG] Adding to target map: '{}'\n", pTarget->module);
		mTargetMap[pTarget->module] = pTarget;

		for (auto& child : pTarget->children)
			PopulateTargetMap(child.get());
	}

	void BuildEnv::AppendDepsAndSelf(Target* pTarget, std::vector<Target*>& to)
	{
		PopulateTargetDependencySet(pTarget, to, [this, &to, pTarget](const TargetDependency& dep) -> Target*
		{
			if (auto target = ResolveTargetDependency(*pTarget, dep))
			{
				AppendDepsAndSelf(target, to);
				return target;
			}

			return nullptr;
		});
	}
}
