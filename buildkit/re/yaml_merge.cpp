#include "yaml_merge.h"

#include <re/debug.h>
#include <re/error.h>


#include <stdexcept>

namespace re
{
    void MergeYamlNode(ulib::yaml& target, const ulib::yaml &source, bool overridden)
    {
        switch (source.type())
        {
        case ulib::yaml::value_t::map:
            MergeYamlMap(target, source, overridden);
            break;
        case ulib::yaml::value_t::sequence:
            MergeYamlSequences(target, source, overridden);
            break;
        default:
            target = source;
            break;
        }
    }

    void MergeYamlMap(ulib::yaml &target, const ulib::yaml &source, bool overridden)
    {
        if (overridden)
        {
            target = source;
            return;
        }

        for (const auto &j : source.items())
        {
            auto key = j.name();

            constexpr auto kOverridePrefix = "override.";

            if (overridden || key.find(kOverridePrefix) == 0)
            {
                RE_TRACE("OVERRIDE PREFIX found on {}\n", key);
                MergeYamlNode(target[key.substr(sizeof kOverridePrefix + 1)], j.value(), true);
            }
            else
            {
                RE_TRACE("Not overriding {}\n", key);
                MergeYamlNode(target[key], j.value());
            }
        }
    }

    void MergeYamlSequences(ulib::yaml &target, const ulib::yaml &source, bool overridden)
    {
        if (overridden)
        {
            RE_TRACE("{} - override enabled, will copy over\n", target.scalar());

            target = source;
            return;
        }
        else
            RE_TRACE("{} - override disabled\n", target.scalar());

        for (std::size_t i = 0; i != source.size(); ++i)
        {
            RE_TRACE("  Adding {}\n", source[i].scalar());
            target.push_back(source[i]);
        }
    }

    ulib::yaml MergeYamlNodes(const ulib::yaml &defaultNode, const ulib::yaml &overrideNode)
    {
        auto cloned = defaultNode;
        MergeYamlNode(cloned, overrideNode);
        return cloned;
    }
} // namespace re
