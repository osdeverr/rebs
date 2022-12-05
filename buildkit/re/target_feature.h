#pragma once
#include <re/target.h>

namespace re
{
    struct ITargetFeature
    {
        virtual ~ITargetFeature() = default;

        virtual const char *GetName() = 0;
        virtual void ProcessTargetPostInit(Target &target) {}
        virtual void ProcessTargetPreBuild(Target &target) {}
    };

    template <class T>
    struct TargetFeature : ITargetFeature
    {
        virtual const char *GetName() { return T::kFeatureName; }
    };
}
