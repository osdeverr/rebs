#include "vcpkg_dep_resolver.h"

#include <fmt/format.h>
#include <fmt/color.h>

#include <re/process_util.h>
#include <re/target_cfg_utils.h>

#include <fstream>

namespace re
{
	Target* VcpkgDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
        auto [scope, context] = target.GetBuildVarScope();

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto cache_path = fmt::format("{}-{}-{}-{}", dep.name, re_arch, re_platform, re_config);

        if (auto& cached = mTargetCache[cache_path])
            return cached.get();

        auto vcpkg_root = mVcpkgPath;

        if (auto path = target.GetCfgEntry<std::string>("vcpkg-root-path", CfgEntryKind::Recursive))
            vcpkg_root = fs::path{ *path };

        auto dep_str = dep.ToString();

        if (!fs::exists(vcpkg_root))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(&target, "Cannot auto-download vcpkg - autoloading is disabled", dep_str);

            mOut->Info(
                fmt::emphasis::bold | fg(fmt::color::light_sea_green),
                "[{}] [vcpkg] Target dependency '{}' needs vcpkg. Installing...\n\n",
                target.module,
                dep_str
            );

            fs::create_directories(vcpkg_root);

            // Try cloning it to Git.
            RunProcessOrThrow(
                "git",
                {
                    "git",
                    "clone",
                    "https://github.com/microsoft/vcpkg.git",
                    vcpkg_root.u8string()
                },
                true,
                true
            );

#ifdef WIN32
            std::system((vcpkg_root / "bootstrap-vcpkg").string().c_str());
#else
            std::system((vcpkg_root / "bootstrap-vcpkg.sh").string().c_str());
#endif

            /*
            RunProcessOrThrow(
                "bootstrap-vcpkg",
                {

                }
            );
            */
        }

        auto path = vcpkg_root / "packages" / (dep.name + fmt::format("_{}-{}", re_arch, re_platform));

        // We optimize the lookup by not invoking vcpkg at all if the package is already there.
        if (!fs::exists(path / "BUILD_INFO"))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(&target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep_str);

            fmt::print(
                fmt::emphasis::bold | fg(fmt::color::light_green),
                "[{}] Restoring package {}...\n\n",
                target.module,
                dep_str
            );

            auto start_time = std::chrono::high_resolution_clock::now();

            RunProcessOrThrow(
                "vcpkg",
                {
                    (vcpkg_root / "vcpkg").u8string(),
                    "install",
                    fmt::format("{}:{}-{}", dep.name, re_arch, re_platform)
                },
                true, true
            );

            // Throw if the package still isn't there
            if (!fs::exists(path))
            {
                RE_THROW TargetDependencyException(&target, "vcpkg: package not found: {}", dep.ToString());
            }

            auto end_time = std::chrono::high_resolution_clock::now();

            mOut->Info(
                fmt::emphasis::bold | fg(fmt::color::light_green),
                "\n[{}] Restored package {} ({:.2f}s)\n",
                target.module,
                dep_str,
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f
            );
        }
        else
        {
            // vcpkg-dep dependencies are created automatically within this method itself - no need to spam the console
            if (dep.ns != "vcpkg-dep")
            {
                mOut->Info(
                    fmt::emphasis::bold | fg(fmt::color::light_green),
                    "[{}] Package {} already available\n",
                    target.module,
                    dep_str
                );
            }
        }

        YAML::Node config{ YAML::NodeType::Map };

        if (fs::exists(path / "include"))
        {
            config["cxx-include-dirs"].push_back((path / "include").u8string());
        }

        // Support custom configurations
        if (fs::exists(path / re_config))
            path /= re_config;

        if (fs::exists(path / "lib"))
        {
            for (auto& file : fs::directory_iterator{ path / "lib" })
            {
                if (file.is_regular_file())
                    config["cxx-link-deps"].push_back(file.path().u8string());
            }
        }

        if (fs::exists(path / "bin"))
        {
            YAML::Node entry{ YAML::NodeType::Map };

            entry["copy-to-deps"]["from"] = "./bin";
            entry["copy-to-deps"]["to"] = ".";

            config["actions"].push_back(entry);
        }

        auto package_target = std::make_unique<Target>(path.u8string(), "vcpkg." + dep.name, TargetType::StaticLibrary, config);

        YAML::Node vcpkg_json = YAML::LoadFile((vcpkg_root / "ports" / dep.name / "vcpkg.json").u8string());

        if (auto deps = vcpkg_json["dependencies"])
        {
            for (const auto& vcdep : deps)
            {
                TargetDependency dep;

                dep.ns = "vcpkg-dep";

                if (vcdep.IsMap())
                {
                    dep.name = vcdep["name"].as<std::string>();

                    if (auto ver = vcdep["version"])
                        dep.version = ver.as<std::string>();
                    else if (auto ver = vcdep["version>="])
                        dep.version = ">=" + ver.as<std::string>();
                    else if (auto ver = vcdep["version>"])
                        dep.version = ">" + ver.as<std::string>();
                    else if (auto ver = vcdep["version<="])
                        dep.version = "<=" + ver.as<std::string>();
                    else if (auto ver = vcdep["version<"])
                        dep.version = "<" + ver.as<std::string>();
                }
                else
                {
                    dep.name = vcdep.as<std::string>();
                }

                dep.resolved = { ResolveTargetDependency(target, dep) };

                package_target->dependencies.emplace_back(std::move(dep));
            }
        }

        package_target->root_path = target.root_path;

        package_target->config["enabled"] = true;

        package_target->config["no-auto-include-dirs"] = true;

        package_target->config["arch"] = re_arch;
        package_target->config["platform"] = re_platform;
        package_target->config["configuration"] = re_config;

        package_target->var_parent = target.var_parent;
        package_target->local_var_ctx = context;
        package_target->build_var_scope.emplace(&package_target->local_var_ctx, "build", &scope);

        package_target->resolved_config = GetResolvedTargetCfg(*package_target, {
            { "arch", re_arch },
            { "platform", re_platform },
            { "config", re_config }
        });

        package_target->LoadConditionalDependencies();

        /*
        package_target->config["name"] = package_target->name;
        package_target->config["type"] = TargetTypeToString(package_target->type);

        for (auto& dep : package_target->dependencies)
        {
            package_target->config["deps"].push_back(fmt::format("{}:{}", dep.ns, dep.name));
        }

        YAML::Emitter emitter;
        emitter << package_target->config;

        std::ofstream of{ "debug/vcpkg-packages/" + cache_path + ".yml" };
        of << emitter.c_str();
        */

        auto& result = (mTargetCache[cache_path] = std::move(package_target));
        return result.get();
	}
}
