#include "buildenv.h"

#include <fmt/format.h>

#include <re/error.h>
#include <re/process_util.h>
#include <re/debug.h>

#include <boost/algorithm/string.hpp>

namespace re
{
	void PopulateTargetDependencySet(Target *pTarget, std::vector<Target *> &to, std::function<Target *(const Target&, const TargetDependency &)> dep_resolver, bool throw_on_missing)
	{
		if (std::find(to.begin(), to.end(), pTarget) != to.end())
			return;

		if (pTarget->resolved_config && !pTarget->resolved_config["enabled"].as<bool>())
		{
			RE_TRACE(" PopulateTargetDependencySet: Skipping '{}' because it's not enabled\n", pTarget->module);
		}

		for (auto& [name, dep] : pTarget->used_mapping)
		{

			RE_TRACE(" PopulateTargetDependencySet: Attempting to resolve uses-mapping '{}' <- '{}'\n", pTarget->module, dep->ToString());
			dep->resolved = dep_resolver(*pTarget, *dep);

			if (!dep->resolved)
			{
				RE_TRACE("     failed\n");

				if (throw_on_missing)
					RE_THROW TargetDependencyException(pTarget, "unresolved uses-map dependency {}", dep->name);
			}
		}

		for (auto &dep : pTarget->dependencies)
		{
			RE_TRACE(" PopulateTargetDependencySet: Attempting to resolve '{}' <- '{}'\n", pTarget->module, dep.ToString());
			dep.resolved = dep_resolver(*pTarget, dep);

			if (!dep.resolved)
			{
				RE_TRACE("     failed\n");

				if (throw_on_missing)
					RE_THROW TargetDependencyException(pTarget, "unresolved dependency {}", dep.name);
			}
			else
			{
				PopulateTargetDependencySet(dep.resolved, to, dep_resolver, throw_on_missing);

				dep.resolved->dependents.insert(pTarget);
				RE_TRACE("     done\n");
			}
		}

		for (auto& child : pTarget->children)
			PopulateTargetDependencySet(child.get(), to, dep_resolver, throw_on_missing);

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
			RE_TRACE(" PopulateTargetDependencySetNoResolve - {} <- {} @ {}\n", pTarget->module, dep.ToString(), (const void*) &dep);

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

		// mTargetMap.clear();
		// PopulateTargetMap(target.get());

		return target;
	}

	Target& BuildEnv::LoadTarget(const fs::path& path)
	{
		auto target = LoadFreeTarget(path);
		target->root_path = path;

		target->LoadDependencies();
		target->LoadMiscConfig();
		target->LoadSourceTree();

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

	std::vector<Target*> BuildEnv::GetSingleTargetLocalDepSet(Target* pTarget)
	{
		std::vector<Target*> result;
		AppendDepsAndSelf(pTarget, result, false, false);
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

	ILangProvider* BuildEnv::InitializeTargetLinkEnv(Target* target, NinjaBuildDesc& desc)
	{
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

		ILangProvider* link_provider = link_language ? GetLangProvider(*link_language) : nullptr;

		if (link_language && !link_provider)
			RE_THROW TargetLoadException(target, "unknown link-with language {}", *link_language);

		if (link_provider)
			link_provider->InitLinkTargetEnv(desc, *target);

		return link_provider;
	}

	void BuildEnv::InitializeTargetLinkEnvWithDeps(Target* target, NinjaBuildDesc& desc)
	{
		std::vector<Target*> deps;
		AppendDepsAndSelf(target, deps, false, false);

		for (auto& dep : deps)
			InitializeTargetLinkEnv(dep, desc);
	}

	void BuildEnv::PopulateBuildDesc(Target* target, NinjaBuildDesc& desc)
	{
		auto langs = target->GetCfgEntry<TargetConfig>("langs", CfgEntryKind::Recursive).value_or(TargetConfig{ YAML::NodeType::Sequence });

		ILangProvider* link_provider = InitializeTargetLinkEnv(target, desc);

		if (target->resolved_config && !target->resolved_config["enabled"].as<bool>())
		{
			RE_TRACE(" PopulateBuildDesc: Skipping '{}' because it's not enabled\n", target->module);
			return;
		}

		for (const auto& lang : langs)
		{
			auto lang_id = lang.as<std::string>();

			auto provider = GetLangProvider(lang_id);
			if (!provider)
				RE_THROW TargetLoadException(target, "unknown language {}", lang_id);

			if (provider->InitBuildTargetRules(desc, *target))
			{
				for (auto& source : target->sources)
					provider->ProcessSourceFile(desc, *target, source);
			}
		}

		if (link_provider)
			link_provider->CreateTargetArtifact(desc, *target);
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

	void BuildEnv::PerformCopyToDependentsImpl(const Target& target, const Target* dependent, const NinjaBuildDesc* desc, const fs::path& from, const std::string& to)
	{
		auto path = GetEscapedModulePath(*dependent);

		RE_TRACE("    for dependent '{}':\n", dependent->module);

		if (desc->HasArtifactsFor(path))
		{
			auto to_dep = desc->out_dir / desc->GetArtifactDirectory(path);

			auto& from_path = from;
			fs::path to_path = to_dep / to;

			RE_TRACE("        copying from '{}' to '{}'\n", from_path.u8string(), to_path.u8string());

			if (std::filesystem::exists(to_dep))
			{
				std::filesystem::copy(
					from_path,
					to_path,
					std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing
				);

				RE_TRACE("            done\n");
			}
			else
			{
				RE_TRACE("            !!! no to_dep dir\n");
			}
		}
		else
		{
			RE_TRACE("        no artifacts\n");
		}

		for (auto& inner_dep : dependent->dependents)
			PerformCopyToDependentsImpl(target, inner_dep, desc, from, to);
	}

	void BuildEnv::RunTargetAction(const NinjaBuildDesc* desc, const Target& target, const std::string& type, const TargetConfig& data)
	{
		if (type == "copy")
		{
			auto from = data["from"].as<std::string>();
			auto to = data["to"].as<std::string>();

			std::filesystem::copy(
				target.path / from,
				desc->out_dir / desc->GetArtifactDirectory(GetEscapedModulePath(target)) / to,
				std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing
			);
		}
		else if (type == "copy-to-deps")
		{
			auto from = data["from"].as<std::string>();
			auto to = data["to"].as<std::string>();

			auto from_path = target.path / from;

			for (auto& dependent : target.dependents)
			{
				PerformCopyToDependentsImpl(target, dependent, desc, from_path, to);
			}
		}
		else if (type == "run")
		{
			auto command = data["command"].as<std::string>();

			std::vector<std::string> args{ command };

			for (auto& arg : data["args"])
				args.push_back(arg.Scalar());

			RunProcessOrThrow("command", args, true, true, target.path.u8string());
		}
	}

	void BuildEnv::RunPostBuildActions(Target* target, const NinjaBuildDesc& desc)
	{
		RunActionsCategorized(target, &desc, "post-build");
	}

	void BuildEnv::RunInstallActions(Target* target, const NinjaBuildDesc &desc)
	{
		auto from = desc.out_dir / desc.GetArtifactDirectory(target->module);
		InstallPathToTarget(target, from);

		RunActionsCategorized(target, &desc, "post-install");
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

	Target* BuildEnv::ResolveTargetDependencyImpl(const Target& target, const TargetDependency& dep, bool use_external)
	{
		if (dep.ns.empty() || dep.ns == "local")
		{
			auto result = GetTargetOrNull(dep.name);

			// Arch coercion - this is SOMETIMES very useful
			if (result)
			{
				if (target.build_var_scope && result->build_var_scope)
				{
					auto target_arch = target.build_var_scope->ResolveLocal("arch");
					auto dep_arch = result->build_var_scope->ResolveLocal("arch");

					if (target_arch != dep_arch)
					{
						if (use_external)
						{
							RE_TRACE(" *** Performing arch coercion: {}:{} <- {}:{}\n", target.module, target_arch, result->module, dep_arch);

							if (auto resolver = mDepResolvers["arch-coerced"])
								result = resolver->ResolveCoercedTargetDependency(target, *result);
							else
								RE_THROW TargetLoadException(
									&target,
									"dependency '{}': architecture mismatch (target:{} != dep:{}) without a multi-arch dep resolver",
									dep.ToString(),
									target_arch,
									dep_arch
								);
						}
						else
							result = nullptr;
					}
				}
			}

			return result;
		}

		if (use_external)
		{
			if (dep.ns == "uses")
			{
				auto used = target.GetUsedDependency(dep.name);

				if (!used)
					RE_THROW TargetDependencyException(&target, "uses-dependency '{}' not found", dep.ToString());

				auto result = used->resolved;

				if (!result)
					RE_THROW TargetDependencyException(&target, "unresolved uses-dependency '{}' <- '{}'", dep.ToString(), used->ToString());

				if (!dep.version.empty())
				{
					// RE_THROW TargetDependencyException(&target, "uses-dependency '{}' <- '{}' - partial target deps are not yet implemented", dep.ToString(), used->ToString());
				
					

					std::vector<std::string> parts;
					boost::algorithm::split(parts, dep.version, boost::is_any_of("."));

					for (auto& part : parts)
					{
						if (!part.empty())
							result = result->FindChild(part);

						if (!result)
							RE_THROW TargetDependencyException(
								&target,
								"unresolved partial uses-dependency '{}' <- '{}' (failed at part '{}')",
								dep.ToString(), used->ToString(), part
							);
					}
				}

				return result;
			}

			if (auto resolver = mDepResolvers[dep.ns])
				return resolver->ResolveTargetDependency(target, dep);
			else
				RE_THROW TargetLoadException(&target, "dependency '{}': unknown target namespace '{}'", dep.ToString(), dep.ns);
		}
		else
		{
			return nullptr;
		}
	}

	Target* BuildEnv::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		return ResolveTargetDependencyImpl(target, dep, true);
	}

	void BuildEnv::PopulateTargetMap(Target* pTarget)
	{
		RE_TRACE(" [DBG] Adding to target map: '{}'\n", pTarget->module);

		if (mTargetMap[pTarget->module] != nullptr)
			RE_THROW TargetLoadException(pTarget, "target defined more than once");

		mTargetMap[pTarget->module] = pTarget;

		for (auto& child : pTarget->children)
			PopulateTargetMap(child.get());
	}

	void BuildEnv::AppendDepsAndSelf(Target* pTarget, std::vector<Target*>& to, bool throw_on_missing, bool use_external)
	{
		PopulateTargetDependencySet(pTarget, to, [this, &to, pTarget, throw_on_missing, use_external](const Target& target, const TargetDependency& dep) -> Target*
		{
			return ResolveTargetDependencyImpl(target, dep, use_external);
		}, throw_on_missing);
	}

	void BuildEnv::RunActionList(const NinjaBuildDesc* desc, Target *target, const TargetConfig &list, std::string_view run_type, const std::string &default_run_type)
	{
		for (const auto &v : list)
		{
			for (const auto &kv : v)
			{
				auto type = kv.first.as<std::string>();
				auto& data = kv.second;

				std::string run = default_run_type;
				RE_TRACE("{} -> action {}\n", target->module, type);

				if (auto run_val = data["at"])
					run = run_val.as<std::string>();

				if (run == run_type)
					RunTargetAction(desc, *target, type, data);
			}
		}
	}

	void BuildEnv::RunActionsCategorized(Target* target, const NinjaBuildDesc* desc, std::string_view run_type)
	{
		if (auto actions = target->GetCfgEntry<TargetConfig>("actions"))
		{
			if (actions->IsMap())
			{
				for (const auto& kv : *actions)
				{
					auto type = kv.first.as<std::string>();
					auto& data = kv.second;

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
