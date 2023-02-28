#pragma once
#include "target.h"

#include <memory>
#include <string_view>


#include <re/fs.h>
#include <re/vars.h>


namespace re
{
    class DepsVersionCache;

    struct IDepResolver
    {
        virtual ~IDepResolver() = default;

        virtual Target *ResolveTargetDependency(const Target &target, const TargetDependency &dep,
                                                DepsVersionCache *cache) = 0;
        virtual Target *ResolveCoercedTargetDependency(const Target &target, const Target &dep)
        {
            return nullptr;
        }

        virtual bool SaveDependencyToPath(const TargetDependency &dep, const fs::path &path)
        {
            return false;
        }

        virtual bool DoesCustomHandleFilters()
        {
            return false;
        }
    };
} // namespace re
