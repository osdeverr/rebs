#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>

#include <re/target_loader.h>

namespace re
{
    class FsDepResolver : public IDepResolver
    {
    public:
        FsDepResolver(ITargetLoader* loader)
            : mLoader{ loader }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep, DepsVersionCache* cache);

        virtual bool SaveDependencyToPath(const TargetDependency& dep, const fs::path& path);

    private:
        ITargetLoader* mLoader;
        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
