#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>

#include <re/target_loader.h>

namespace re
{
    class ArchCoercedDepResolver : public IDepResolver
    {
    public:
        ArchCoercedDepResolver(ITargetLoader* loader)
            : mLoader{ loader }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);
        Target* ResolveCoercedTargetDependency(const Target& target, const Target& dep);

    private:
        ITargetLoader* mLoader;
        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
