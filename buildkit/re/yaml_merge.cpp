#include "yaml_merge.h"

#include <re/debug.h>
#include <re/error.h>


#include <stdexcept>

namespace re
{
    void MergeYamlNode(YAML::Node target, const YAML::Node &source, bool overridden)
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
            RE_THROW std::runtime_error("MergeNode: Undefined source nodes not supported");
        }
    }

    void MergeYamlMap(YAML::Node &target, const YAML::Node &source, bool overridden)
    {
        if (overridden)
        {
            target = Clone(source);
            return;
        }

        for (const auto &j : source)
        {
            auto key = j.first.Scalar();

            constexpr auto kOverridePrefix = "override.";

            if (overridden || key.find(kOverridePrefix) == 0)
            {
                RE_TRACE("OVERRIDE PREFIX found on {}\n", key);
                MergeYamlNode(target[key.substr(sizeof kOverridePrefix + 1)], j.second, true);
            }
            else
            {
                RE_TRACE("Not overriding {}\n", key);
                MergeYamlNode(target[key], j.second);
            }
        }
    }

    void MergeYamlSequences(YAML::Node &target, const YAML::Node &source, bool overridden)
    {
        if (overridden)
        {
            RE_TRACE("{} - override enabled, will copy over\n", target.Scalar());

            target = YAML::Clone(source);
            return;
        }
        else
            RE_TRACE("{} - override disabled\n", target.Scalar());

        for (std::size_t i = 0; i != source.size(); ++i)
        {
            RE_TRACE("  Adding {}\n", source[i].Scalar());
            target.push_back(YAML::Clone(source[i]));
        }
    }

    YAML::Node MergeYamlNodes(const YAML::Node &defaultNode, const YAML::Node &overrideNode)
    {
        auto cloned = Clone(defaultNode);
        MergeYamlNode(cloned, overrideNode);
        return cloned;
    }
} // namespace re
