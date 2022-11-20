#include "vcpkg_dep_resolver.h"

#include <fmt/format.h>
#include <fmt/color.h>

#include <re/process_util.h>

namespace re
{
	Target* VcpkgDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
        if (auto& cached = mTargetCache[dep.name])
            return cached.get();

        auto vcpkg_root = mVcpkgPath;

        if (auto path = target.GetCfgEntry<std::string>("vcpkg-root-path", CfgEntryKind::Recursive))
            vcpkg_root = std::filesystem::path{ *path };

        auto dep_str = dep.ToString();

        if (!std::filesystem::exists(vcpkg_root))
        {
            fmt::print(
                fmt::emphasis::bold | fg(fmt::color::light_sea_green),
                "[{}] [vcpkg] Target dependency '{}' needs vcpkg. Installing...\n\n",
                target.module,
                dep_str
            );

            std::filesystem::create_directories(vcpkg_root);

            // Try cloning it to Git.
            RunProcessOrThrow(
                "git",
                {
                    "git",
                    "clone",
                    "https://github.com/microsoft/vcpkg.git",
                    vcpkg_root.string()
                },
                true,
                true
            );

            std::system((vcpkg_root / "bootstrap-vcpkg").string().c_str());

            /*
            RunProcessOrThrow(
                "bootstrap-vcpkg",
                {

                }
            );
            */
        }

        auto re_arch = std::getenv("RE_ARCH");
        auto re_platform = std::getenv("RE_PLATFORM");

        auto path = vcpkg_root / "packages" / (dep.name + fmt::format("_{}-{}", re_arch, re_platform));

        // We optimize the lookup by not invoking vcpkg at all if the package is already there.
        if (!std::filesystem::exists(path))
        {
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
                    (vcpkg_root / "vcpkg").string(),
                    "install",
                    fmt::format("{}:{}-{}", dep.name, re_arch, re_platform)
                },
                true, true
            );

            // Throw if the package still isn't there
            if (!std::filesystem::exists(path))
            {
                throw TargetLoadException("vcpkg: package not found: " + dep.name);
            }

            auto end_time = std::chrono::high_resolution_clock::now();

            fmt::print(
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
                fmt::print(
                    fmt::emphasis::bold | fg(fmt::color::light_green),
                    "[{}] Package {} already available\n",
                    target.module,
                    dep_str
                );
            }
        }

        YAML::Node config{ YAML::NodeType::Map };

        if (std::filesystem::exists(path / "include"))
        {
            config["cxx-include-dirs"].push_back((path / "include").string());
        }

        if (std::filesystem::exists(path / "lib"))
        {
            for (auto& file : std::filesystem::directory_iterator{ path / "lib" })
            {
                if (file.is_regular_file())
                    config["cxx-link-deps"].push_back(file.path().string());
            }
        }

        if (std::filesystem::exists(path / "bin"))
        {
            YAML::Node entry{ YAML::NodeType::Map };

            entry["copy-to-deps"]["from"] = "./bin";
            entry["copy-to-deps"]["to"] = ".";

            config["actions"].push_back(entry);
        }

        auto package_target = std::make_unique<Target>(path.string(), "vcpkg." + dep.name, TargetType::StaticLibrary, config);

        YAML::Node vcpkg_json = YAML::LoadFile((vcpkg_root / "ports" / dep.name / "vcpkg.json").string());

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

                dep.resolved = ResolveTargetDependency(target, dep);

                package_target->dependencies.emplace_back(std::move(dep));
            }
        }

        /*
        package_target->config["name"] = package_target->name;
        package_target->config["type"] = TargetTypeToString(package_target->type);

        for (auto& dep : package_target->dependencies)
        {
            package_target->config["deps"].push_back(fmt::format("{}:{}", dep.ns, dep.name));
        }

        YAML::Emitter emitter;
        emitter << package_target->config;

        std::ofstream of{ "debug/vcpkg-packages/" + dep.name + ".yml" };
        of << emitter.c_str();
        */

        auto& result = (mTargetCache[dep.name] = std::move(package_target));
        return result.get();
	}
}
