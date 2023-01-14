#include "deps_version_cache.h"

namespace re
{
    std::string DepsVersionCache::GetLatestVersionMatchingRequirements(
        const Target& target,
        const TargetDependency& dep,
        std::function<std::vector<std::string>(const TargetDependency&)> get_available_versions
    )
    {
        if (dep.version_kind == DependencyVersionKind::RawTag)
            return dep.version;

        auto& existing = mData[dep.raw];

        if (!existing.is_null())
            return existing.get<std::string>();

        auto available = get_available_versions(dep);

        if (available.empty())
            RE_THROW TargetDependencyException(&target, "no versions for '{}'", dep.raw);

        auto erase_cond_negated = [](auto& cont, const auto& value, const auto& pred)
        {
            cont.erase(std::remove_if(
                cont.begin(),
                cont.end(),
                [&pred, &value] (const auto& x)
                {
                    try
                    {
                        return !(pred(semverpp::version{x}, value));
                    }
                    catch (semverpp::invalid_version)
                    {
                        return true;
                    }
                }
            ), cont.end());
        };

        switch (dep.version_kind)
        {
        case DependencyVersionKind::Equal:
            erase_cond_negated(available, dep.version_sv, std::equal_to<semverpp::version>{});
            break;
        case DependencyVersionKind::Greater:
            erase_cond_negated(available, dep.version_sv, std::greater<semverpp::version>{});
            break;
        case DependencyVersionKind::GreaterEqual:
            erase_cond_negated(available, dep.version_sv, std::greater_equal<semverpp::version>{});
            break;
        case DependencyVersionKind::Less:
            erase_cond_negated(available, dep.version_sv, std::less<semverpp::version>{});
            break;
        case DependencyVersionKind::LessEqual:
            erase_cond_negated(available, dep.version_sv, std::less_equal<semverpp::version>{});
            break;
        case DependencyVersionKind::SameMinor:
            erase_cond_negated(
                available,
                dep.version_sv,
                [] (const semverpp::version& a, const semverpp::version& b)
                {
                    return a >= b && a.major == b.major && a.minor == b.minor;
                }
            );
            break;
        case DependencyVersionKind::SameMajor:
            erase_cond_negated(
                available,
                dep.version_sv,
                [] (const semverpp::version& a, const semverpp::version& b)
                {
                    return a >= b && a.major == b.major;
                }
            );
            break;
        };

        if (available.empty())
            RE_THROW TargetDependencyException(&target, "no matching versions for '{}'", dep.raw);

        std::sort(
            available.begin(), available.end(),
            [] (const std::string& a, const std::string& b)
            {
                return semverpp::version{a} > semverpp::version{b};
            });
        
        const auto& version = available.at(0);
        existing = version;

        fmt::print("\n # result: {} -> [\n", dep.raw);

        for (auto& ver : available)
            fmt::print("    {}\n", ver);

        fmt::print("]\n");
        
        return version;
    }
}
