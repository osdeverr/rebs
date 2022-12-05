#pragma once
#include <re/target_feature.h>

namespace re
{
    class CxxHeaderProjection : public TargetFeature<CxxHeaderProjection>
    {
    public:
        static constexpr auto kFeatureName = "cxx-header-projection";

        void ProcessTargetPostInit(Target &target);
    };
}
