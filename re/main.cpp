#include <re/buildenv.h>
#include <magic_enum.hpp>
#include <filesystem>

#include <fmt/format.h>

#include <reproc++/reproc.hpp>

class CCppLangProvider : public re::ILangProvider
{
public:
	bool SupportsFileExtension(std::string_view extension)
	{
		return extension == ".c" || extension == ".cpp" || extension == ".ixx";
	}
};

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
            DumpTargetStructure(child, tabs + 1);

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
    void GenerateNinjaBuildFile(BuildEnv& env, const std::string& out_dir)
    {
        auto targets = env.GetTargetsInDependencyOrder();

        std::string path = out_dir + "/build.ninja";

        std::unique_ptr<std::FILE, decltype(&std::fclose)> file{ std::fopen(path.data(), "w"), &std::fclose };

        constexpr auto kDefaultLinkerPath = R"(C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\bin\Hostx64\x64\)";
        constexpr auto kDefaultLibPath = R"(C:\Program Files (x86)\Windows Kits\10\Lib\10.0.19041.0\um\x64)";

        fmt::print(file.get(), "cc = {}/cl.exe\n", kDefaultLinkerPath);
        fmt::print(file.get(), "link = {}/link.exe\n", kDefaultLinkerPath);

        fmt::print(file.get(), "builddir = .\n", out_dir);

        fmt::print(file.get(), "cflags = /std:c++latest /experimental:module /EHsc /MD\n");

        fmt::print(file.get(), "rule cxx\n");
        fmt::print(file.get(), "    command = $cc $cflags /c $in /out:$out\n");
        fmt::print(file.get(), "    description = CXX $cflags /c $in /out:$out\n");

        fmt::print(file.get(), "rule cxx_module_iface\n");
        fmt::print(file.get(), "    command = $cc $cflags /interface /c $in /out:$out\n");
        fmt::print(file.get(), "    description = CXX_MI $cflags /interface /c $in /out:$out\n");

        fmt::print(file.get(), "rule link\n");
        fmt::print(file.get(), "    command = link kernel32.lib $in /OUT:$out\n");
        fmt::print(file.get(), "    description = LINK kernel32.lib $in /OUT:$out\n");

        for (auto& target : targets)
        {
            auto module_escaped = target->module;
            std::replace(module_escaped.begin(), module_escaped.end(), '.', '_');

            std::filesystem::create_directories(out_dir + "/" + target->module);

            fmt::print(file.get(), "path_{} = {}\n", module_escaped, target->path);

            for (auto& source : target->sources)
            {
                auto local_path = source.path.substr(target->path.size() + 1);
                fmt::print(file.get(), "build $builddir/{}/{}.obj: ", target->module, local_path);

                if(source.extension == ".ixx")
                    fmt::print(file.get(), "cxx_module_iface $path_{}/{}\n", module_escaped, local_path);
                else
                    fmt::print(file.get(), "cxx $path_{}/{}\n", module_escaped, local_path);
            }

            if (target->type == TargetType::Executable)
            {
                fmt::print(file.get(), "build $builddir/{}.exe: link", target->name);

                for (auto& source : target->sources)
                {
                    auto local_path = source.path.substr(target->path.size() + 1);
                    fmt::print(file.get(), " $builddir/{}/{}.obj", target->module, local_path);
                }

                fmt::print(file.get(), "\n");
            }
        }
    }
}

int main(int args, const char** argv)
{
	re::BuildEnv env;

	CCppLangProvider provider;
	env.AddLangProvider("c", &provider);
	env.AddLangProvider("cpp", &provider);

    auto& root = env.LoadRootTarget(RE_TEST_EXAMPLES_FOLDER "/complex-project");

	// re::DumpTargetStructure(root);

	auto targets = env.GetTargetsInDependencyOrder();
    for (auto& target : targets)
        fmt::print("{} v\n", target->module);

    auto out_dir = root.GetCfgEntry<std::string>("out-dir").value_or("out");

    std::filesystem::create_directories(out_dir);
    re::GenerateNinjaBuildFile(env, out_dir);

    auto path_to_me = std::filesystem::path{ argv[0] }.parent_path();
    auto path_to_ninja = path_to_me / "ninja.exe";

    reproc::options options;
    options.redirect.parent = true;

    reproc::process ninja_process;
    std::vector<std::string> cmdline;

    cmdline.push_back(path_to_ninja.string());
    cmdline.push_back("-C");
    cmdline.push_back("./out");

    auto start_ec = ninja_process.start(cmdline, options);
    if (start_ec)
    {
        fmt::print("[{}] ! Failed to start Ninja: {}\n", root.module, start_ec.message());
        return -1;
    }

    auto [exit_code, end_ec] = ninja_process.wait(reproc::infinite);

    if (end_ec)
    {
        fmt::print("[{}] ! Failed to run Ninja with error code: {} (exit_code={})\n", root.module, end_ec.message(), exit_code);
        return -1;
    }

    if (exit_code)
    {
        fmt::print("[{}] ! Failed to run Ninja (exit_code={})\n", root.module, exit_code);
        return -1;
    }
}