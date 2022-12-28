#include "target_cfg_utils.h"
#include "yaml_merge.h"

#include <re/debug.h>

#include <boost/algorithm/string.hpp>

namespace re
{
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
								for (auto inner_kv : cloned)
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

		// Deps and uses are automatically recursed by Target facilities:
		// copying parent deps and uses into children would lead to a performance impact due to redundant regex parsing
		auto top_deps = Clone(result["deps"]);
		auto top_uses = Clone(result["uses"]);

		while (p)
		{
			result = MergeYamlNodes(GetFlatResolvedTargetCfg(p->config, mappings), result);
			p = p->parent;
		}

		result["deps"] = top_deps;
		result["uses"] = top_uses;

		/*
		YAML::Emitter emitter;
		emitter << result;

		RE_TRACE(" [DBG] Flat target config for '{}':\n\n{}\n\n", leaf.module, emitter.c_str());
		*/

		return result;
	}
}
