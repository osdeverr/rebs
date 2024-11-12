#include "re_package_dep_resolver.h"

#include <re/deps_version_cache.h>
#include <re/target.h>

#include <magic_enum.hpp>

namespace re
{
    Target *RePackageDepResolver::ResolveTargetDependency(const Target &target, const TargetDependency &dep,
                                                          DepsVersionCache *cache)
    {
        try
        {
            auto package = mClient.FindPackage(dep.name);

            if (!package)
            {
                RE_THROW TargetDependencyException(&target, "{}: package not found", dep.ToString());
            }

            if (package->type == RePackagePublishType::External)
            {
                auto hosted_at = ParseTargetDependency(package->hosted_at);

                if (auto resolver = mLoader->GetDepResolver(hosted_at.ns))
                {
                    auto dep_new = dep;

                    dep_new.ns = hosted_at.ns;
                    dep_new.name = hosted_at.name;

                    return resolver->ResolveTargetDependency(target, dep_new, cache);
                }
                else
                {
                    RE_THROW TargetDependencyException(
                        &target, "{}: externally hosted package {} requires missing resolver '{}'", dep.ToString(),
                        dep.ToString(), hosted_at.ns);
                }
            }
            else if (package->type == RePackagePublishType::Hosted)
            {
                auto latest_version = cache->GetLatestVersionMatchingRequirements(
                    target, dep, "", [&package](const auto &, auto) -> std::vector<std::string> {
                        std::vector<std::string> result;

                        for (auto &[tag, _] : package->versions)
                            result.push_back(tag);

                        return result;
                    });

                auto &version = package->versions.at(latest_version);

                version.link; // :)
            }
            else
            {
                RE_THROW TargetDependencyException(&target,
                                                   "{}: failed to load package - unsupported package publish type .{}",
                                                   dep.ToString(), magic_enum::enum_name(package->type));
            }
        }
        catch (const RePackageClientException &e)
        {
            RE_THROW TargetDependencyException(&target, "{}: {}", dep.ToString(), e.what());
        }

        /*
        auto cached_dir = fmt::format("git.{}.{}@{}", dep.ns, dep.name, branch);

        auto [scope, context] = target.GetBuildVarScope();

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto triplet = fmt::format("{}-{}-{}", re_arch, re_platform, re_config);

        if (dep.extra_config_hash)
            triplet += fmt::format("-ecfg-{}", dep.extra_config_hash);
        */
    }
} // namespace re
