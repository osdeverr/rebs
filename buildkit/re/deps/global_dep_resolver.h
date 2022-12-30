#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>
#include <re/target_loader.h>
#include <re/user_output.h>

#include <re/fs.h>

namespace re
{
    class GlobalDepResolver : public IDepResolver
    {
    public:
        GlobalDepResolver(const fs::path& path, ITargetLoader* pLoader, IUserOutput* pOut)
            : mPackagesPath{ fs::canonical(path) }, mLoader{ pLoader }, mOut{ pOut }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);

        void InstallGlobalPackage(const TargetDependency& dep, const TargetDependency& as);
        void SelectGlobalPackageTag(const TargetDependency& dep, const std::string& new_tag);
        std::vector<std::pair<std::string, bool>> GetGlobalPackageInfo(const TargetDependency& dep);
        std::unordered_map<std::string, std::string> GetGlobalPackageList();
        void RemoveGlobalPackage(const TargetDependency& dep);

    private:
        fs::path mPackagesPath;
        ITargetLoader* mLoader;
        IUserOutput* mOut;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
        
        void PopulateGlobalPackageList(const fs::path& subpath, std::unordered_map<std::string, std::string>& out);
    };
}
