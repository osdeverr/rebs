#include "target_cfg_utils.h"
#include "yaml_merge.h"

#include <re/debug.h>

// #include <boost/algorithm/string.hpp>
#include <ulib/string.h>
#include <ulib/format.h>

namespace re
{
    TargetConfig GetFlatResolvedTargetCfg(const TargetConfig &cfg,
                                          const std::unordered_map<std::string, std::string> &mappings)
    {
        // recurse the ky
        auto result = Clone(cfg);

        if (cfg.IsMap())
        {
            for (auto &kv : cfg)
            {
                std::string key = kv.first.as<std::string>();

                for (const auto &[category, value] : mappings)
                {
                    if (key.find(category + ".") == 0)
                    {
                        ulib::string raw = key.substr(category.size() + 1);

                        ulib::list<ulib::string> categories = raw.split("|");

                        bool supported = (raw == "any");

                        for (auto &supported_category : categories)
                        {
                            RE_TRACE("{}.{}\n", category, supported_category);

                            if (supported)
                                break;

                            supported |= (value == supported_category ||
                                          ulib::string{value}.starts_with(supported_category + "."));

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

    TargetConfig GetResolvedTargetCfg(const Target &leaf, const std::unordered_map<std::string, std::string> &mappings)
    {
        auto p = leaf.parent;

        auto leaf_cfg = GetFlatResolvedTargetCfg(leaf.config, mappings);

        // Deps and uses are automatically recursed by Target facilities:
        // copying parent deps and uses into children would lead to a performance impact due to redundant regex parsing
        auto top_deps = Clone(leaf_cfg["deps"]);
        // auto top_uses = Clone(leaf_cfg["uses"]);

        auto top_actions = Clone(leaf_cfg["actions"]);
        auto top_tasks = Clone(leaf_cfg["tasks"]);

        std::vector<const Target *> genealogy = {&leaf};

        while (p)
        {
            genealogy.insert(genealogy.begin(), p);
            p = p->parent;
        }

        TargetConfig result{YAML::NodeType::Map};

        for (auto &target : genealogy)
            MergeYamlNode(result, GetFlatResolvedTargetCfg(target->config, mappings));

        result["deps"] = top_deps;
        // result["uses"] = top_uses;
        result["actions"] = top_actions;
        result["tasks"] = top_tasks;

        /*
                YAML::Emitter emitter;
                emitter << result;

                fmt::print(" [DBG] Flat target config for '{}':\n\n{}\n\n", leaf.module, emitter.c_str());
        */

        return result;
    }
} // namespace re
