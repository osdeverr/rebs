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
		
		BuildVars vars;

		std::vector<BuildTool> tools;
		std::vector<BuildRule> rules;
		std::vector<BuildTarget> targets;
	};
}
