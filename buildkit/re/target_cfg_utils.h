#pragma once
#include <re/target.h>

namespace re
{
	void MergeYamlNode(YAML::Node& target, const YAML::Node& source, bool overridden);

	void MergeYamlMap(YAML::Node& target, const YAML::Node& source, bool overridden);

	void MergeYamlSequences(YAML::Node& target, const YAML::Node& source, bool overridden);

	YAML::Node MergeYamlNodes(const YAML::Node& defaultNode, const YAML::Node& overrideNode);


	TargetConfig GetFlatResolvedTargetCfg(const TargetConfig& cfg, const std::unordered_map<std::string, std::string>& mappings);

	TargetConfig GetResolvedTargetCfg(const Target& leaf, const std::unordered_map<std::string, std::string>& mappings);
}
