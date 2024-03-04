#pragma once
#include <re/packages/re_package_client.h>

#include <re/dep_resolver.h>
#include <re/target.h>
#include <re/target_loader.h>
#include <re/user_output.h>

#include <string_view>

namespace re
{
    class RePackageDepResolver : public IDepResolver
    {
    public:
        RePackageDepResolver(ITargetLoader *pLoader, IUserOutput *pOut, const std::string &repo_id,
                             RePackageClient &&client)
            : mLoader{pLoader}, mOut{pOut}, mRepoId{repo_id}, mClient{std::move(client)}
        {
        }

        Target *ResolveTargetDependency(const Target &target, const TargetDependency &dep, DepsVersionCache *cache);

    private:
        ITargetLoader *mLoader;
        IUserOutput *mOut;

        std::string mRepoId;
        RePackageClient mClient;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
} // namespace re
