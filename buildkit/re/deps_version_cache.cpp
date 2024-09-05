#include "deps_version_cache.h"

namespace re
{
    ulib::string DepsVersionCache::GetLatestVersionMatchingRequirements(
        const Target &target, const TargetDependency &dep, ulib::string_view name,
        std::function<std::vector<std::string>(const re::TargetDependency &, const std::string&)> get_available_versions)
    {
        if (dep.version_kind == DependencyVersionKind::RawTag)
            return dep.version;

        ulib::string kind_str = "@";

        switch (dep.version_kind)
        {
        case DependencyVersionKind::Equal:
            kind_str = "==";
            break;
        case DependencyVersionKind::Greater:
            kind_str = "<";
            break;
        case DependencyVersionKind::GreaterEqual:
            kind_str = ">=";
            break;
        case DependencyVersionKind::Less:
            kind_str = "<";
            break;
        case DependencyVersionKind::LessEqual:
            kind_str = "<=";
            break;
        case DependencyVersionKind::SameMinor:
            kind_str = "~";
            break;
        case DependencyVersionKind::SameMajor:
            kind_str = "^";
            break;
        };

        auto existing_key = ulib::format("{}:{}{}{}", dep.ns, dep.name, kind_str, dep.version);

        auto &existing = mData[existing_key];

        if (!existing.is_null())
            return existing.get<std::string>();

        auto available = get_available_versions(dep, name);

        if (available.empty())
            RE_THROW TargetDependencyException(&target, "no versions for '{}'", dep.raw);

        auto erase_cond_negated = [](auto &cont, const auto &value, const auto &pred) {
            cont.erase(std::remove_if(cont.begin(), cont.end(),
                                      [&pred, &value](const auto &x) {
                                          try
                                          {
                                              return !(pred(semverpp::version{std::string(x)}, value));
                                          }
                                          catch (semverpp::invalid_version)
                                          {
                                              return true;
                                          }
                                      }),
                       cont.end());
        };

        /*
                fmt::print("\n # result_all: {} -> [\n", dep.raw);

                for (auto &ver : available)
                    fmt::print("    {} @ {}\n", ver, semverpp::version{ver}.string());

                fmt::print("]; dep.version_sv={}\n", dep.version_sv.string());
                */

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
            erase_cond_negated(available, dep.version_sv, [](const semverpp::version &a, const semverpp::version &b) {
                return a >= b && a.major == b.major && a.minor == b.minor;
            });
            break;
        case DependencyVersionKind::SameMajor:
            erase_cond_negated(available, dep.version_sv, [](const semverpp::version &a, const semverpp::version &b) {
                return a >= b && a.major == b.major;
            });
            break;
        };

        if (available.empty())
            RE_THROW TargetDependencyException(&target, "no matching versions for '{}'", dep.raw);

        std::sort(available.begin(), available.end(), [](const std::string &a, const std::string &b) {
            return semverpp::version{a} > semverpp::version{b};
        });

        const auto &version = available.at(0);
        existing = version;

        fmt::print("\n # result: {} -> [\n", dep.raw);

        for (auto &ver : available)
            fmt::print("    {} @ {}\n", ver, semverpp::version{std::string(ver)}.string());

        fmt::print("]\n");

        return version;
    }
} // namespace re
