#include "cmake_target_load_middleware.h"

#include <re/process_util.h>
#include <re/target_cfg_utils.h>

#include <fstream>
#include <futile/futile.h>

namespace re
{
    bool CMakeTargetLoadMiddleware::SupportsTargetLoadPath(const fs::path &path)
    {
        //
        // In case a re.yml exists in the directory, it can be assumed the middleware isn't needed
        // as the project is already designed with Re build support in mind.
        //

        return fs::exists(path / "CMakeLists.txt") && !fs::exists(path / "re.yml");
    }

    std::unique_ptr<Target> CMakeTargetLoadMiddleware::LoadTargetWithMiddleware(const fs::path &path,
                                                                                const Target *ancestor,
                                                                                const TargetDependency *dep_source)
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

            std::string dir_name = fmt::format("{}-{}-{}-{}-{}", canonical_path.filename().generic_u8string(), arch,
                                               config, platform, path_hash);

            if (dep_source && dep_source->extra_config_hash)
                dir_name += fmt::format("-{}", dep_source->extra_config_hash);

            out_dir = ancestor->root->path / ".re-cache" / "cmake-gen" / dir_name;

            if (auto compiler = scope.GetVar("cxx.tool.compiler"))
                compiler_override = fs::path{*compiler}.generic_u8string();

            if (auto flags = scope.GetVar("platform-link-paths"))
                link_flags += scope.Resolve(*flags);

            if (auto standard = ancestor->resolved_config.search("cxx-standard"))
                cxx_std_override = standard->scalar();

            if (auto build_type = ancestor->resolved_config.search("cmake-build-type"))
                config = build_type->scalar();
        }

        // HACK: PLEASE add something better later
        if (cxx_std_override == "latest" || cxx_std_override == "2a")
            cxx_std_override = "20";

        auto bin_dir = out_dir / "build";

        fs::create_directories(bin_dir);

        mOut->Info(fg(fmt::color::cornflower_blue) | fmt::emphasis::bold,
                   "\n * CMakeTargetLoadMiddleware: Loading CMake project target from '{}'\n", path.generic_u8string());

        auto meta_path = out_dir / "re-cmake-meta.yml";

        bool should_rebuild = true;

        if (ancestor && ancestor->build_var_scope->GetVar("cmake-reconfigure").value_or("false") == "true")
            should_rebuild = true;

        ulib::yaml cmake_meta{ulib::yaml::value_t::null};

        if (fs::exists(meta_path))
        {
            cmake_meta = ulib::yaml::parse(futile::open(meta_path).read());

            should_rebuild = false;

            if (!should_rebuild && dep_source &&
                cmake_meta["last-build-ecfg-hash"].get<std::size_t>() != dep_source->extra_config_data_hash)
                should_rebuild = true;
        }

        if (should_rebuild)
        {
            mOut->Info(fg(fmt::color::dim_gray) | fmt::emphasis::bold, "     Rebuilding CMake cache...\n\n");

            ulib::list<ulib::string> cmdline = {"cmake", "-G", "Ninja"};

            cmdline.emplace_back("-DRE_ORIGINAL_CMAKE_DIR=" + canonical_path.generic_u8string());
            cmdline.emplace_back("-DRE_BIN_OUT_DIR=" + bin_dir.generic_u8string());
            cmdline.emplace_back("-DRE_ADAPTED_META_FILE=" + meta_path.generic_u8string());
            cmdline.emplace_back("-DCMAKE_BUILD_TYPE=" + config);

            if (dep_source && !dep_source->extra_config.is_null())
            {
                auto parse_config_for_target = [&cmdline, &arch, &platform, &config](
                                                   const ulib::yaml &node, const ulib::list<ulib::string> &targets) {
                    std::string defs_private, defs_public;

                    for (auto &kv : node["cxx-compile-definitions"].items())
                        defs_private.append(fmt::format("{}={};", kv.name(), kv.value().scalar()));

                    for (auto &kv : node["cxx-compile-definitions-public"].items())
                        defs_public.append(fmt::format("{}={};", kv.name(), kv.value().scalar()));

                    for (auto &target : targets)
                    {
                        cmdline.emplace_back(
                            fmt::format("-DRE_CUSTOM_COMPILE_DEFINITIONS_PRIVATE_{}={}", target, defs_private));
                        cmdline.emplace_back(
                            fmt::format("-DRE_CUSTOM_COMPILE_DEFINITIONS_PUBLIC_{}={}", target, defs_public));
                    }

                    for (auto kv : node["cmake-extra-options"].items())
                        cmdline.emplace_back(fmt::format("-D{}={}", kv.name(), kv.value().scalar()));
                };

                auto resolved = GetFlatResolvedTargetCfg(dep_source->extra_config,
                                                         {{"arch", arch}, {"platform", platform}, {"config", config}});

                if (dep_source->filters.empty())
                    parse_config_for_target(resolved, {"ALL"});
                else
                    parse_config_for_target(resolved, dep_source->filters);

                for (auto &kv : resolved.items())
                {
                    constexpr auto kCMakeTargetPrefix = "cmake-target.";

                    if (kv.name().find(kCMakeTargetPrefix) == 0)
                        parse_config_for_target(kv.value(), {kv.name().substr(sizeof kCMakeTargetPrefix)});
                }
            }

            cmdline.emplace_back(fmt::format("-DRE_ECFG_HASH={}", dep_source ? dep_source->extra_config_data_hash : 0));

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
            RunProcessOrThrow("cmake", {}, cmdline, true, true);
        }

        std::ifstream file{meta_path};
        cmake_meta = ulib::yaml::parse(futile::open(meta_path).read());

        TargetConfig target_config{ulib::yaml::value_t::map};

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
        target->name = target->module =
            fmt::format("cmake.{}.{}", path_hash, canonical_path.filename().generic_u8string());

        for (auto &kv : cmake_meta["targets"].items())
        {
            TargetConfig child_config{ulib::yaml::value_t::map};

            TargetType type = TargetType::Custom;

            auto cmake_type = kv.value()["cmake-type"].scalar();

            if (cmake_type == "STATIC_LIBRARY" || cmake_type == "INTERFACE_LIBRARY")
                type = TargetType::StaticLibrary;
            else if (cmake_type == "SHARED_LIBRARY")
                type = TargetType::SharedLibrary;
            else if (cmake_type == "EXECUTABLE")
                type = TargetType::Executable;
            else
                continue;

            if (auto location = kv.value().search("location"))
                child_config["cxx-link-deps"].push_back(location->scalar());

            for (auto dir : kv.value()["include-dirs"])
            {
                if (dir.is_scalar())
                {
                    auto s = dir.scalar();

                    if (!s.empty())
                        child_config["cxx-include-dirs"].push_back(s);
                }
            }

            auto child = std::make_unique<Target>(path, kv.name(), type, child_config);
            child->parent = target.get();
            target->children.emplace_back(std::move(child));
        }

        for (auto kv : cmake_meta["targets"].items())
        {
            if (auto child = target->FindChild(kv.name()))
            {
                if (auto cmake_deps = kv.value().search("cmake-deps"))
                {
                    for (auto dep_str : *cmake_deps)
                    {
                        if (dep_str.scalar().empty() || dep_str.scalar().find('-') == 0)
                            continue;

                        if (auto target_dep = target->FindChild(dep_str.scalar()))
                        {
                            auto dependency = ParseTargetDependency(ulib::string_view{"cmake-ref:"} + dep_str.scalar());
                            dependency.resolved = {target_dep};
                            child->dependencies.emplace_back(dependency);
                        }
                        else
                        {
                            mOut->Warn(
                                fg(fmt::color::dim_gray) | fmt::emphasis::bold,
                                " ! CMakeTargetLoadMiddleware: Target '{}' has unknown CMake dependency '{}' - this "
                                "may or may not be an error\n",
                                child->name, dep_str.scalar());
                        }
                    }
                }
            }
        }

        return target;
    }
} // namespace re
