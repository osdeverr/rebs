#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>

#include <re/fs.h>

namespace re
{
    class VcpkgDepResolver : public IDepResolver
    {
    public:
        VcpkgDepResolver(const fs::path& path)
            : mVcpkgPath{ path }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);

    private:
        fs::path mVcpkgPath;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
