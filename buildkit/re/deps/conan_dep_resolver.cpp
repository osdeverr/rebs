#include "conan_dep_resolver.h"

#include <re/process_util.h>
#include <re/yaml_merge.h>
#include <re/target_cfg_utils.h>

#include <fstream>

namespace re
{
    Target* ConanDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep, DepsVersionCache* cache)
    {
        auto [scope, context] = target.GetBuildVarScope();

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto cache_path = fmt::format("{}{}{}-{}-{}-{}", dep.name, dep.version_kind_str, dep.version, re_arch, re_platform, re_config);

		if (dep.extra_config_hash)
			cache_path += fmt::format("-ecfg-{}", dep.extra_config_hash);

        if (auto& cached = mTargetCache[cache_path])
            return cached.get();

		auto cache_name = ".re-cache";
		auto pkg_cached = target.root_path / cache_name / cache_path;

        auto build_info_path = pkg_cached / "conanbuildinfo.json";

        if (!fs::exists(build_info_path))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(&target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep.raw);

            auto conan_arch = re_arch;
            auto conan_build_type = re_config;

            if (auto overridden = target.resolved_config["conan-arch-name"])
                conan_arch = overridden.Scalar();

            if (auto overridden = target.resolved_config["conan-build-type"])
                conan_build_type = overridden.Scalar();

            fs::create_directories(pkg_cached);

            // Create the appropriate conanfile.txt
            {
                std::ofstream conanfile{pkg_cached / "conanfile.txt"};

                conanfile << "[requires]\n";

                conanfile << dep.name << "/";

                if (dep.version_kind == DependencyVersionKind::RawTag)
                {
                    if (dep.version.empty() || dep.version == "latest")
                    {
                        conanfile << "[>=0.0.1]";
                    }
                    else
                    {
                        conanfile << dep.version;
                    }
                }
                else
                {
                    conanfile << "[" << dep.version_kind_str << dep.version << "]";
                }

                conanfile << "\n";
                conanfile << "[generators]\n";
                conanfile << "json";
            }

            fmt::print(
                fmt::emphasis::bold | fg(fmt::color::plum),
                "[{}] Restoring package {}...\n\n",
                target.module,
                dep.raw
            );

            auto start_time = std::chrono::high_resolution_clock::now();

            RunProcessOrThrow(
                "conan",
                "C:/Users/krasi/AppData/Local/Packages/PythonSoftwareFoundation.Python.3.9_qbz5n2kfra8p0/LocalCache/local-packages/Python39/Scripts/conan.exe",
                {
                    "install",
                    ".",
                    "--build=missing",
                    "-s",
                    fmt::format("arch={}", conan_arch),
                    "-s",
                    fmt::format("arch_build={}", conan_arch),
                    "-s",
                    fmt::format("build_type={}", conan_build_type)
                },
                true, true,
                pkg_cached.u8string()
            );

            /*
            // Throw if the package still isn't there
            if (!fs::exists(path))
            {
                RE_THROW TargetDependencyException(&target, "conan: package not found: {}", dep.ToString());
            }
            */

            auto end_time = std::chrono::high_resolution_clock::now();

            mOut->Info(
                fmt::emphasis::bold | fg(fmt::color::plum),
                "\n[{}] Restored package {} ({:.2f}s)\n",
                target.module,
                dep.raw,
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f
            );
        }
        else
        {
            mOut->Info(
                fmt::emphasis::bold | fg(dep.ns == "conan-dep" ? fmt::color::dim_gray : fmt::color::plum),
                "[{}] Package {} already available\n",
                target.module,
                dep.raw
            );
        }

        std::ifstream build_info_file{build_info_path};

        YAML::Node build_info = YAML::Load(build_info_file);

        auto conan_lib_suffix = target.resolved_config["conan-lib-suffix"].Scalar();

        auto load_conan_dependency = [this, conan_lib_suffix, &re_arch, &re_platform, &re_config](YAML::Node data)
        {
            auto name = data["name"].Scalar();
            auto version = data["version"].Scalar();
            auto path = data["rootpath"].Scalar();

            auto cache_path = fmt::format("conan-dep.{}_{}-{}-{}-{}", name, version, re_arch, re_platform, re_config);

            if (auto& cached = mTargetCache[cache_path])
                return cached.get();

            YAML::Node config{ YAML::NodeType::Map };

            config["name"] = name;
            config["version"] = version;

            for (auto inc_path : data["include_paths"])
                config["cxx-include-dirs"].push_back(inc_path.Scalar());

            for (auto lib_path : data["lib_paths"])
                config["cxx-lib-dirs"].push_back(lib_path.Scalar());
                
            for (auto lib : data["libs"])
                config["cxx-link-deps"].push_back(lib.Scalar() + conan_lib_suffix);
                
            // TODO: This will not work properly on Windows!
            for (auto lib : data["system_libs"])
                config["cxx-global-link-deps"].push_back(lib.Scalar() + conan_lib_suffix);
                
            for (auto def : data["defines"])
                config["cxx-compile-definitions"][def.Scalar()] = "1";

            auto dep_target = std::make_unique<Target>(path, cache_path, TargetType::StaticLibrary, config);
            
            dep_target->config["enabled"] = true;
    
            dep_target->config["no-auto-include-dirs"] = true;
    
            dep_target->config["arch"] = re_arch;
            dep_target->config["platform"] = re_platform;
            dep_target->config["configuration"] = re_config;
    
            dep_target->resolved_config = dep_target->config;

            auto& result = (mTargetCache[cache_path] = std::move(dep_target));
            return result.get();
        };

        YAML::Node config{ YAML::NodeType::Map };
        
        auto package_target = std::make_unique<Target>(pkg_cached, "conan." + dep.name + "." + dep.version, TargetType::StaticLibrary, config);

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

		if (dep.extra_config)
			MergeYamlNode(package_target->config, dep.extra_config);

        package_target->resolved_config = package_target->config;

        package_target->var_parent = target.var_parent;
        package_target->local_var_ctx = context;
        package_target->build_var_scope.emplace(&package_target->local_var_ctx, "build", &scope);

        auto& result = (mTargetCache[cache_path] = std::move(package_target));
        return result.get();

        // conan install . --build=missing -s arch=x86 -s arch_build=x86 -s build_type=Debug
    }
        
    bool ConanDepResolver::SaveDependencyToPath(const TargetDependency& dep, const fs::path& path)
    {
        YAML::Node config;
        
        config["type"] = "project";
        config["name"] = dep.name;

        config["deps"] = YAML::Node{YAML::NodeType::Sequence};
        config["deps"].push_back(dep.ToString());

        YAML::Emitter emitter;
        emitter << config;

        fs::create_directories(path);

        std::ofstream of{path / "re.yml"};
        of << emitter.c_str();

        return true;
    }
}
