#include "environment_var_namespace.h"

namespace re
{
    std::optional<std::string> EnvironmentVarNamespace::GetVar(const std::string &key) const
    {
        if (auto var = std::getenv(key.c_str()))
            return var;
        else
            return std::nullopt;
    }
} // namespace re
