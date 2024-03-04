#include "target_name_appender.h"

namespace re
{
    void AppendTargetNameSuffix(Target *pTarget, const std::string &suffix)
    {
        pTarget->name += suffix;
        pTarget->config["name"] = pTarget->config["name"].Scalar() + suffix;

        for (auto &child : pTarget->children)
            AppendTargetNameSuffix(child.get(), suffix);
    }
} // namespace re
