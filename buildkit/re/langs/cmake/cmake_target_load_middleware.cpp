#include "cmake_target_load_middleware.h"

#include <re/process_util.h>

#include <fstream>

namespace re
{
    bool CMakeTargetLoadMiddleware::SupportsTargetLoadPath(const fs::path& path)
    {
        //
        // In case a re.yml exists in the directory, it can be assumed the middleware isn't needed
        // as the project is already designed with Re build support in mind.
        //

        return fs::exists(path / "CMakeLists.txt") && !fs::exists(path / "re.yml");
    }

    std::unique_ptr<Target> CMakeTargetLoadMiddleware::LoadTargetWithMiddleware(const fs::path& path, const Target* ancestor)
    {
        auto canonical_path = fs::canonical(path);

        std::string arch = "default", config = "Debug", platform = "host";
        
        auto out_dir = canonical_path / "out";

        auto path_hash = std::hash<std::string>{}(canonical_path.generic_u8string());

        std::string compiler_override = "";
        std::string link_flags = "";
        std::string cxx_std_override = "";
        
        if (ancestor)
        {
		    auto [scope, context] = ancestor->GetBuildVarScope();

            arch = scope.ResolveLocal("arch");
            config = scope.ResolveLocal("configuration");
            platform = scope.ResolveLocal("platform");

            out_dir = ancestor->root->path / ".re-cache" / "cmake-gen" / fmt::format("{}-{}-{}-{}", path_hash, arch, config, platform);
            
            if (auto compiler = scope.GetVar("cxx.tool.compiler"))
                compiler_override = fs::path{*compiler}.generic_u8string();

            if (auto flags = scope.GetVar("platform-link-paths"))
                link_flags += scope.Resolve(*flags);

            if (auto standard = ancestor->resolved_config["cxx-standard"])
                cxx_std_override = standard.Scalar();

            config = ancestor->resolved_config["cmake-build-type"].Scalar();
        }

        // HACK: PLEASE add something better later
        if (cxx_std_override == "latest")
            cxx_std_override = "20";

        auto bin_dir = out_dir / "build";

        fs::create_directories(bin_dir);

        mOut->Info(
            fg(fmt::color::cornflower_blue) | fmt::emphasis::bold,
            "\n * CMakeTargetLoadMiddleware: Loading CMake project target from '{}'\n", path.generic_u8string()
        );

        auto meta_path = out_dir / "re-cmake-meta.yml";

        if (!fs::exists(meta_path) || (ancestor && ancestor->build_var_scope->GetVar("cmake-reconfigure").value_or("false") == "true"))
        {
            std::vector<std::string> cmdline = {
                "cmake",
                "-G", "Ninja"
            };

            cmdline.emplace_back("-DRE_ORIGINAL_CMAKE_DIR=" + canonical_path.generic_u8string());
            cmdline.emplace_back("-DRE_BIN_OUT_DIR=" + bin_dir.generic_u8string());
            cmdline.emplace_back("-DRE_ADAPTED_META_FILE=" + meta_path.generic_u8string());
            cmdline.emplace_back("-DCMAKE_BUILD_TYPE=" + config);

            cmdline.emplace_back("-DCMAKE_C_COMPILER_WORKS=1");
            cmdline.emplace_back("-DCMAKE_CXX_COMPILER_WORKS=1");

            if (compiler_override.size())
            {
                cmdline.emplace_back("-DCMAKE_C_COMPILER=" + compiler_override);
                cmdline.emplace_back("-DCMAKE_CXX_COMPILER=" + compiler_override);
            }

            if (link_flags.size())
            {
                cmdline.emplace_back("-DRE_CUSTOM_LINK_OPTS=" + link_flags);
                cmdline.emplace_back("-DCMAKE_EXE_LINKER_FLAGS=" + link_flags);
                cmdline.emplace_back("-DLINKER_FLAGS=" + link_flags);
                cmdline.emplace_back("-DLINK_FLAGS=" + link_flags);
            }

            if (cxx_std_override.size())
                cmdline.emplace_back("-DCMAKE_CXX_STANDARD=" + cxx_std_override);

            cmdline.emplace_back("-B");
            cmdline.emplace_back(out_dir.generic_u8string());
            cmdline.emplace_back(mAdapterPath.generic_u8string());

            // Run CMake with our adapter script
            RunProcessOrThrow("cmake", cmdline, true, true);
        }

        std::ifstream file{meta_path};
        auto cmake_meta = YAML::Load(file);

        TargetConfig target_config{YAML::NodeType::Map};

        target_config["arch"] = arch;
        target_config["configuration"] = config;
        target_config["platform"] = platform;

        // Not built or linked using anything
        target_config["langs"].push_back("cmake");
        target_config["link-with"] = "cmake";
        
        target_config["cmake-meta"] = cmake_meta;
        target_config["cmake-out-build-script"] = (out_dir / "build.ninja").generic_u8string();

        target_config["no-auto-include-dirs"] = true;

        auto target = std::make_unique<Target>(path, "_", TargetType::Project, target_config);
        target->name = target->module = fmt::format(".cmake_target_stub_{}", path_hash);

        for (auto kv : cmake_meta["targets"])
        {
            TargetConfig child_config{YAML::NodeType::Map};

            TargetType type = TargetType::Custom;

            auto cmake_type = kv.second["cmake-type"].Scalar();

            if (cmake_type == "STATIC_LIBRARY" || cmake_type == "INTERFACE_LIBRARY")
                type = TargetType::StaticLibrary;
            else if (cmake_type == "SHARED_LIBRARY")
                type = TargetType::SharedLibrary;
            else if (cmake_type == "EXECUTABLE")
                type = TargetType::Executable;
            else
                continue;

            if (auto location = kv.second["location"])
                child_config["cxx-link-deps"].push_back(location.Scalar());
                
            for (auto dir : kv.second["include-dirs"])
            {
                auto s = dir.Scalar();

                if (!s.empty())
                    child_config["cxx-include-dirs"].push_back(s);
            }

            auto child = std::make_unique<Target>(path, kv.first.Scalar(), type, child_config);
            child->parent = target.get();
            target->children.emplace_back(std::move(child));
        }
        
        for (auto kv : cmake_meta["targets"])
        {
            if (auto child = target->FindChild(kv.first.Scalar()))
            {
                for (auto dep_str : kv.second["cmake-deps"])
                {
                    if (dep_str.Scalar().empty() || dep_str.Scalar().find('-') == 0)
                        continue;

                    if (auto target_dep = target->FindChild(dep_str.Scalar()))
                    {
                        auto dependency = ParseTargetDependency("cmake-internal:" + dep_str.Scalar());
                        dependency.resolved = {target_dep};
                        child->dependencies.emplace_back(dependency);
                    }
                    else
                    {
                        mOut->Warn(
                            fg(fmt::color::dim_gray) | fmt::emphasis::bold,
                            " ! CMakeTargetLoadMiddleware: Target '{}' has unknown CMake dependency '{}' - this may or may not be an error\n",
                            child->name, dep_str.Scalar()
                        );
                    }
                }
            }
        }

        return target;
    }
}
