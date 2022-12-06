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

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);
        Target* ResolveGitDependency(const Target& target, const TargetDependency& dep, std::string_view url, std::string_view branch);
        void DownloadGitDependency(std::string_view url, std::string_view branch, const fs::path& to);

    private:
        ITargetLoader* mLoader;
        IUserOutput* mOut;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}
