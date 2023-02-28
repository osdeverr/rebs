/**
 * @file conan_dep_resolver.h
 * @author Nikita Ivanov
 * @brief Dependency resolution implementation for the Conan package manager
 * @version 0.3.5
 * @date 2023-01-20
 *
 * @copyright Copyright (c) 2023 Nikita Ivanov
 *
 */

#pragma once
#include <re/dep_resolver.h>
#include <re/target.h>
#include <re/user_output.h>

namespace re
{
    /**
     * @brief Implements IDepResolver to provide Conan package dependency support.
     */
    class ConanDepResolver : public IDepResolver
    {
    public:
        ConanDepResolver(IUserOutput *pOut) : mOut{pOut}
        {
        }

        Target *ResolveTargetDependency(const Target &target, const TargetDependency &dep, DepsVersionCache *cache);

        bool SaveDependencyToPath(const TargetDependency &dep, const fs::path &path);

        virtual bool DoesCustomHandleFilters()
        {
            return true;
        }

    private:
        IUserOutput *mOut;
        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
} // namespace re
