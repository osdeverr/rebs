#include <re/build/ninja_gen.h>
#include <re/build/default_build_context.h>

#include <re/path_util.h>

#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/args.h>
#include <fmt/color.h>
#include <fmt/ostream.h>

#include <fstream>

#include <boost/algorithm/string.hpp>

int main(int argc, const char** argv)
{
#ifdef WIN32
    SetConsoleOutputCP(65001);
    SetThreadUILanguage(LANG_ENGLISH);
#endif

    try
    {
        std::vector<std::string_view> args(argv, argv + argc);

        re::DefaultBuildContext context;
        context.LoadDefaultEnvironment(re::GetReDataPath(), re::GetReDynamicDataPath());
        // context.LoadDefaultEnvironment(L"D:/Programs/ReBS/bin");

        context.SetVar("configuration", "release");

        if (args.size() == 1)
        {
            auto path = ".";
            context.LoadCachedParams(path);

            return context.BuildTargetInDir(path);
            // return context.BuildTargetInDir(L"D:/PlakSystemsSW/NetUnitCollection"); 
        }
        else if (args[1] == "new")
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
        else if (args[1] == "do")
        {
            auto path = args.size() > 3 ? args[2] : ".";
            context.LoadCachedParams(path);

            auto desc = context.GenerateBuildDescForTargetInDir(path);

            context.BuildTarget(desc);

            auto action_type = args[args.size() > 3 ? 3 : 2];

            auto style = fmt::emphasis::bold | fg(fmt::color::aquamarine);
            fmt::print(style, " - Running custom actions for '{}'\n\n", action_type);

            auto env = context.GetBuildEnv();
            for (auto& dep : env->GetSingleTargetDepSet(desc.pRootTarget))
                env->RunActionsCategorized(dep, &desc, action_type);

            return 0;
        }
        else if (args[1] == "config" || args[1] == "conf" || args[1] == "cfg")
        {
            auto path = ".";
            auto yaml = context.LoadCachedParams(path);

            if (args.size() > 2 && args[2] != "-")
                yaml["arch"] = args[2].data();

            if (args.size() > 3 && args[3] != "-")
                yaml["configuration"] = args[3].data();

            context.SaveCachedParams(path, yaml);

            // fmt::print("{}", desc.meta.dump(4));

            return 0;
        }
        else if (args[1] == "meta")
        {
            auto path = args.size() > 2 ? args[2] : ".";
            context.LoadCachedParams(path);

            context.SetVar("generate-build-meta", "true");

            if (args.size() > 3 && args[3] == "cached-only")
                context.SetVar("auto-load-uncached-deps", "false");

            auto desc = context.GenerateBuildDescForTargetInDir(args.size() > 2 ? args[2] : ".");
            context.SaveTargetMeta(desc);

            // fmt::print("{}", desc.meta.dump(4));

            return 0;
        }
        else
        {
            auto path = args[1] == "b" ? "." : args[1];
            context.LoadCachedParams(path);

            if (args.size() > 2 && args[2] != "-")
                context.SetVar("arch", args[2].data());

            if (args.size() > 3 && args[3] != "-")
                context.SetVar("configuration", args[3].data());

            auto desc = context.GenerateBuildDescForTargetInDir(path);
            context.BuildTarget(desc);

            return 0;
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
    catch (const re::TargetUncachedDependencyException& e)
    {
        return 5;
    }
    catch (const std::exception& e)
    {
        std::string message = "";

        /*
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
        */

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

        return 1;
    }
}