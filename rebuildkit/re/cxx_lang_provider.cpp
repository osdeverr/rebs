#include "cxx_lang_provider.h"
#include "target.h"
#include "buildenv.h"

#include <fmt/format.h>

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
	}

	CxxLangProvider::CxxLangProvider(std::string_view env_search_path)
		: mEnvSearchPath{ env_search_path }
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

		// Choose and load the correct build environment.

		auto env_cfg = target.GetCfgEntryOrThrow<TargetConfig>("cxx-env", "C++ environment not specified anywhere in the target tree", CfgEntryKind::Recursive);
		auto& env_cached_name = desc.vars["re_cxx_env_for_" + path];

		if (!env_cfg.IsMap())
		{
			env_cached_name = env_cfg.as<std::string>();
		}
		else
		{
			auto& platform = desc.vars.at("re_build_platform");
			auto& platform_closest = desc.vars.at("re_build_platform_closest");

			if (auto val = env_cfg[platform])
				env_cached_name = val.as<std::string>();
			else if (auto val = env_cfg[platform_closest])
				env_cached_name = val.as<std::string>();
			else if (auto val = env_cfg["default"])
				env_cached_name = val.as<std::string>();
			else
				throw TargetLoadException("failed to find platform-specific C++ environment");
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

		desc.vars["re_cxx_target_cflags_" + path] = env["default-flags"]["compiler"].as<std::string>();
		desc.vars["re_cxx_target_lflags_" + path] = env["default-flags"]["linker"].as<std::string>();
		desc.vars["re_cxx_target_arflags_" + path] = env["default-flags"]["archiver"].as<std::string>();

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

		for (auto& target : include_deps)
		{
			extra_flags.push_back(fmt::format(
				templates["cxx-include-dir"].as<std::string>(),
				fmt::arg("directory", target->path)
			));

			// TODO: Make this only work with modules enabled???
			extra_flags.push_back(fmt::format(
				templates["cxx-module-lookup-dir"].as<std::string>(),
				fmt::arg("directory", fmt::format("$builddir/{}", target->module))
			));
		}

		/////////////////////////////////////////////////////////////////

		for (const auto& kv : definitions)
		{
			auto name = kv.first.as<std::string>();
			auto value = kv.second.as<std::string>();

			extra_flags.push_back(fmt::format(
				templates["cxx-compile-definition"].as<std::string>(),
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
			desc.tools.push_back(BuildTool{ "cxx_" + kv.first.as<std::string>() + "_" + path, kv.second.as<std::string>()});

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

		for (auto& dep : include_deps)
		{
			if (dep->type != TargetType::StaticLibrary)
				continue;

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
			bool has_any_eligible_sources = (desc.vars["re_cxx_target_has_objects_" + res_path] == "1");

			if (has_any_eligible_sources)
			{
				deps_list.insert("$cxx_artifact_" + res_path);
			}
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
			fmt::arg("flags", "$target_custom_flags $re_cxx_target_lflags_" + path),
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
			fmt::arg("flags", "$target_custom_flags $re_cxx_target_arflags_" + path),
			fmt::arg("link_deps", deps_input),
			fmt::arg("input", "$in"),
			fmt::arg("output", "$out")
		);
		rule_lib.description = "Archiving target $out";

		desc.rules.emplace_back(std::move(rule_cxx));
		desc.rules.emplace_back(std::move(rule_link));
		desc.rules.emplace_back(std::move(rule_lib));

		desc.vars["cxx_path_" + path] = target.path;
		desc.vars["cxx_config_path_" + path] = target.config_path;

		return true;
	}

	void CxxLangProvider::ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file)
	{
		auto path = GetEscapedModulePath(target);
		auto& env = mEnvCache.at(desc.vars.at("re_cxx_env_for_" + path));

		bool eligible = false;

		for (const auto& ext : env["supported-extensions"])
			if (file.extension == ext.as<std::string>())
				eligible = true;

		if (!eligible)
			return;

		auto local_path = file.path.substr(target.path.size() + 1);
		auto extension = env["default-extensions"]["object"].as<std::string>();

		BuildTarget build_target;
		build_target.pSourceTarget = &target;
		build_target.pSourceFile = &file;

		build_target.in = "$cxx_path_" + path + "/" + local_path;
		build_target.out = fmt::format("$builddir/{}/{}.{}", target.module, local_path, extension);
		build_target.rule = "cxx_compile_" + path;

		desc.targets.emplace_back(std::move(build_target));

		desc.vars["re_cxx_target_has_objects_" + path] = "1";
	}

	void CxxLangProvider::CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target)
	{
		auto path = GetEscapedModulePath(target);

		auto& env = mEnvCache.at(desc.vars.at("re_cxx_env_for_" + path));
		const auto& default_extensions = env["default-extensions"];

		BuildTarget link_target;

		link_target.pSourceTarget = &target;
		link_target.out = "$builddir/build/" + target.module;
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
			if (dep != &target && dep->type != TargetType::Custom)
				link_target.deps.push_back(dep->module);

		link_target.deps.push_back("$cxx_config_path_" + path);

		BuildTarget alias_target;

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
			auto& data = (mEnvCache[name.data()] = YAML::LoadFile((mEnvSearchPath + "/" + name.data() + ".yml").data()));

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
		catch (YAML::BadFile const& e)
		{
			throw TargetLoadException(std::string("failed to load C++ environment ") + name.data() + " for target " + invokee.module + ": " + e.what());
		}
	}
}
