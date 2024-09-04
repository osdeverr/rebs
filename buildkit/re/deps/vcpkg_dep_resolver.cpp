#include "vcpkg_dep_resolver.h"

#include <fmt/color.h>
#include <fmt/format.h>

#include <re/process_util.h>
#include <re/target_cfg_utils.h>
#include <re/yaml_merge.h>

#include <fstream>
#include <futile/futile.h>

namespace re
{
    Target *VcpkgDepResolver::ResolveTargetDependency(const Target &target, const TargetDependency &dep,
                                                      DepsVersionCache *cache)
    {
        auto [scope, context] = target.GetBuildVarScope();

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto lib_type = scope.ResolveLocal("vcpkg-library-type");

        auto at_prefix = (dep.version.size() && dep.ns == "vcpkg") ? fmt::format("-{}", dep.version) : "";
        // fmt::print(" / dbg - ns='{}' atp='{}'\n", dep.ns, at_prefix);

        if (!lib_type.empty() && lib_type != "dynamic")
        {
            at_prefix = fmt::format("-{}", lib_type) + at_prefix;
        }

        auto cache_path = fmt::format("{}{}-{}-{}-{}", dep.name, at_prefix, re_arch, re_platform, re_config);

        if (dep.extra_config_hash)
            cache_path += fmt::format("-ecfg-{}", dep.extra_config_hash);

        if (auto &cached = mTargetCache[cache_path])
            return cached.get();

        auto vcpkg_root = mVcpkgPath;

        if (auto path = scope.GetVar("vcpkg-root-path"))
            vcpkg_root = fs::path{*path};

        auto dep_str = dep.ToString();

        if (!fs::exists(vcpkg_root / ".git"))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(
                    &target, "Cannot auto-download vcpkg - autoloading is disabled", dep_str);

            mOut->Info(fmt::emphasis::bold | fg(fmt::color::light_sea_green),
                       "[{}] [vcpkg] Target dependency '{}' needs vcpkg. Installing...\n\n", target.module, dep_str);

            fs::create_directories(vcpkg_root);

            // Try cloning it to Git.
            RunProcessOrThrow("git", {},
                              {"git", "clone", "https://github.com/microsoft/vcpkg.git", vcpkg_root.u8string()}, true,
                              true);

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

        auto path = vcpkg_root / "packages" / (dep.name + fmt::format("_{}-{}{}", re_arch, re_platform, at_prefix));

        // We optimize the lookup by not invoking vcpkg at all if the package is already there.
        if (!fs::exists(path / "BUILD_INFO"))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(
                    &target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep_str);

            fmt::print(fmt::emphasis::bold | fg(fmt::color::light_green), "[{}] Restoring package {}...\n\n",
                       target.module, dep_str);

            auto start_time = std::chrono::high_resolution_clock::now();

            std::string vcpkg_name = "vcpkg";

            if (re_platform == "windows")
                vcpkg_name += ".exe";

            RunProcessOrThrow("vcpkg", vcpkg_root / vcpkg_name,
                              {"install", fmt::format("{}:{}-{}{}", dep.name, re_arch, re_platform, at_prefix)}, true,
                              true);

            // Throw if the package still isn't there
            if (!fs::exists(path))
            {
                RE_THROW TargetDependencyException(&target, "vcpkg: package not found: {}", dep.ToString());
            }

            auto end_time = std::chrono::high_resolution_clock::now();

            mOut->Info(fmt::emphasis::bold | fg(fmt::color::light_green), "\n[{}] Restored package {} ({:.2f}s)\n",
                       target.module, dep_str,
                       std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f);
        }
        else
        {
            mOut->Info(fmt::emphasis::bold | fg(dep.ns == "vcpkg-dep" ? fmt::color::dim_gray : fmt::color::light_green),
                       "[{}] Package {} already available\n", target.module, dep_str);
        }

        ulib::yaml config{ulib::yaml::value_t::map};

        if (fs::exists(path / "include"))
        {
            config["cxx-include-dirs"].push_back((path / "include").u8string());
        }

        config["is-external-dep"] = "true";

        // Support custom configurations
        if (fs::exists(path / re_config))
            path /= re_config;

        if (fs::exists(path / "lib"))
        {
            for (auto &file : fs::directory_iterator{path / "lib"})
            {
                if (file.is_regular_file() && file.path().extension() != ".pdb")
                    config["cxx-link-deps"].push_back(file.path().u8string());
            }
        }

        if (fs::exists(path / "bin"))
        {
            ulib::yaml entry{ulib::yaml::value_t::map};

            entry["copy-to-deps"]["from"] = (path / "bin").u8string();
            entry["copy-to-deps"]["to"] = ".";

            config["actions"].push_back(entry);
        }

        auto package_target =
            std::make_unique<Target>(path.u8string(), "vcpkg." + cache_path, TargetType::StaticLibrary, config);

        ulib::yaml vcpkg_json = ulib::yaml::parse(futile::open(vcpkg_root / "ports" / dep.name / "vcpkg.json").read());

        auto append_deps_from = [this, &dep, &target, &package_target, &re_platform, cache](ulib::yaml json) {
            if (auto deps = json.search("dependencies"))
            {
                for (auto &vcdep : *deps)
                {
                    TargetDependency pkg_dep;

                    pkg_dep.ns = "vcpkg-dep";

                    if (vcdep.is_map())
                    {
                        pkg_dep.name = vcdep["name"].scalar();

                        if (auto ver = vcdep.search("version"))
                            pkg_dep.version = ver->scalar();
                        else if (auto ver = vcdep.search("version>="))
                            pkg_dep.version = ulib::string{">="} + ver->scalar();
                        else if (auto ver = vcdep.search("version>"))
                            pkg_dep.version = ulib::string{">"} + ver->scalar();
                        else if (auto ver = vcdep.search("version<="))
                            pkg_dep.version = ulib::string{"<="} + ver->scalar();
                        else if (auto ver = vcdep.search("version<"))
                            pkg_dep.version = ulib::string{"<"} + ver->scalar();

                        if (auto platform = vcdep.search("platform"))
                        {
                            auto eligible = true;
                            auto str = platform->scalar();

                            if (str.front() == '!')
                            {
                                if (str.substr(1) == re_platform)
                                    eligible = false;
                            }
                            else if (str != re_platform)
                            {
                                eligible = false;
                            }

                            if (!eligible)
                            {
                                // fmt::print(" ! EVIL dep {} has wrong platform '{}'\n", pkg_dep.name,
                                // platform.scalar());
                                continue;
                            }
                        }
                    }
                    else
                    {
                        pkg_dep.name = vcdep.scalar();
                    }

                    pkg_dep.raw = fmt::format("{}:{}@{}", pkg_dep.ns, pkg_dep.name, pkg_dep.version);

                    // Fixes rare stack overflow bugs
                    if (pkg_dep.name == dep.name)
                    {
                        // fmt::print(" ! EVIL dep {} refers to ITSELF\n", pkg_dep.name);
                        continue;
                    }

                    pkg_dep.resolved = {ResolveTargetDependency(target, pkg_dep, cache)};

                    package_target->dependencies.emplace_back(std::move(pkg_dep));
                }
            }
        };

        append_deps_from(vcpkg_json);

        if (auto features = vcpkg_json.search("default-features"))
            if (features->is_sequence())
                for (auto feature : *features)
                {
                    append_deps_from(vcpkg_json["features"][feature.scalar()]);
                }

        package_target->root_path = target.root_path;

        package_target->config["enabled"] = true;

        package_target->config["no-auto-include-dirs"] = true;

        package_target->config["arch"] = re_arch;
        package_target->config["platform"] = re_platform;
        package_target->config["configuration"] = re_config;

        if (!dep.extra_config.is_null())
            MergeYamlNode(package_target->config, dep.extra_config);

        package_target->resolved_config = package_target->config;

        package_target->var_parent = target.var_parent;
        package_target->local_var_ctx = context;
        package_target->build_var_scope.emplace(&package_target->local_var_ctx, "build", &scope);

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

        auto &result = (mTargetCache[cache_path] = std::move(package_target));
        return result.get();
    }

    bool VcpkgDepResolver::SaveDependencyToPath(const TargetDependency &dep, const fs::path &path)
    {
        ulib::yaml config;

        config["type"] = "project";
        config["name"] = dep.name;

        config["deps"] = ulib::yaml{ulib::yaml::value_t::sequence};
        config["deps"].push_back(dep.ToString());

        fs::create_directories(path);
        futile::open(path / "re.yml", "w").write(config.dump());

        return true;
    }
} // namespace re
