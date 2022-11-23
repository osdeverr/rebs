#include "cxx_lang_provider.h"

#include <re/target.h>
#include <re/buildenv.h>

#include <fmt/format.h>

#include <fstream>

namespace re
{
	namespace
	{
		inline std::string GetEscapedModulePath(const Target& target)
		{
			auto module_escaped = target.module;
			std::replace(module_escaped.begin(), module_escaped.end(), '.', '_');
			return module_escaped;
		}

		inline TargetConfig GetRecursiveMapCfg(const Target& leaf, std::string_view key)
		{
			auto result = TargetConfig{ YAML::NodeType::Map };
			auto p = &leaf;

			while (p)
			{
				if (auto map = leaf.GetCfgEntry<TargetConfig>(key))
				{
					for (const std::pair<YAML::Node, YAML::Node>& kv : *map)
					{
						auto type = kv.first.as<std::string>();
						auto& yaml = kv.second;

						if (!result[type])
							result[type] = yaml;
					}
				}

				p = p->parent;
			}

			return result;
		}

		inline TargetConfig GetRecursiveSeqCfg(const Target& leaf, std::string_view key)
		{
			auto result = TargetConfig{ YAML::NodeType::Sequence };
			auto p = &leaf;

			while (p)
			{
				if (auto seq = leaf.GetCfgEntry<TargetConfig>(key))
				{
					for (const auto& v : *seq)
					{
						result.push_back(v);
					}
				}

				p = p->parent;
			}

			return result;
		}

		inline void AppendIncludeDirs(const Target& target, const std::string& cxx_include_dir_tpl, std::vector<std::string>& out_flags)
		{
			out_flags.push_back(fmt::format(
				cxx_include_dir_tpl,
				fmt::arg("directory", target.path.u8string())
			));

			auto extra_includes = GetRecursiveSeqCfg(target, "cxx-include-dirs");

			for (const auto& v : extra_includes)
			{
				auto dir = fs::path{ v.as<std::string>() };

				if (!dir.is_absolute())
					dir = target.path / dir;

				out_flags.push_back(fmt::format(
					cxx_include_dir_tpl,
					fmt::arg("directory", dir.u8string())
				));
			}
		}

		inline void AppendLinkFlags(const Target& target, const std::string& cxx_lib_dir_tpl, std::vector<std::string>& out_flags, std::unordered_set<std::string>& out_deps, const LocalVarScope& vars)
		{
			auto link_lib_dirs = GetRecursiveSeqCfg(target, "cxx-lib-dirs");

			for (const auto& dir : link_lib_dirs)
			{
				auto formatted = vars.Resolve(dir.as<std::string>());

				out_flags.push_back(fmt::format(
					cxx_lib_dir_tpl,
					fmt::arg("directory", formatted)
				));
			}

			auto extra_link_deps = GetRecursiveSeqCfg(target, "cxx-link-deps");

			for (const auto& dep : extra_link_deps)
				out_deps.insert(fmt::format("\"{}\"", vars.Resolve(dep.as<std::string>())));
		}

		template<class F>
		void EnumerateCategorizedStuffSeq(const TargetConfig& cfg, const std::string& category, const std::string& key, F callback, bool root = true)
		{
			if (const auto& categorized = cfg[category])
			{
				for (const auto& v : categorized)
					EnumerateCategorizedStuffSeq(v.second, category, callback, root);
			}
			else if (const auto& keyed = cfg[key])
			{
				for (const auto& v : keyed)
					EnumerateCategorizedStuffSeq(v, category, callback, false);
			}
		}
	}

	CxxLangProvider::CxxLangProvider(const fs::path& env_search_path, VarContext* var_ctx)
		: mEnvSearchPath{ env_search_path }, mVarCtx{ var_ctx }
	{
	}

	void CxxLangProvider::InitInBuildDesc(NinjaBuildDesc& desc)
	{
		// Do nothing
	}

	bool CxxLangProvider::InitBuildTarget(NinjaBuildDesc& desc, const Target& target)
	{
		// if (target.type != TargetType::Executable && target.type != TargetType::StaticLibrary && target.type != TargetType::SharedLibrary)
		//	return false;

		/////////////////////////////////////////////////////////////////

		auto path = GetEscapedModulePath(target);

		LocalVarScope vars{ mVarCtx, "target", &target };

		// Choose and load the correct build environment.

		auto env_cfg = target.GetCfgEntryOrThrow<TargetConfig>("cxx-env", "C++ environment not specified anywhere in the target tree", CfgEntryKind::Recursive);
		auto& env_cached_name = desc.state["re_cxx_env_for_" + path];

		if (!env_cfg.IsMap())
		{
			env_cached_name = env_cfg.as<std::string>();
		}
		else
		{
			auto& platform = vars.Resolve("${platform}");
			auto& platform_closest = vars.Resolve("${platform-closest}");

			if (auto val = env_cfg[platform])
				env_cached_name = val.as<std::string>();
			else if (auto val = env_cfg[platform_closest])
				env_cached_name = val.as<std::string>();
			else if (auto val = env_cfg["default"])
				env_cached_name = val.as<std::string>();
			else
				RE_THROW TargetBuildException(&target, "failed to find platform-specific C++ environment");
		}

		/////////////////////////////////////////////////////////////////

		//
		// This is guaranteed to either give us a working environment or to mess up the build.
		//
		CxxBuildEnvData& env = LoadEnvOrThrow(env_cached_name, target);

		TargetConfig cxx_flags = GetRecursiveMapCfg(target, "cxx-flags");
		TargetConfig link_flags = GetRecursiveMapCfg(target, "cxx-link-flags");

		TargetConfig definitions = GetRecursiveMapCfg(target, "cxx-compile-definitions");
		TargetConfig definitions_pub = GetRecursiveMapCfg(target, "cxx-compile-definitions-public");

		if (auto vars_cfg = env["vars"])
			for (const auto& kv : vars_cfg)
				vars.SetVar(kv.first.as<std::string>(), kv.second.as<std::string>());

		// Make the local definitions supersede all platform ones
		for (const auto& def : env["platform-definitions"])
			if (!definitions[def.first])
				definitions[def.first] = def.second;

		std::vector<const Target*> include_deps;
		PopulateTargetDependencySetNoResolve(&target, include_deps);

		for (auto& target : include_deps)
		{
			auto dependency_defines = GetRecursiveMapCfg(*target, "cxx-compile-definitions-public");

			for (const std::pair<YAML::Node, YAML::Node>& kv : dependency_defines)
			{
				auto name = kv.first.as<std::string>();
				auto value = kv.second.as<std::string>();

				if (!definitions_pub[name])
					definitions_pub[name] = value;
			}
		}

		for (const std::pair<YAML::Node, YAML::Node>& kv : definitions_pub)
		{
			auto name = kv.first.as<std::string>();
			auto value = kv.second.as<std::string>();

			if (!definitions[name])
				definitions[name] = value;
		}

		/////////////////////////////////////////////////////////////////

		auto& default_cflags = desc.vars["re_cxx_target_cflags_" + path] = vars.Resolve(env["default-flags"]["compiler"].as<std::string>());
		auto& default_lflags = desc.vars["re_cxx_target_lflags_" + path] = vars.Resolve(env["default-flags"]["linker"].as<std::string>());
		auto& default_arflags = desc.vars["re_cxx_target_arflags_" + path] = vars.Resolve(env["default-flags"]["archiver"].as<std::string>());

		/////////////////////////////////////////////////////////////////

		// TODO: Configuration/arch/platform switches

		/////////////////////////////////////////////////////////////////

		std::string flags_base = fmt::format("$re_cxx_target_cflags_{} $target_custom_flags ", path);
		std::string out_dir = fmt::format("$builddir/{}", target.module);

		const auto& templates = env["templates"];

		std::vector<std::string> extra_flags;

		std::string cpp_std = target.GetCfgEntry<std::string>("cxx-version", CfgEntryKind::Recursive).value_or("latest");

		extra_flags.push_back(fmt::format(
			templates["cxx-standard"].as<std::string>(),
			fmt::arg("version", cpp_std)
		));

		extra_flags.push_back(fmt::format(
			templates["cxx-module-output"].as<std::string>(),
			fmt::arg("directory", out_dir)
		));

		auto cxx_include_dir = templates["cxx-include-dir"].as<std::string>();
		auto cxx_module_lookup_dir = templates["cxx-module-lookup-dir"].as<std::string>();

		for (auto& target : include_deps)
		{
			AppendIncludeDirs(*target, cxx_include_dir, extra_flags);

			// TODO: Make this only work with modules enabled???
			extra_flags.push_back(fmt::format(
				cxx_module_lookup_dir,
				fmt::arg("directory", fmt::format("$builddir/{}", target->module))
			));
		}


		/////////////////////////////////////////////////////////////////

		auto cxx_compile_definitions = templates["cxx-compile-definition"].as<std::string>();

		for (const auto& kv : definitions)
		{
			auto name = kv.first.as<std::string>();
			auto value = kv.second.as<std::string>();

			extra_flags.push_back(fmt::format(
				cxx_compile_definitions,
				fmt::arg("name", name),
				fmt::arg("value", value)
			));
		}

		/////////////////////////////////////////////////////////////////

		for (auto& flag : extra_flags)
		{
			flags_base.append(flag);
			flags_base.append(" ");
		}

		/////////////////////////////////////////////////////////////////

		// Forward the C++ build tools definitions to the build system
		for (const auto& kv : env["tools"])
			desc.tools.push_back(BuildTool{ "cxx_" + kv.first.as<std::string>() + "_" + path, vars.Resolve(kv.second.as<std::string>())});

		// Create build rules

		BuildRule rule_cxx;

		rule_cxx.name = "cxx_compile_" + path;
		rule_cxx.tool = "cxx_compiler_" + path;
		rule_cxx.cmdline = fmt::format(
			templates["compiler-cmdline"].as<std::string>(),
			fmt::arg("flags", flags_base),
			fmt::arg("input", "$in"),
			fmt::arg("output", "$out")
		);
		rule_cxx.description = "Building C++ source $in";

		if (auto rule_vars = env["custom-rule-vars"])
			for (const auto& var : rule_vars)
				rule_cxx.vars[var.first.as<std::string>()] = var.second.as<std::string>();

		std::unordered_set<std::string> deps_list;
		std::vector<std::string> extra_link_flags;

		auto cxx_lib_dir = templates["cxx-lib-dir"].as<std::string>();

		for (auto& dep : include_deps)
		{
			//if (dep->type != TargetType::StaticLibrary)
			//	continue;

			/*
			bool skip = false;

			for (auto& dep_in : target.dependencies)
			{
				for (auto& dep_in_in : dep_in.resolved->dependencies)
					if (dep_in_in.resolved == dep.resolved)
						skip = true;
			}

			if (skip)
				continue;
				*/

			auto res_path = GetEscapedModulePath(*dep);
			bool has_any_eligible_sources = (desc.state["re_cxx_target_has_objects_" + res_path] == "1");

			if (dep->type == TargetType::StaticLibrary && has_any_eligible_sources)
			{
				deps_list.insert("$cxx_artifact_" + res_path);
			}

			AppendLinkFlags(*dep, cxx_lib_dir, extra_link_flags, deps_list, vars);
		}

		std::string extra_link_flags_str = "";

		for (auto& flag : extra_link_flags)
		{
			extra_link_flags_str.append(" ");
			extra_link_flags_str.append(flag);
		}

		std::string deps_input = "";

		for (auto& dep : deps_list)
		{
			deps_input.append(dep);
			deps_input.append(" ");
		}

		BuildRule rule_link;

		rule_link.name = "cxx_link_" + path;
		rule_link.tool = "cxx_linker_" + path;

		rule_link.cmdline = fmt::format(
			templates["linker-cmdline"].as<std::string>(),
			fmt::arg("flags", "$target_custom_flags $re_cxx_target_lflags_" + path + extra_link_flags_str),
			fmt::arg("link_deps", deps_input),
			fmt::arg("input", "$in"),
			fmt::arg("output", "$out")
		);
		rule_link.description = "Linking target $out";

		BuildRule rule_lib;

		rule_lib.name = "cxx_archive_" + path;
		rule_lib.tool = "cxx_archiver_" + path;
		rule_lib.cmdline = fmt::format(
			templates["archiver-cmdline"].as<std::string>(),
			fmt::arg("flags", "$target_custom_flags $re_cxx_target_arflags_" + path + extra_link_flags_str),
			fmt::arg("link_deps", deps_input),
			fmt::arg("input", "$in"),
			fmt::arg("output", "$out")
		);
		rule_lib.description = "Archiving target $out";

		desc.rules.emplace_back(std::move(rule_cxx));
		desc.rules.emplace_back(std::move(rule_link));
		desc.rules.emplace_back(std::move(rule_lib));

		desc.vars["cxx_path_" + path] = target.path.u8string();
		desc.vars["cxx_config_path_" + path] = target.config_path.u8string();

		return true;
	}

	void CxxLangProvider::ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file)
	{
		if (target.type == TargetType::Project)
			return;

		auto path = GetEscapedModulePath(target);
		auto& env = mEnvCache.at(desc.state.at("re_cxx_env_for_" + path));

		bool eligible = false;

		for (const auto& ext : env["supported-extensions"])
			if (file.extension == ext.as<std::string>())
				eligible = true;

		if (!eligible)
			return;

		auto local_path = file.path.u8string().substr(target.path.u8string().size() + 1);
		auto extension = env["default-extensions"]["object"].as<std::string>();

		BuildTarget build_target;

		build_target.type = BuildTargetType::Object;

		build_target.pSourceTarget = &target;
		build_target.pSourceFile = &file;

		build_target.in = "$cxx_path_" + path + "/" + local_path;
		build_target.out = fmt::format("$builddir/{}/{}.{}", desc.GetObjectDirectory(target.module), local_path, extension);
		build_target.rule = "cxx_compile_" + path;

		desc.targets.emplace_back(std::move(build_target));

		desc.state["re_cxx_target_has_objects_" + path] = "1";
	}

	void CxxLangProvider::CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target)
	{
		auto path = GetEscapedModulePath(target);

		auto& env = mEnvCache.at(desc.state.at("re_cxx_env_for_" + path));
		const auto& default_extensions = env["default-extensions"];

		BuildTarget link_target;

		link_target.type = BuildTargetType::Artifact;
		link_target.pSourceTarget = &target;
		link_target.out = "$builddir/" + desc.GetArtifactDirectory(target.module) + "/" + target.module;
		link_target.rule = "cxx_link_" + path;

		std::string extension = "";

		switch (target.type)
		{
		case TargetType::Executable:
			extension = default_extensions["executable"].as<std::string>();
			break;
		case TargetType::StaticLibrary:
			extension = default_extensions["static-library"].as<std::string>();
			link_target.rule = "cxx_archive_" + path;
			break;
		case TargetType::SharedLibrary:
			extension = default_extensions["shared-library"].as<std::string>();

			link_target.vars["target_custom_flags"].append(" ");
			link_target.vars["target_custom_flags"].append(env["templates"]["link-as-shared-library"].as<std::string>());
			break;
		case TargetType::Project:
			link_target.rule = "phony";
			break;
		}

		if (auto out_ext = target.GetCfgEntry<std::string>("out-ext"))
			extension = *out_ext;

		if (!extension.empty())
		{
			link_target.out.append(".");
			link_target.out.append(extension);
		}

		for(auto& build_target : desc.targets)
			if (build_target.pSourceTarget == &target && build_target.pSourceFile)
			{
				link_target.in.append(build_target.out);
				link_target.in.append(" ");
			}

		std::vector<const Target*> link_deps;
		PopulateTargetDependencySetNoResolve(&target, link_deps);

		for (auto& dep : link_deps)
			if (dep != &target)
			{
				auto artifact = desc.vars["cxx_artifact_" + GetEscapedModulePath(*dep)];
				if (!artifact.empty())
					link_target.deps.push_back(artifact);
			}

		link_target.deps.push_back("$cxx_config_path_" + path);

		BuildTarget alias_target;

		alias_target.type = BuildTargetType::Alias;
		alias_target.in = link_target.out;
		alias_target.out = target.module;
		alias_target.rule = "phony";

		desc.vars["cxx_artifact_" + path] = link_target.out;

		desc.targets.emplace_back(std::move(link_target));
		desc.targets.emplace_back(std::move(alias_target));
	}

	CxxBuildEnvData& CxxLangProvider::LoadEnvOrThrow(std::string_view name, const Target& invokee)
	{
		if (mEnvCache.find(name.data()) != mEnvCache.end())
			return mEnvCache[name.data()];

		try
		{
			// std::ifstream stream{ (mEnvSearchPath / name.data() / ".yml") };

			auto& data = (mEnvCache[name.data()] = YAML::LoadFile(mEnvSearchPath.u8string() + "/" + name.data() + ".yml"));

			if(auto inherits = data["inherits"])
				for (const auto& v : inherits)
				{
					auto& other = LoadEnvOrThrow(v.as<std::string>(), invokee);

					for (const auto& pair : other)
						if (!data[pair.first])
							data[pair.first] = pair.second;
				}

			return data;
		}
		catch (const std::exception& e)
		{
			RE_THROW TargetBuildException(&invokee, "failed to load C++ environment {}: {}", name, e.what());
		}
	}
}
