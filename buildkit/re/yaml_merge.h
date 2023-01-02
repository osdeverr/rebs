#pragma once
#include <yaml-cpp/yaml.h>

namespace re
{
	void MergeYamlNode(YAML::Node& target, const YAML::Node& source, bool overridden = false);

	void MergeYamlMap(YAML::Node& target, const YAML::Node& source, bool overridden = false);

	void MergeYamlSequences(YAML::Node& target, const YAML::Node& source, bool overridden = false);

	YAML::Node MergeYamlNodes(const YAML::Node& defaultNode, const YAML::Node& overrideNode);
}
