#pragma once
#include <re/target_feature.h>

namespace re
{
    class Cpp2Translation : public TargetFeature<Cpp2Translation>
    {
    public:
        static constexpr auto kFeatureName = "cpp2-translation";

        void ProcessTargetPostInit(Target &target);
    };
}
