#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>
#include <re/target_loader.h>
#include <re/user_output.h>

#include <string_view>

namespace re
{
    class GitDepResolver : public IDepResolver
    {
    public:
        GitDepResolver(ITargetLoader* pLoader, IUserOutput* pOut)
            : mLoader{ pLoader }, mOut{ pOut }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep, DepsVersionCache* cache);
        Target* ResolveGitDependency(const Target& target, const TargetDependency& dep, ulib::string_view url, ulib::string branch, DepsVersionCache* cache);
        void DownloadGitDependency(ulib::string_view url, ulib::string_view branch, const fs::path& to);
        
        virtual bool SaveDependencyToPath(const TargetDependency& dep, const fs::path& path);

    private:
        ITargetLoader* mLoader;
        IUserOutput* mOut;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
