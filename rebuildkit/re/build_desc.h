#pragma once
#include <string>
#include <vector>
#include <unordered_map>

#include <fmt/format.h>

#include <re/fs.h>

namespace re
{
	using BuildVars = std::unordered_map<std::string, std::string>;

	class Target;
	struct SourceFile;

	struct BuildTool
	{
		std::string name;
		std::string path;
	};

	struct BuildRule
	{
		std::string name;
		std::string tool;
		std::string cmdline;
		std::string description;

		BuildVars vars;
	};

	enum class BuildTargetType
	{
		Auxiliar,
		Object,
		Artifact,
		Alias
	};

	struct BuildTarget
	{
		BuildTargetType type;

		std::string rule;
		std::string in;
		std::string out;

		BuildVars vars;

		std::vector<std::string> deps;

		const Target* pSourceTarget = nullptr;
		const SourceFile* pSourceFile = nullptr;
	};

	struct NinjaBuildDesc
	{
		fs::path out_dir;

		std::string object_out_format;
		std::string artifact_out_format;

		// Substituted variables: those WILL end up in the build script
		BuildVars vars;

		// Arbitrary generation state: this won't go anywhere
		BuildVars state;

		std::vector<BuildTool> tools;
		std::vector<BuildRule> rules;
		std::vector<BuildTarget> targets;

		std::string GetObjectDirectory(std::string_view module) const
		{
			return fmt::format(object_out_format, fmt::arg("module", module));
		}

		std::string GetArtifactDirectory(std::string_view module) const
		{
			return fmt::format(artifact_out_format, fmt::arg("module", module));
		}
	};
}
