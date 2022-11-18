#pragma once
#include <string>
#include <vector>
#include <unordered_map>

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

	struct BuildTarget
	{
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
		// std::string name;

		// Substituted variables: those WILL end up in the build script
		BuildVars vars;

		// Arbitrary generation state: this won't go anywhere
		BuildVars state;

		std::vector<BuildTool> tools;
		std::vector<BuildRule> rules;
		std::vector<BuildTarget> targets;
	};
}
