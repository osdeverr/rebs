#include "target_name_appender.h"

namespace re
{
    void AppendTargetNameSuffix(Target *pTarget, ulib::string_view suffix)
    {
        pTarget->name += suffix;
        pTarget->config["name"] = pTarget->config["name"].scalar() + suffix;

        for (auto &child : pTarget->children)
            AppendTargetNameSuffix(child.get(), suffix);
    }
} // namespace re
