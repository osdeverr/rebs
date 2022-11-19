#include <re/buildenv.h>
#include <re/build_desc.h>
#include <re/cxx_lang_provider.h>
#include <re/path_util.h>

#include <magic_enum.hpp>
#include <filesystem>

#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/args.h>
#include <fmt/color.h>

#include <reproc++/reproc.hpp>

#include <fstream>

namespace re
{
    int RunProcessOrThrow(std::string_view program_name, const std::vector<std::string>& cmdline, bool output = false, bool throw_on_bad_exit = false)
    {
        reproc::options options;
        options.redirect.parent = output;

        reproc::process process;

        auto start_ec = process.start(cmdline, options);
        if (start_ec)
        {
            throw TargetLoadException(fmt::format("{} failed to start: {}", program_name, start_ec.message()));
        }

        auto [exit_code, end_ec] = process.wait(reproc::infinite);

        // process.read(reproc::stream::out, );

        if (end_ec)
        {
            throw TargetLoadException(fmt::format("{} failed to run: {} (exit_code={})", program_name, end_ec.message(), exit_code));
        }

        if (throw_on_bad_exit && exit_code != 0)
        {
            throw TargetLoadException(fmt::format("{} failed: exit_code={}", program_name, exit_code));
        }

        return exit_code;
    }
}

namespace re
{
    class VcpkgDepResolver : public IDepResolver
    {
    public:
        VcpkgDepResolver(const std::filesystem::path& path)
            : mVcpkgPath{ path }
        {}

        Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep)
        {
            if (auto& cached = mTargetCache[dep.name])
                return cached.get();

            auto vcpkg_root = mVcpkgPath;

            if (auto path = target.GetCfgEntry<std::string>("vcpkg-root-path", CfgEntryKind::Recursive))
                vcpkg_root = std::filesystem::path{ *path };

            if (!std::filesystem::exists(vcpkg_root))
            {
                fmt::print(
                    fmt::emphasis::bold | fg(fmt::color::light_sea_green),
                    "[{}] [vcpkg] Target dependency '{}' needs vcpkg. Installing...\n\n",
                    target.module,
                    dep.name
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
                    "[{}] [vcpkg] Restoring package '{}'...\n\n",
                    target.module,
                    dep.name
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
                    "\n[{}] [vcpkg] Restored package '{}' ({:.2f}s)\n",
                    target.module,
                    dep.name,
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
                        "[{}] [vcpkg] Package '{}' already available\n",
                        target.module,
                        dep.name
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

    private:
        std::filesystem::path mVcpkgPath;

        std::unordered_map<std::string, std::unique_ptr<Target>> mTargetCache;
    };
}

namespace re
{
    void DumpTargetStructure(const Target& target, int tabs = 0)
    {
        for (auto i = 0; i < tabs; i++)
            fmt::print("    ");

        switch (target.type)
        {
        case TargetType::Project:
            fmt::print("\x1b[33;1m* ");
            break;
        case TargetType::Executable:
            fmt::print("\x1b[32;1m");
            break;
        case TargetType::StaticLibrary:
            fmt::print("\x1b[35;1m");
            break;
        default:
            break;
        };

        fmt::print("{} [{}] (module=\x1b[3m{}\x1b[0m)\n", target.name, magic_enum::enum_name(target.type), target.module);

        fmt::print("\x1b[0m");

        for (auto& child : target.children)
            DumpTargetStructure(*child, tabs + 1);

        for (auto& source : target.sources)
        {
            for (auto i = 0; i < tabs + 1; i++)
                fmt::print("    ");

            fmt::print("\x1b[1m{}\x1b[0m => \x1b[3m{}\x1b[0m\n", source.path, "<none>");
        }
    }
}

namespace re
{
    void GenerateNinjaBuildFile(const NinjaBuildDesc& desc, const std::string& out_dir)
    {
        constexpr auto kToolPrefix = "re_tool_";

        std::string path = out_dir + "/build.ninja";

        fmt::ostream out = fmt::output_file(path);

        out.print("builddir = .\n", desc.out_dir);

        for (auto& [key, val] : desc.vars)
            out.print("{} = {}\n", key, val);

        out.print("\n");

        for (auto& tool : desc.tools)
            out.print("{}{} = {}\n", kToolPrefix, tool.name, tool.path);

        out.print("\n");

        for (auto& rule : desc.rules)
        {
            out.print("rule {}\n", rule.name);
            out.print("    command = ${}{} {}\n", kToolPrefix, rule.tool, rule.cmdline);
            out.print("    description = {}\n", rule.description);

            for (auto& [key, val] : rule.vars)
                out.print("    {} = {}\n", key, val);
        }

        out.print("\n");

        for (auto& target : desc.targets)
        {
            out.print("build {}: {} {}", target.out, target.rule, target.in);

            if (target.deps.size() > 0)
            {
                out.print(" |");

                for (auto& dep : target.deps)
                    out.print(" {}", dep);
            }

            out.print("\n");

            for (auto& [key, val] : target.vars)
                out.print("    {} = {}\n", key, val);
        }

        out.print("\n");

        /*
        for (auto& child : desc.children)
        {
            auto child_dir = out_dir + "/" + child.name;
            std::filesystem::create_directories(child_dir);

            GenerateNinjaFile(child, child_dir);
            out.print("subninja {}\n", child_dir);
        }
        */
    }

    std::string& ReplaceInString(std::string& s, const std::string& from, const std::string& to)
    {
        if (!from.empty())
            for (size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size())
                s.replace(pos, from.size(), to);
        return s;
    }

    int BuildReTargetAt(const std::filesystem::path& path_to_me, std::string_view path)
    {
        re::BuildEnv env;

        re::CxxLangProvider provider{ (path_to_me / "data" / "environments" / "cxx").string() };
        env.AddLangProvider("cpp", &provider);

        VcpkgDepResolver vcpkg_resolver{ path_to_me / "data" / "deps" / "vcpkg" };
        env.AddDepResolver("vcpkg", &vcpkg_resolver);
        env.AddDepResolver("vcpkg-dep", &vcpkg_resolver);

        env.LoadCoreProjectTarget((path_to_me / "data" / "core-project").string());

        if (!re::DoesDirContainTarget(path))
        {
            fmt::print(stderr, " ! Directory '{}' does not contain a valid Re target. Quitting.\n", path);
            return -1;
        }

        auto& root = env.LoadTarget(path.data());

        auto re_arch = std::getenv("RE_ARCH");
        auto re_platform = std::getenv("RE_PLATFORM");

        auto out_dir = root.GetCfgEntry<std::string>("output-directory").value_or(root.path + "/out/" + fmt::format("{}-{}", re_arch, re_platform));

        std::filesystem::create_directories(out_dir);

        NinjaBuildDesc desc;

        desc.out_dir = out_dir;
        desc.artifact_out_format = root.GetCfgEntry<std::string>("artifact-dir-format", CfgEntryKind::Recursive).value_or("build");
        desc.object_out_format = root.GetCfgEntry<std::string>("object-dir-format", CfgEntryKind::Recursive).value_or("{module}");

        env.PopulateBuildDesc(desc);
        re::GenerateNinjaBuildFile(desc, out_dir);

        auto path_to_ninja = path_to_me / "ninja.exe";

        reproc::options options;
        options.redirect.parent = true;

        reproc::process ninja_process;
        std::vector<std::string> cmdline;

        cmdline.push_back(path_to_ninja.string());
        cmdline.push_back("-C");
        cmdline.push_back(out_dir);

        auto start_ec = ninja_process.start(cmdline, options);
        if (start_ec)
        {
            fmt::print("[{}] ! Failed to start Ninja: {}\n", root.module, start_ec.message());
            return -255;
        }

        auto [exit_code, end_ec] = ninja_process.wait(reproc::infinite);

        if (end_ec)
        {
            fmt::print(stderr, "[{}] ! Failed to run Ninja with error code: {} (exit_code={})\n", root.module, end_ec.message(), exit_code);
            return exit_code;
        }

        if (exit_code)
        {
            fmt::print(stderr, "\n [{}] Build failed! (exit_code={})\n", root.module, exit_code);
            return exit_code;
        }

        // Running post-build actions
        env.RunPostBuildActions(desc);

        return 0;
    }
}

int main(int argc, const char** argv)
{
    try
    {
        auto path_to_me = re::GetCurrentExecutablePath();

        std::vector<std::string_view> args(argv, argv + argc);

        if (args.size() == 1)
        {
            return re::BuildReTargetAt(path_to_me, ".");
        }
        else if (args[1] == "build")
        {
            return re::BuildReTargetAt(path_to_me, args[2]);
        }

        /*
        if (args.size() == 1)
        {
        }
        else
        {
            else if (second_arg == "new")
            {
                auto& type = args[2];
                auto& name = args[3];

                auto path = name;
                if (path.front() == '.')
                    path = path.substr(1);

                if (args.size() > 4)
                    path = args[4];

                re::Target::CreateEmptyTarget(path, re::TargetTypeFromString(type.data()), name);
                fmt::print("\n");
                fmt::print("Created new {} target '{}' in directory '{}'.\n", type, name, path);
                fmt::print("\n");
                fmt::print("    To build the new target, type:\n");
                fmt::print("        > cd {}\n", path);
                fmt::print("        > re\n");
                fmt::print("\n");
                fmt::print("    To edit the new target, modify the {}/re.yml file.\n", path);
                fmt::print("\n");
            }
            else if (second_arg == "env")
            {
                auto data_path = (path_to_me / "data").string();

                auto env_cfg = YAML::LoadFile(data_path + "/environments/cmdline/" + args[2].data() + ".yml");

                fmt::dynamic_format_arg_store<fmt::format_context> store;

                store.push_back(fmt::arg("re_data_path", data_path));

                std::size_t i = 0;

                std::list<std::string> arg_names;

                for (const auto& arg : env_cfg["args"])
                {
                    auto index = i++;
                    auto& name = arg_names.emplace_back(arg.as<std::string>());

                    if (args.size() < index)
                        throw std::runtime_error("missing argument '" + name + "'");

                    store.push_back(fmt::arg(name.data(), args[index + 3]));
                }

                for (const auto& cmd : env_cfg["run"])
                {
                    auto expanded = fmt::vformat(cmd.as<std::string>(), store);
                    auto code = std::system(expanded.data());

                    if (code != 0)
                        throw std::runtime_error(fmt::format("command '{}' returned exit code {}", expanded, code));
                }
            }
        }
        */
    }
    catch (const std::exception& e)
    {
        fmt::print(
            fmt::emphasis::bold | bg(fmt::color::dark_red) | fg(fmt::color::white),
            "\n{}\n", e.what()
        );
    }
}
