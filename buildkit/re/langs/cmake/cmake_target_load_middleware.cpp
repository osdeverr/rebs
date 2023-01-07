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
        
        if (ancestor)
        {
		    auto [scope, context] = ancestor->GetBuildVarScope();

            arch = scope.ResolveLocal("arch");
            config = scope.ResolveLocal("configuration");
            platform = scope.ResolveLocal("platform");

            out_dir = ancestor->root->path / ".re-cache" / "cmake-gen" / fmt::format("{}-{}-{}-{}", path_hash, arch, config, platform);
        }

        // HACK
        config = "RelWithDebInfo";

        auto bin_dir = out_dir / "build";

        fs::create_directories(bin_dir);

        mOut->Info(fg(fmt::color::cornflower_blue), " * CMakeTargetLoadMiddleware: Loading CMake project target from '{}'\n\n", path.generic_u8string());

        auto meta_path = out_dir / "re-cmake-meta.yml";

        // Run CMake with our adapter script
        RunProcessOrThrow("cmake", {
            "cmake",
            "-G", "Ninja",
            fmt::format("-DRE_ORIGINAL_CMAKE_DIR={}", canonical_path.generic_u8string()),
            fmt::format("-DRE_BIN_OUT_DIR={}", bin_dir.generic_u8string()),
            fmt::format("-DRE_ADAPTED_META_FILE={}", meta_path.generic_u8string()),
            fmt::format("-DCMAKE_BUILD_TYPE={}", config),
            "-B",
            out_dir.generic_u8string(),
            mAdapterPath.generic_u8string()
        });

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

        return target;
    }
}
