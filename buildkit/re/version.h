#pragma once
#include "version_impl.h"

namespace re
{
    constexpr auto GetBuildVersionTag() { return kBuildVersionTag; }
    constexpr auto GetBuildRevision() { return kBuildRevision; }
}
