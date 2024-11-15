#include "target_cfg_utils.h"
#include "yaml_merge.h"

#include <re/debug.h>

// #include <boost/algorithm/string.hpp>
#include <ulib/format.h>
#include <ulib/string.h>
#include <iostream>


namespace re
{
    TargetConfig GetFlatResolvedTargetCfg(const TargetConfig &cfg,
                                          const std::unordered_map<std::string, std::string> &mappings)
    {
        // recurse the ky
        auto result = cfg;

        if (cfg.is_map())
        {
            for (const auto &kv : cfg.items())
            {
                ulib::string_view key = kv.name();

                for (const auto &[category, value] : mappings)
                {
                    if (key.find(category + ".") == 0)
                    {
                        ulib::string_view raw = key.substr(category.size() + 1);
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
                            if (kv.value().is_scalar() && kv.value().scalar() == "unsupported")
                                RE_THROW Exception("unsupported {} '{}'", category, value);

                            auto cloned = GetFlatResolvedTargetCfg(kv.value(), mappings);

                            if (cloned.is_map())
                            {
                                for (auto& inner_kv : cloned.items())
                                    inner_kv.value() = GetFlatResolvedTargetCfg(inner_kv.value(), mappings);
                            }

                            MergeYamlNode(result, cloned);
                        }

                        if (result.is_map())
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
        auto top_deps = leaf_cfg["deps"];
        // auto top_uses = Clone(leaf_cfg["uses"]);

        auto top_actions = leaf_cfg["actions"];
        auto top_tasks = leaf_cfg["tasks"];

        std::vector<const Target *> genealogy = {&leaf};

        while (p)
        {
            genealogy.insert(genealogy.begin(), p);
            if (std::find(genealogy.begin(), genealogy.end(), p->parent) != genealogy.end())
            {
                std::system("pause");

                if (p->parent)
                {
                    fmt::print("p->name: {}\n", p->name);
                    fmt::print("p->path: {}\n", p->path.generic_string());
                    fmt::print("p->module: {}\n", p->module);
                    fmt::print("p->type: {}\n", TargetTypeToString(p->type));

                    // fmt::print("p->parent->name: {}\n", p->parent->name);
                    // fmt::print("p->parent->path: {}\n", p->parent->path.generic_string());
                    // fmt::print("p->parent->module: {}\n", p->parent->module);
                    // fmt::print("p->parent->type: {}\n", TargetTypeToString(p->parent->type));

                    // fmt::print("p->parent->resolved_config:\n");
                    // std::cout << p->parent->resolved_config << std::endl;

                    // fmt::print("p->parent->config:\n");
                    // std::cout << p->parent->config << std::endl;

                }

                break;
            }
                

            p = p->parent;
        }

    


        TargetConfig result{ulib::yaml::value_t::map};

        for (auto &target : genealogy)
            MergeYamlNode(result, GetFlatResolvedTargetCfg(target->config, mappings));

        // Everything is always inherited from the core config target in the root target
        if (leaf.parent &&
            (!leaf.parent->config.search("is-core-config") || leaf.parent->config["is-core-config"].get<bool>() != true))
        {
            result["deps"] = top_deps;
            // result["uses"] = top_uses;
            result["actions"] = top_actions;
            result["tasks"] = top_tasks;
        }

        result["is-core-config"] = false;

        /*
                YAML::Emitter emitter;
                emitter << result;

                fmt::print(" [DBG] Flat target config for '{}':\n\n{}\n\n", leaf.module, emitter.c_str());
        */

        return result;
    }
} // namespace re
