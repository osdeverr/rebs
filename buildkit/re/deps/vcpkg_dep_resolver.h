#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>
#include <re/user_output.h>

#include <re/fs.h>

namespace re
{
    class VcpkgDepResolver : public IDepResolver
    {
    public:
        VcpkgDepResolver(const fs::path& path, IUserOutput* pOut)
            : mVcpkgPath{ fs::canonical(path) }, mOut{ pOut }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);
        
        virtual bool SaveDependencyToPath(const TargetDependency& dep, const fs::path& path);

    private:
        fs::path mVcpkgPath;
        IUserOutput* mOut;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
