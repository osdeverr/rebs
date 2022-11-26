#include "target_cfg_utils.h"

#include <re/debug.h>

#include <boost/algorithm/string.hpp>

namespace re
{
	void MergeYamlNode(YAML::Node& target, const YAML::Node& source, bool overridden = false)
	{
		switch (source.Type())
		{
		case YAML::NodeType::Scalar:
			target = source.Scalar();
			break;
		case YAML::NodeType::Map:
			MergeYamlMap(target, source, overridden);
			break;
		case YAML::NodeType::Sequence:
			MergeYamlSequences(target, source, overridden);
			break;
		case YAML::NodeType::Null:
			target = source;
			break;
			// throw std::runtime_error("merge_node: Null source nodes not supported");
		case YAML::NodeType::Undefined:
			RE_THROW std::runtime_error(
				"MergeNode: Undefined source nodes not supported");
		}
	}

	void MergeYamlMap(YAML::Node& target, const YAML::Node& source, bool overridden = false)
	{
		if (overridden)
		{
			target = Clone(source);
			return;
		}

		for (auto const& j : source) {
			auto key = j.first.Scalar();

			constexpr auto kOverridePrefix = "override.";

			if (overridden || key.find(kOverridePrefix) == 0)
				MergeYamlNode(target[key.substr(sizeof kOverridePrefix + 1)], j.second, true);
			else
				MergeYamlNode(target[key], j.second);
		}
	}

	void MergeYamlSequences(YAML::Node& target, const YAML::Node& source, bool overridden = false)
	{
		if (overridden)
		{
			target = Clone(source);
			return;
		}

		for (std::size_t i = 0; i != source.size(); ++i) {
			target.push_back(YAML::Clone(source[i]));
		}
	}

	YAML::Node MergeYamlNodes(const YAML::Node& defaultNode, const YAML::Node& overrideNode)
	{
		auto cloned = Clone(defaultNode);
		MergeYamlNode(cloned, overrideNode);
		return cloned;
	}


	TargetConfig GetFlatResolvedTargetCfg(const TargetConfig& cfg, const std::unordered_map<std::string, std::string>& mappings)
	{
		// recurse the ky
		auto result = Clone(cfg);

		if (cfg.IsMap())
		{
			for (auto& kv : cfg)
			{
				std::string key = kv.first.as<std::string>();

				for (const auto& [category, value] : mappings)
				{
					if (key.find(category + ".") == 0)
					{
						auto raw = key.substr(category.size() + 1);

						std::vector<std::string> categories;
						boost::algorithm::split(categories, raw, boost::is_any_of("|"));

						bool supported = (raw == "any");

						for (auto& supported_category : categories)
						{
							RE_TRACE("{}.{}\n", category, supported_category);

							if (supported)
								break;

							supported |= (supported_category == value);

							if (supported_category.front() == '!')
							{
								RE_TRACE("    Negation: value={}\n", (supported_category != value.substr(1)));
								supported |= (supported_category.substr(1) != value);
							}
						}

						if (supported)
						{
							if (kv.second.IsScalar() && kv.second.Scalar() == "unsupported")
								RE_THROW Exception("unsupported {} '{}'", category, value);

							auto cloned = GetFlatResolvedTargetCfg(kv.second, mappings);

							if (cloned.IsMap())
							{
								for (auto& inner_kv : cloned)
									inner_kv.second = GetFlatResolvedTargetCfg(inner_kv.second, mappings);
							}

							MergeYamlNode(result, cloned);
						}

						result.remove(key);
						break;
					}
				}
			}
		}

		return result;
	}

	TargetConfig GetResolvedTargetCfg(const Target& leaf, const std::unordered_map<std::string, std::string>& mappings)
	{
		auto result = GetFlatResolvedTargetCfg(leaf.config, mappings);
		auto p = leaf.parent;

		while (p)
		{
			result = MergeYamlNodes(GetFlatResolvedTargetCfg(p->config, mappings), result);
			p = p->parent;
		}

		YAML::Emitter emitter;
		emitter << result;

		RE_TRACE(" [DBG] Flat target config for '{}':\n\n{}\n\n", leaf.module, emitter.c_str());

		return result;
	}
}
