#include "environment_var_namespace.h"

#include <ulib/env.h>

namespace re
{
    std::optional<ulib::string> EnvironmentVarNamespace::GetVar(ulib::string_view key) const
    {
        if (auto var = ulib::getenv(ulib::u8(key)))
            return ulib::sstr(*var);
        else
            return std::nullopt;
    }
} // namespace re
