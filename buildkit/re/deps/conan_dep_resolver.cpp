#include "conan_dep_resolver.h"

#include <re/process_util.h>
#include <re/target_cfg_utils.h>
#include <re/yaml_merge.h>

#include <fstream>
#include <iostream>

#include <futile/futile.h>
#include <ulib/format.h>

namespace re
{
    Target *ConanDepResolver::ResolveTargetDependency(const Target &target, const TargetDependency &dep,
                                                      DepsVersionCache *cache)
    {
        auto bv_scope = target.GetBuildVarScope();
        auto [scope, context] = bv_scope;

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto cache_path = fmt::format("{}{}{}-{}-{}-{}-{}", dep.name, dep.version_kind_str, dep.version, re_arch,
                                      re_platform, re_config, std::hash<std::string>{}(dep.raw));

        if (dep.extra_config_hash)
            cache_path += fmt::format("-ecfg-{}", dep.extra_config_hash);

        if (auto &cached = mTargetCache[cache_path])
            return cached.get();

        auto cache_name = ".re-cache";
        auto pkg_cached = target.root_path / cache_name / "conan-deps-cache" / cache_path;

        auto build_info_path = pkg_cached / "conanbuildinfo.json";

        if (!fs::exists(build_info_path))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(
                    &target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep.raw);

            auto conan_arch = re_arch;
            auto conan_build_type = re_config;

            if (auto overridden = scope.GetVar("conan-arch-name"))
                conan_arch = *overridden;

            if (auto overridden = scope.GetVar("conan-build-type"))
                conan_build_type = *overridden;

            // fmt::print("conan arch: {}\n", conan_arch);
            // fmt::print("target.name: {}\n", target.name);
            // fmt::print("scope.ResolveLocal: {}\n", scope.ResolveLocal("conan-arch-name"));
            // fmt::print("resolved_config: \n");
            // std::cout << target.resolved_config << std::endl;

            fs::create_directories(pkg_cached);

            // Create the appropriate conanfile.txt
            {
                std::ofstream conanfile{pkg_cached / "conanfile.txt"};

                conanfile << "[requires]\n";

                conanfile << std::string_view{dep.name} << "/";

                if (dep.version_kind == DependencyVersionKind::RawTag)
                {
                    if (dep.version.empty() || dep.version == "latest")
                    {
                        conanfile << "[>=0.0.1]";
                    }
                    else
                    {
                        conanfile << std::string_view{dep.version};
                    }
                }
                else
                {
                    conanfile << "[" << std::string_view{dep.version_kind_str} << std::string_view{dep.version} << "]";
                }

                conanfile << "\n";

                if (dep.filters.size())
                {
                    conanfile << "[options]\n";

                    for (auto &filter : dep.filters)
                    {
                        auto s = scope.Resolve(filter);

                        if (s.find("=") == std::string::npos)
                        {
                            if (s[0] == '!')
                            {
                                s = s.substr(1);
                                s += " = False";
                            }
                            else
                            {
                                s += " = True";
                            }
                        }

                        conanfile << std::string_view{dep.name} << ":" << std::string_view{s} << "\n";
                    }
                }

                conanfile << "\n";
                conanfile << "[generators]\n";
                conanfile << "json";
            }

            fmt::print(fmt::emphasis::bold | fg(fmt::color::plum), "[{}] Restoring package {}...\n\n", target.module,
                       dep.raw);

            auto start_time = std::chrono::high_resolution_clock::now();

            RunProcessOrThrow("conan", {},
                              {"conan", "install", ".", "--build=missing", "-s", fmt::format("arch={}", conan_arch),
                               "-s", fmt::format("arch_build={}", conan_arch), "-s",
                               fmt::format("build_type={}", conan_build_type)},
                              true, true, pkg_cached.u8string());

            /*
            // Throw if the package still isn't there
            if (!fs::exists(path))
            {
                RE_THROW TargetDependencyException(&target, "conan: package not found: {}", dep.ToString());
            }
            */

            auto end_time = std::chrono::high_resolution_clock::now();

            mOut->Info(fmt::emphasis::bold | fg(fmt::color::plum), "\n[{}] Restored package {} ({:.2f}s)\n",
                       target.module, dep.raw,
                       std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f);
        }
        else
        {
            mOut->Info(fmt::emphasis::bold | fg(dep.ns == "conan-dep" ? fmt::color::dim_gray : fmt::color::plum),
                       "[{}] Package {} already available\n", target.module, dep.raw);
        }

        ulib::yaml build_info = ulib::yaml::parse(futile::open(build_info_path).read());

        ulib::string conan_lib_suffix = "";
        if (auto suffix = scope.GetVar("conan-lib-suffix"))
            conan_lib_suffix = *suffix;

        // auto conan_lib_suffix = target.resolved_config["conan-lib-suffix"].scalar();

        auto load_conan_dependency = [this, conan_lib_suffix, &re_arch, &re_platform, &re_config, &bv_scope,
                                      &target](ulib::yaml data) {
            auto [scope, context] = bv_scope;

            auto name = data["name"].scalar();
            auto version = data["version"].scalar();
            auto path = data["rootpath"].scalar();

            auto cache_path = ulib::format("conan-dep.{}_{}-{}-{}-{}", name, version, re_arch, re_platform, re_config);

            if (auto &cached = mTargetCache[cache_path])
                return cached.get();

            ulib::yaml config{ulib::yaml::value_t::map};

            config["name"] = name;
            config["version"] = version;
            config["is-external-dep"] = "true";

            if (auto paths = data.search("include_paths"))
                if (paths->is_sequence())
                    for (auto inc_path : *paths)
                        config["cxx-include-dirs"].push_back(inc_path.scalar());

            if (auto paths = data.search("lib_paths"))
                if (paths->is_sequence())
                    for (auto lib_path : *paths)
                        config["cxx-lib-dirs"].push_back(lib_path.scalar());

            if (auto paths = data.search("bin_paths"))
                if (paths->is_sequence())
                    for (auto bin_path : *paths)
                    {
                        ulib::yaml entry{ulib::yaml::value_t::map};

                        entry["copy-to-deps"]["from"] = bin_path.scalar();
                        entry["copy-to-deps"]["to"] = ".";

                        config["actions"].push_back(entry);
                    }

            if (auto libs = data.search("libs"))
                if (libs->is_sequence())
                    for (auto lib : data["libs"])
                        config["cxx-link-deps"].push_back(lib.scalar() + conan_lib_suffix.ToView());

            // TODO: This will not work properly on Windows!
            if (auto libs = data.search("system_libs"))
                if (libs->is_sequence())
                    for (auto lib : *libs)
                        config["cxx-global-link-deps"].push_back(lib.scalar() + conan_lib_suffix.ToView());

            if (auto defs = data.search("defines"))
                if (defs->is_sequence())
                    for (auto def : *defs)
                        config["cxx-compile-definitions"][def.scalar()] = "1";

            auto dep_target = std::make_unique<Target>(path, cache_path, TargetType::StaticLibrary, config);

            dep_target->config["enabled"] = true;

            dep_target->config["no-auto-include-dirs"] = true;

            dep_target->config["arch"] = re_arch;
            dep_target->config["platform"] = re_platform;
            dep_target->config["configuration"] = re_config;

            dep_target->resolved_config = dep_target->config;

            dep_target->var_parent = target.var_parent;
            dep_target->local_var_ctx = context;
            dep_target->build_var_scope.emplace(&dep_target->local_var_ctx, "build", &scope);

            auto &result = (mTargetCache[cache_path] = std::move(dep_target));
            return result.get();
        };

        ulib::yaml config{ulib::yaml::value_t::map};

        config["is-external-dep"] = "true";

        auto package_target = std::make_unique<Target>(pkg_cached, "conan." + dep.name + "." + dep.version,
                                                       TargetType::StaticLibrary, config);

        for (auto dependency : build_info["dependencies"])
        {
            auto resolved = load_conan_dependency(dependency);

            TargetDependency pkg_dep;

            pkg_dep.ns = "conan-dep";
            pkg_dep.name = resolved->name;
            pkg_dep.resolved = {resolved};

            package_target->dependencies.emplace_back(std::move(pkg_dep));
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

        auto &result = (mTargetCache[cache_path] = std::move(package_target));
        return result.get();

        // conan install . --build=missing -s arch=x86 -s arch_build=x86 -s build_type=Debug
    }

    bool ConanDepResolver::SaveDependencyToPath(const TargetDependency &dep, const fs::path &path)
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
