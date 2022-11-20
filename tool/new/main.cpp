#include <re/target.h>
#include <re/path_util.h>

#include <fmt/format.h>
#include <fmt/color.h>

int main(int argc, const char** argv)
{
    try
    {
        auto path_to_me = re::GetCurrentExecutablePath();

        std::vector<std::string_view> args(argv, argv + argc);

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
    catch (const std::exception& e)
    {
        fmt::print(
            fmt::emphasis::bold | bg(fmt::color::dark_red) | fg(fmt::color::white),
            "\n{}\n", e.what()
        );
    }
}
