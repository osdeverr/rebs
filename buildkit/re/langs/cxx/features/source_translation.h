#pragma once
#include <re/target_feature.h>

namespace re
{
    class SourceTranslation : public TargetFeature<SourceTranslation>
    {
    public:
        static constexpr auto kFeatureName = "source-translation";

        void ProcessTargetPostInit(Target &target);
        void ProcessTargetPreBuild(Target &target);
    };
}
