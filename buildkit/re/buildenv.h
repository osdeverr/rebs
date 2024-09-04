/**
 * @file re/buildenv.h
 * @author Nikita Ivanov
 * @brief Low-level target resolution and build functionality
 * @version 0.2.8
 * @date 2023-01-14
 *
 * @copyright Copyright (c) 2023 Nikita Ivanov
 */

#pragma once
#include "build_desc.h"
#include "dep_resolver.h"
#include "deps_version_cache.h"
#include "target.h"
#include "target_load_middleware.h"
#include "target_loader.h"
#include "user_output.h"

#include <re/vars.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <unordered_set>

namespace re
{
    /**
     * @brief A function capable of resolving target dependencies.
     */
    using TargetDepResolver = std::function<bool(const Target &, const TargetDependency &, ulib::list<Target *> &)>;

    /**
     * @brief Populates the specified collection with a flat target dependency set, resolving all target dependences in
     * progress.
     *
     * @param pTarget The root target to start with
     * @param to The destination collection
     * @param dep_resolver The dependency resolver to use
     * @param throw_on_missing Throw exceptions if any dependency is missing
     */
    void PopulateTargetDependencySet(Target *pTarget, ulib::list<Target *> &to, TargetDepResolver dep_resolver = {},
                                     bool throw_on_missing = true);

    /**
     * @brief Populates the specified collection with a flat target dependency set without resolving any target
     * dependencies.
     *
     * @param pTarget The root target to start with
     * @param to The destination collection
     */
    void PopulateTargetDependencySetNoResolve(const Target *pTarget, std::vector<const Target *> &to);

    /**
     * @brief The "build environment" responsible for loading and resolving targets.
     */
    class BuildEnv : public ILangLocator, public ITargetLoader
    {
    public:
        /**
         * @brief Construct a new BuildEnv object from a var scope and an output interface.
         *
         * @param scope The root variable scope
         * @param pOut The output interface to use
         */
        BuildEnv(LocalVarScope &scope, IUserOutput *pOut);

        /**
         * @brief Loads _the_ core project target considered the root target of all other targets.
         *
         * @param path The core project target's path
         * @return Target& A reference to the core project target
         */
        Target &LoadCoreProjectTarget(const fs::path &path);

        /**
         * @brief Gets the core project target.
         *
         * @return Target* A pointer to the core project target
         */
        Target *GetCoreTarget();

        /**
         * @brief Loads a "free" target without registering it anywhere.
         *
         * It is up to the caller to further register the target and be responsible about its ownership.
         * This method will use ITargetMiddleware to try loading the target in absense of a `re.yml`.
         *
         * @param path The target path
         * @param ancestor The target's "ancestor" target responsible for it (can be nullptr)
         * @param dep_source The target's "source" dependency which caused it to be loaded (can be nullptr)
         *
         * @return std::unique_ptr<Target> A unique pointer to the loaded target.
         */
        std::unique_ptr<Target> LoadFreeTarget(const fs::path &path, const Target *ancestor = nullptr,
                                               const TargetDependency *dep_source = nullptr);

        /**
         * @brief Loads a root-level target.
         *
         * @param path The target path
         * @return Target& A reference to the loaded target.
         */
        Target &LoadTarget(const fs::path &path);

        /**
         * @brief Registers the specified target to be available as a local dependency.
         *
         * @param pTarget The target to register
         */
        void RegisterLocalTarget(Target *pTarget);

        /**
         * @brief Checks if the specified path contains a loadable Re target, taking into account all the registered
         * target load middlewares.
         *
         * @param path The path to check against
         * @return true A Re target can be loaded from this location.
         * @return false A Re target cannot be loaded from this location.
         */
        bool CanLoadTargetFrom(const fs::path &path);

        ulib::list<Target *> GetSingleTargetDepSet(Target *pTarget);
        ulib::list<Target *> GetSingleTargetLocalDepSet(Target *pTarget);
        ulib::list<Target *> GetTargetsInDependencyOrder();

        void AddLangProvider(ulib::string_view name, ILangProvider *provider);
        ILangProvider *GetLangProvider(ulib::string_view name) override;

        ILangProvider *InitializeTargetLinkEnv(Target *target, NinjaBuildDesc &desc);
        void InitializeTargetLinkEnvWithDeps(Target *target, NinjaBuildDesc &desc);

        void PopulateBuildDesc(Target *target, NinjaBuildDesc &desc);
        void PopulateBuildDescWithDeps(Target *target, NinjaBuildDesc &desc);
        void PopulateFullBuildDesc(NinjaBuildDesc &desc);

        void RunTargetAction(const NinjaBuildDesc *desc, const Target &target, ulib::string_view type,
                             const TargetConfig &data);

        void RunActionsCategorized(Target *target, const NinjaBuildDesc *desc, ulib::string_view run_type);

        void RunAutomaticStructuredTasks(Target *target, const NinjaBuildDesc *desc, ulib::string_view stage);

        void RunStructuredTask(Target *target, const NinjaBuildDesc *desc, ulib::string_view name,
                               ulib::string_view stage);

        void RunStructuredTaskData(Target *target, const NinjaBuildDesc *desc, const TargetConfig &task,
                                   ulib::string_view name, ulib::string_view stage);

        void RunPostBuildActions(Target *target, const NinjaBuildDesc &desc);
        void RunInstallActions(Target *target, const NinjaBuildDesc &desc);

        void AddDepResolver(ulib::string_view name, IDepResolver *resolver);

        void AddTargetFeature(ulib::string_view name, ITargetFeature *feature);
        void AddTargetFeature(ITargetFeature *feature);

        void AddTargetLoadMiddleware(ITargetLoadMiddleware *middleware);

        IDepResolver *GetDepResolver(ulib::string_view name);

        void DebugShowVisualBuildInfo(const Target *pTarget = nullptr, int depth = 0);

        /**
         * @brief Set the DepsVersionCache to use when resolving dependencies.
         *
         * @param cache The cache to use
         */
        void SetDepsVersionCache(DepsVersionCache *cache);

        inline Target *GetTargetOrNull(ulib::string_view module)
        {
            auto it = mTargetMap.find(module);

            if (it != mTargetMap.end())
                return it->second;
            else
                return nullptr;
        }

    private:
        std::unique_ptr<Target> mTheCoreProjectTarget;

        ulib::list<std::unique_ptr<Target>> mRootTargets;
        std::unordered_map<std::string, Target *> mTargetMap;

        std::unordered_map<std::string, ILangProvider *> mLangProviders;
        std::unordered_map<std::string, IDepResolver *> mDepResolvers;
        std::unordered_map<std::string, ITargetFeature *> mTargetFeatures;

        ulib::list<ITargetLoadMiddleware *> mTargetLoadMiddlewares;

        LocalVarScope mVars;
        IUserOutput *mOut;

        DepsVersionCache *mDepsVersionCache = nullptr;

        std::unordered_set<std::string> mCompletedActions;

        void PopulateTargetMap(Target *pTarget);

        void AppendDepsAndSelf(Target *pTarget, ulib::list<Target *> &to, bool throw_on_missing = true,
                               bool use_external = true);

        void RunActionList(const NinjaBuildDesc *desc, Target *target, const TargetConfig &list,
                           ulib::string_view run_type, ulib::string_view default_run_type);

        void InstallPathToTarget(const Target *pTarget, const fs::path &from);

        bool ResolveTargetDependencyImpl(const Target &target, const TargetDependency &dep, ulib::list<Target *> &out,
                                         bool use_external = true);

        void PerformCopyToDependentsImpl(const Target &target, const Target *dependent, const NinjaBuildDesc *desc,
                                         const fs::path &from, ulib::string_view to);
    };
} // namespace re
