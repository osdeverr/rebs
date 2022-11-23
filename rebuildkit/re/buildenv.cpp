#include "buildenv.h"

#include <fmt/format.h>

#include <re/error.h>

namespace re
{
	void PopulateTargetDependencySet(Target *pTarget, std::vector<Target *> &to, std::function<Target *(const TargetDependency &)> dep_resolver)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto &child : pTarget->children)
			PopulateTargetDependencySet(child.get(), to, dep_resolver);

		for (auto &dep : pTarget->dependencies)
		{
			dep.resolved = dep_resolver(dep);

			if (!dep.resolved)
				RE_THROW TargetDependencyException(pTarget, "unresolved dependency {}", dep.name);

			dep.resolved->dependents.insert(pTarget);
		}

		to.push_back(pTarget);
	}

	void PopulateTargetDependencySetNoResolve(const Target *pTarget, std::vector<const Target *> &to)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		for (auto &child : pTarget->children)
			PopulateTargetDependencySetNoResolve(child.get(), to);

		for (auto &dep : pTarget->dependencies)
		{
			if (dep.resolved)
				PopulateTargetDependencySetNoResolve(dep.resolved, to);
			else
				RE_THROW TargetDependencyException(pTarget, "unresolved dependency {}", dep.name);
		}

		to.push_back(pTarget);
	}

	BuildEnv::BuildEnv(LocalVarScope& scope)
		: mVars{ &scope, "build" }
	{
		mVars.SetVar("platform", "${env:RE_PLATFORM | $re:platform-string}");
		mVars.SetVar("platform-closest", "${build:platform}");
		mVars.SetVar("arch", "${build:platform}");
	}

	std::unique_ptr<Target> BuildEnv::LoadFreeTarget(const fs::path& path)
	{
		auto target = std::make_unique<Target>(path, mTheCoreProjectTarget.get());

		target->LoadDependencies();
		target->LoadMiscConfig();
		target->LoadSourceTree();

		// mTargetMap.clear();
		// PopulateTargetMap(target.get());

		return target;
	}

	Target& BuildEnv::LoadTarget(const fs::path& path)
	{
		auto target = LoadFreeTarget(path);

		// mTargetMap.clear();
		PopulateTargetMap(target.get());

		auto& moved = mRootTargets.emplace_back(std::move(target));
		return *moved.get();
	}

	void BuildEnv::RegisterLocalTarget(Target* pTarget)
	{
		PopulateTargetMap(pTarget);
	}

	Target& BuildEnv::LoadCoreProjectTarget(const fs::path& path)
	{
		mTheCoreProjectTarget = std::make_unique<Target>(path);
		return *mTheCoreProjectTarget;
	}

	Target* BuildEnv::GetCoreTarget()
	{
		return mTheCoreProjectTarget.get();
	}

	std::vector<Target*> BuildEnv::GetSingleTargetDepSet(Target* pTarget)
	{
		std::vector<Target*> result;
		AppendDepsAndSelf(pTarget, result);
		return result;
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

	void BuildEnv::PopulateBuildDesc(Target* target, NinjaBuildDesc& desc)
	{
		auto langs = target->GetCfgEntry<TargetConfig>("langs", CfgEntryKind::Recursive).value_or(TargetConfig{ YAML::NodeType::Sequence });

		for (const auto& lang : langs)
		{
			auto lang_id = lang.as<std::string>();

			auto provider = GetLangProvider(lang_id);
			if (!provider)
				RE_THROW TargetLoadException(target, "unknown language {}", lang_id);

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
				RE_THROW TargetLoadException(target, "unknown link-with language {}", *link_language);
		}
	}

	void BuildEnv::PopulateBuildDescWithDeps(Target* target, NinjaBuildDesc& desc)
	{
		for (auto& dep : GetSingleTargetDepSet(target))
			PopulateBuildDesc(dep, desc);
	}

	void BuildEnv::PopulateFullBuildDesc(NinjaBuildDesc& desc)
	{
		//auto re_arch = std::getenv("RE_ARCH");
		//auto re_platform = std::getenv("RE_PLATFORM");

		/*
		desc.vars["re_build_platform"] = mVars.Substitute("${platform}");
		desc.vars["re_build_platform_closest"] = mVars.Substitute("${platform-closest}");
		desc.vars["re_build_arch"] = mVars.Substitute("${arch}");
		*/

		for (auto& [name, provider] : mLangProviders)
			provider->InitInBuildDesc(desc);

	}

	void BuildEnv::RunTargetAction(const NinjaBuildDesc& desc, const Target& target, const std::string& type, const TargetConfig& data)
	{
		if (type == "copy")
		{
			auto from = data["from"].as<std::string>();
			auto to = data["to"].as<std::string>();

			std::filesystem::copy(
				target.path / from,
				desc.out_dir / desc.GetArtifactDirectory(target.module) / to,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing
			);
		}
		else if (type == "copy-to-deps")
		{
			auto from = data["from"].as<std::string>();
			auto to = data["to"].as<std::string>();

			for (auto &dependent : target.dependents)
			{
				auto to_dep = desc.out_dir / desc.GetArtifactDirectory(dependent->module);

				if (std::filesystem::exists(to_dep))
					std::filesystem::copy(
						target.path / from,
						to_dep / to,
						std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing
					);
			}
		}
	}

	void BuildEnv::RunPostBuildActions(const NinjaBuildDesc& desc)
	{
		RunActionsCategorized(desc, "post-build");
	}

	void BuildEnv::RunInstallActions(const NinjaBuildDesc &desc)
	{
		for (auto &target : GetTargetsInDependencyOrder())
		{
			auto from = desc.out_dir / desc.GetArtifactDirectory(target->module);
			InstallPathToTarget(target, from);
		}

		RunActionsCategorized(desc, "post-install");
	}

	void BuildEnv::InstallPathToTarget(const Target *pTarget, const fs::path& from)
	{
		if (auto path = pTarget->GetCfgEntry<TargetConfig>("install", CfgEntryKind::Recursive))
		{
			auto path_str = path->as<std::string>();

			fmt::print("Installing {} - {} => {}\n", pTarget->module, from.u8string(), path_str);

			if(std::filesystem::exists(from))
			{
				std::filesystem::copy(
					from,
					path_str,
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing
				);
			}
		}
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
			RE_THROW TargetLoadException(&target, "dependency '{}': unknown target namespace '{}'", dep.ToString(), dep.ns);
	}

	void BuildEnv::PopulateTargetMap(Target* pTarget)
	{
		// fmt::print(" [DBG] Adding to target map: '{}'\n", pTarget->module);

		if (mTargetMap[pTarget->module] != nullptr)
			RE_THROW TargetLoadException(pTarget, "target defined more than once");

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
				target->dependents.insert(pTarget);

				AppendDepsAndSelf(target, to);
				return target;
			}

			return nullptr;
		});
	}

	void BuildEnv::RunActionList(const NinjaBuildDesc &desc, Target *target, const TargetConfig &list, std::string_view run_type, const std::string &default_run_type)
	{
		for (const auto &v : list)
		{
			for (const auto &kv : v)
			{
				auto type = kv.first.as<std::string>();
				auto &data = kv.second;

				std::string run = default_run_type;
				// fmt::print("{}\n", type);

				if (auto run_val = data["run"])
					run = run_val.as<std::string>();

				if (run == run_type)
					RunTargetAction(desc, *target, type, data);
			}
		}
	}

	void BuildEnv::RunActionsCategorized(const NinjaBuildDesc& desc, std::string_view run_type)
	{
		for (auto& target : GetTargetsInDependencyOrder())
		{
			if (auto actions = target->GetCfgEntry<TargetConfig>("actions"))
			{
				if (actions->IsMap())
				{
					for (const auto &kv : *actions)
					{
						auto type = kv.first.as<std::string>();
						auto &data = kv.second;

						RunActionList(desc, target, data, run_type, type);
					}
				}
				else
				{
					RunActionList(desc, target, *actions, run_type, "post-build");
				}
			}
		}
	}
}
