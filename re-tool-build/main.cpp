#include <re/buildenv.h>
#include <re/build_desc.h>
#include <re/path_util.h>

#include <re/langs/cxx_lang_provider.h>
#include <re/deps/vcpkg_dep_resolver.h>
#include <re/deps/git_dep_resolver.h>
#include <re/deps/github_dep_resolver.h>

#include <re/process_util.h>

#include <magic_enum.hpp>
#include <filesystem>

#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/args.h>
#include <fmt/color.h>
#include <fmt/ostream.h>

#include <reproc++/reproc.hpp>

#include <fstream>

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

            fmt::print("\x1b[1m{}\x1b[0m => \x1b[3m{}\x1b[0m\n", source.path.u8string(), "<none>");
        }
    }
}

namespace re
{
    class FmtOstreamWrapper
    {
    public:
        FmtOstreamWrapper(std::ostream* stream)
            : mStream{ stream }
        {}

        template<class F, class... Args>
        void print(const F& format, Args&&... args)
        {
            fmt::print(*mStream, format, std::forward<Args>(args)...);
        }

    private:
        std::ostream* mStream;
    };

    void GenerateNinjaBuildFile(const NinjaBuildDesc& desc, const fs::path& out_dir)
    {
        constexpr auto kToolPrefix = "re_tool_";

        auto path = out_dir / "build.ninja";

        std::ofstream file{ path, std::ios::binary };
        FmtOstreamWrapper out{ &file };

        out.print("builddir = {}\n", desc.out_dir.u8string());

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

    int BuildReTargetAt(const fs::path& path_to_me, const fs::path& path, bool install = false)
    {
        re::BuildEnv env;

        re::CxxLangProvider provider{ path_to_me / "data" / "environments" / "cxx" };
        env.AddLangProvider("cpp", &provider);

        VcpkgDepResolver vcpkg_resolver{ path_to_me / "deps" / "vcpkg" };
        env.AddDepResolver("vcpkg", &vcpkg_resolver);
        env.AddDepResolver("vcpkg-dep", &vcpkg_resolver);

        GitDepResolver git_resolver{ &env };
        GithubDepResolver github_resolver{ &git_resolver };

        env.AddDepResolver("git", &git_resolver);
        env.AddDepResolver("github", &github_resolver);
        env.AddDepResolver("github-ssh", &github_resolver);

        env.LoadCoreProjectTarget(path_to_me / "data" / "core-project");

        if (!re::DoesDirContainTarget(path))
        {
            fmt::print(stderr, " ! Directory '{}' does not contain a valid Re target. Quitting.\n", path.u8string());
            return -1;
        }

        auto& root = env.LoadTarget(path);

        auto re_arch = std::getenv("RE_ARCH");
        auto re_platform = std::getenv("RE_PLATFORM");

        auto out_dir = root.path / "out" / fmt::format("{}-{}", re_arch, re_platform);
        
        if (auto entry = root.GetCfgEntry<std::string>("output-directory"))
            out_dir = fmt::format(*entry, fmt::arg("arch", re_arch), fmt::arg("platform", re_platform));

        std::filesystem::create_directories(out_dir);

        NinjaBuildDesc desc;

        desc.out_dir = out_dir;
        desc.artifact_out_format = root.GetCfgEntry<std::string>("artifact-dir-format", CfgEntryKind::Recursive).value_or("build");
        desc.object_out_format = root.GetCfgEntry<std::string>("object-dir-format", CfgEntryKind::Recursive).value_or("{module}");

        env.PopulateBuildDesc(desc);
        re::GenerateNinjaBuildFile(desc, out_dir);

        auto path_to_ninja = path_to_me / "ninja.exe";

        std::vector<std::wstring> cmdline;

        cmdline.push_back(path_to_ninja.wstring());
        cmdline.push_back(L"-C");
        cmdline.push_back(out_dir.wstring());

        fmt::print("{}\n", out_dir.u8string());

        RunProcessOrThrowWindows("ninja", cmdline, true, true);

        // Running post-build actions
        env.RunPostBuildActions(desc);

        if (install)
            env.RunInstallActions(desc);

        return 0;
    }
}

int main(int argc, const char** argv)
{
    SetConsoleOutputCP(65001);
    SetThreadUILanguage(LANG_ENGLISH);

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
        else
        {
            return re::BuildReTargetAt(path_to_me, args[1], args.size() > 2 && args[2] == "install");
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
        std::string message = "";


        const boost::stacktrace::stacktrace* st = boost::get_error_info<re::TracedError>(e);
        if (st) {
            int i = 0;

            for (auto& f : *st)
            {
                auto name = f.name();
                auto path = re::fs::path{ f.source_file() };

                if (name.find("re::") != name.npos)
                    message.append(fmt::format(
                        "  at {} @ {}:{}\n", name, path.filename().u8string(), f.source_line()
                    ));
            }

        }

        fmt::print(
            stderr,
            fmt::emphasis::bold | bg(fmt::color::black) | fg(fmt::color::light_coral),
            "\n\n  Error: {}\n", e.what()
        );

        fmt::print(
            stderr,
            bg(fmt::color{ 0x090909 }) | fg(fmt::color::light_coral),
            "\n\n{}", message
        );

        fmt::print(
            stderr,
            "\n\n"
        );
    }
}
