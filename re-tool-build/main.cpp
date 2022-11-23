#include <re/build/ninja_gen.h>
#include <re/build/default_build_context.h>

#include <re/path_util.h>

#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/args.h>
#include <fmt/color.h>
#include <fmt/ostream.h>

#include <fstream>

int main(int argc, const char** argv)
{
    SetConsoleOutputCP(65001);
    SetThreadUILanguage(LANG_ENGLISH);

    try
    {
        std::vector<std::string_view> args(argv, argv + argc);

        re::DefaultBuildContext context;
        context.LoadDefaultEnvironment(L"D:/Programs/ReBS/bin");

        if (args.size() == 1)
        {
            return context.BuildTargetInDir(L"D:/PlakSystemsSW/regar");
        }
        else if (args[1] == "build")
        {
            return context.BuildTargetInDir(args[2]);
        }
        else
        {
            auto desc = context.GenerateBuildDescForTargetInDir(args[1]);
            
            context.BuildTarget(desc);

            if (args.size() > 2 && args[2] == "install")
                context.InstallTarget(desc);

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
