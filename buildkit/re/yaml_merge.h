#pragma once
#include <ulib/yaml.h>

namespace re
{
	void MergeYamlNode(ulib::yaml& target, const ulib::yaml& source, bool overridden = false);
	void MergeYamlMap(ulib::yaml& target, const ulib::yaml& source, bool overridden = false);
	void MergeYamlSequences(ulib::yaml& target, const ulib::yaml& source, bool overridden = false);

	ulib::yaml MergeYamlNodes(const ulib::yaml& defaultNode, const ulib::yaml& overrideNode);
}
