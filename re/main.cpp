#include <re/buildenv.h>
#include <re/build_desc.h>

#include <magic_enum.hpp>
#include <filesystem>

#include <fmt/format.h>
#include <fmt/os.h>

#include <reproc++/reproc.hpp>

namespace re
{
    class CCppLangProvider : public ILangProvider
    {
    public:
        virtual const char* GetLangId() { return "cpp"; }

        bool SupportsFileExtension(std::string_view extension)
        {
            return extension == ".c" || extension == ".cpp" || extension == ".cc" || extension == ".cxx" || extension == ".ixx";
        }
    };

    class MsvcCCppLangProvider : public CCppLangProvider
    {
    public:
        void InitInBuildDesc(NinjaBuildDesc& desc)
        {
            // TODO: Implement proper searches for the toolchain
            desc.tools.push_back(BuildTool{ "msvc_cxx", "cl.exe" });
            desc.tools.push_back(BuildTool{ "msvc_link", "link.exe" });
            desc.tools.push_back(BuildTool{ "msvc_lib", "lib.exe" });

            desc.vars["msvc_cflags"] = "/nologo /interface /MP /std:c++latest /experimental:module /EHsc /MD";
            desc.vars["msvc_lflags"] = "/nologo";
        }

        bool InitBuildTarget(NinjaBuildDesc& desc, const Target& target)
        {
            if (target.type != TargetType::Executable && target.type != TargetType::StaticLibrary && target.type != TargetType::SharedLibrary)
                return false;

            auto path = GetEscapedModulePath(target);

            TargetConfig cxx_flags = GetRecursiveMapCfg(target, "cxx-flags");
            TargetConfig link_flags = GetRecursiveMapCfg(target, "cxx-link-flags");

            TargetConfig definitions = GetRecursiveMapCfg(target, "cxx-compile-definitions");
            TargetConfig definitions_pub = GetRecursiveMapCfg(target, "cxx-compile-definitions-public");
            definitions_pub["WIN32"] = true;

            std::string flags_base = "$msvc_cflags $target_custom_flags";

            flags_base += fmt::format(" /ifcOutput $builddir/{}", target.module);

            // flags_base += " /I\"" + target.path + "\"";

            std::vector<const Target*> include_deps;
            PopulateTargetDependencySetNoResolve(&target, include_deps);

            for (auto& target : include_deps)
            {
                flags_base += " /I\"" + target->path + "\"";
                flags_base += fmt::format(" /ifcSearchDir $builddir/{}", target->module);
            }

            for (auto& target : include_deps)
            {
                auto dependency_defines = GetRecursiveMapCfg(*target, "cxx-compile-definitions-public");

                for (const std::pair<YAML::Node, YAML::Node>& kv : dependency_defines)
                {
                    auto name = kv.first.as<std::string>();
                    auto value = kv.second.as<std::string>();

                    if (!definitions_pub[name])
                        definitions_pub[name] = value;
                }
            }

            for (const std::pair<YAML::Node, YAML::Node>& kv : definitions_pub)
            {
                auto name = kv.first.as<std::string>();
                auto value = kv.second.as<std::string>();

                if (!definitions[name])
                    definitions[name] = value;
            }

            for (const std::pair<YAML::Node, YAML::Node>& kv : definitions)
            {
                auto name = kv.first.as<std::string>();
                auto value = kv.second.as<std::string>();

                flags_base += " /D" + name + "=" + value;
            }

            BuildRule rule_regular;
            rule_regular.vars["deps"] = "msvc";

            rule_regular.name = "msvc_cxx_" + path;
            rule_regular.tool = "msvc_cxx";
            rule_regular.cmdline = fmt::format("{} /c $in /Fo:$out", flags_base);
            rule_regular.description = "Building C++ source $in";

            BuildRule rule_ixx;
            rule_ixx.vars["deps"] = "msvc";

            rule_ixx.name = "msvc_cxx_mi_" + path;
            rule_ixx.tool = "msvc_cxx";
            rule_ixx.cmdline = fmt::format("{} /interface /c $in /Fo:$out", flags_base);
            rule_ixx.description = "Building C++ module interface source $in";

            std::string deps_input = "";

            for (auto& dep : target.dependencies)
            {
                if (!dep.resolved)
                    throw TargetLoadException("unresolved dependency " + dep.name + " at build generation time");

                if (dep.resolved->type != TargetType::StaticLibrary)
                    continue;

                bool skip = false;                

                for (auto& dep_in : target.dependencies)
                {
                    for (auto& dep_in_in : dep_in.resolved->dependencies)
                        if (dep_in_in.resolved == dep.resolved)
                            skip = true;
                }

                if (skip)
                    continue;

                bool has_any_eligible_sources = false;
                for (auto& file : dep.resolved->sources)
                    if (file.provider == this)
                    {
                        has_any_eligible_sources = true;
                        break;
                    }

                if (has_any_eligible_sources)
                {
                    deps_input += "$msvc_artifact_" + GetEscapedModulePath(*dep.resolved);
                    deps_input += " ";
                }
            }

            BuildRule rule_link;

            rule_link.name = "msvc_link_" + path;
            rule_link.tool = "msvc_link";
            rule_link.cmdline = fmt::format("$msvc_lflags $target_custom_flags {}$in /OUT:$out", deps_input);
            rule_link.description = "Linking target $out";

            BuildRule rule_lib;

            rule_lib.name = "msvc_lib_" + path;
            rule_lib.tool = "msvc_lib";
            rule_lib.cmdline = fmt::format("$msvc_lflags $target_custom_flags {}$in /OUT:$out", deps_input);
            rule_lib.description = "Archiving target $out";

            desc.rules.emplace_back(std::move(rule_regular));
            desc.rules.emplace_back(std::move(rule_ixx));
            desc.rules.emplace_back(std::move(rule_link));
            desc.rules.emplace_back(std::move(rule_lib));

            desc.vars["msvc_path_" + path] = target.path;
            desc.vars["msvc_config_path_" + path] = target.config_path;

            return true;
        }

        void ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file)
        {
            if (file.provider != this)
                return;

            auto path = GetEscapedModulePath(target);

            BuildTarget build_target;
            auto local_path = file.path.substr(target.path.size() + 1);

            build_target.in = "$msvc_path_" + path + "/" + local_path;
            build_target.out = fmt::format("$builddir/{}/{}.obj", target.module, local_path);

            if (file.extension == ".ixx")
                build_target.rule = "msvc_cxx_mi_" + path;
            else
                build_target.rule = "msvc_cxx_" + path;

            desc.targets.emplace_back(std::move(build_target));
        }

        void CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target)
        {
            auto path = GetEscapedModulePath(target);

            BuildTarget link_target;

            link_target.out = "$builddir/artifacts/" + target.module;
            link_target.rule = "msvc_link_" + path;

            switch (target.type)
            {
            case TargetType::Executable:
                link_target.out += ".exe";
                break;
            case TargetType::StaticLibrary:
                link_target.out += ".lib";
                link_target.rule = "msvc_lib_" + path;
                break;
            case TargetType::SharedLibrary:
                link_target.out += ".dll";
                link_target.vars["target_custom_flags"] += " /DLL";
                break;
            }

            for (auto& file : target.sources)
            {
                if (file.provider == this)
                {
                    auto local_path = file.path.substr(target.path.size() + 1);

                    link_target.in += fmt::format("$builddir/{}/{}.obj", target.module, local_path);
                    link_target.in += " ";
                }
            }

            for (auto& dep : target.dependencies)
                if (dep.resolved && dep.resolved->type != TargetType::Custom)
                    link_target.deps.push_back(dep.resolved->module);

            link_target.deps.push_back("$msvc_config_path_" + path);

            BuildTarget alias_target;

            alias_target.in = link_target.out;
            alias_target.out = target.module;
            alias_target.rule = "phony";

            desc.vars["msvc_artifact_" + path] = link_target.out;

            desc.targets.emplace_back(std::move(link_target));
            desc.targets.emplace_back(std::move(alias_target));
        }

    private:
        TargetConfig GetRecursiveMapCfg(const Target& leaf, std::string_view key)
        {
            auto result = TargetConfig{ YAML::NodeType::Map };
            auto p = &leaf;

            while (p)
            {
                if (auto map = leaf.GetCfgEntry<TargetConfig>(key))
                {
                    for(const std::pair<YAML::Node, YAML::Node>& kv : *map)
                    {
                        auto type = kv.first.as<std::string>();
                        auto& yaml = kv.second;

                        if (!result[type])
                            result[type] = yaml;
                    }
                }

                p = p->parent;
            }

            return result;
        }

        std::string GetEscapedModulePath(const Target& target)
        {
            auto module_escaped = target.module;
            std::replace(module_escaped.begin(), module_escaped.end(), '.', '_');
            return module_escaped;
        }
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
    void GenerateNinjaBuildFile(BuildEnv& env, const std::string& out_dir)
    {
        auto targets = env.GetTargetsInDependencyOrder();

        std::string path = out_dir + "/build.ninja";

        std::unique_ptr<std::FILE, decltype(&std::fclose)> file{ std::fopen(path.data(), "w"), &std::fclose };

        constexpr auto kDefaultLinkerPath = R"(C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Tools\MSVC\14.29.30037\bin\Hostx64\x64\)";
        constexpr auto kDefaultLibPath = R"(C:\Program Files\\ (x86)\Windows\\ Kits\10\Lib\10.0.19041.0\um\x64)";

        fmt::print(file.get(), "cc = {}/cl.exe\n", kDefaultLinkerPath);
        fmt::print(file.get(), "link = {}/link.exe\n", kDefaultLinkerPath);

        fmt::print(file.get(), "builddir = .\n", out_dir);

        fmt::print(file.get(), "cflags = /nologo /std:c++latest /experimental:module /EHsc /MD\n");
        fmt::print(file.get(), "lflags = /LIBPATH:{}\n", kDefaultLibPath);

        auto cflags_plus_inout = "$cflags /c $in /Fo:$out";

        fmt::print(file.get(), "rule cxx\n");
        fmt::print(file.get(), "    command = $cc {}\n", cflags_plus_inout);
        fmt::print(file.get(), "    description = CXX $in\n");

        fmt::print(file.get(), "rule cxx_module_iface\n");
        fmt::print(file.get(), "    command = $cc /interface {}\n", cflags_plus_inout);
        fmt::print(file.get(), "    description = CXX_MI $in\n");

        fmt::print(file.get(), "rule link\n");
        fmt::print(file.get(), "    command = $link $in /OUT:$out\n");
        fmt::print(file.get(), "    description = LINK $out\n");

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

    void GenerateNinjaBuildFile(const NinjaBuildDesc& desc, const std::string& out_dir)
    {
        constexpr auto kToolPrefix = "re_tool_";

        std::string path = out_dir + "/build.ninja";

        fmt::ostream out = fmt::output_file(path);

        out.print("builddir = .\n");

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
}

int main(int argc, const char** argv)
{
    try
    {
        re::BuildEnv env;

        re::MsvcCCppLangProvider provider;
        env.AddLangProvider("cpp", &provider);

        auto& root = env.LoadRootTarget(RE_TEST_EXAMPLES_FOLDER "/complex-project");

        // re::DumpTargetStructure(root);

        auto targets = env.GetTargetsInDependencyOrder();
        for (auto& target : targets)
            fmt::print("{} v\n", target->module);

        auto out_dir = root.GetCfgEntry<std::string>("out-dir").value_or("out");

        std::filesystem::create_directories(out_dir);

        auto desc = env.GenerateBuildDesc();
        re::GenerateNinjaBuildFile(desc, out_dir);

        auto path_to_me = std::filesystem::path{ argv[0] }.parent_path();
        auto path_to_ninja = path_to_me / "ninja.exe";

        reproc::options options;
        options.redirect.parent = true;

        reproc::process ninja_process;
        std::vector<std::string> cmdline;

        cmdline.push_back(path_to_ninja.string());
        cmdline.push_back("-C");
        cmdline.push_back("./out");

        for(auto i = 1; i < argc; i++)
            cmdline.push_back(argv[i]);

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
            fmt::print("\n [{}] Build failed! (exit_code={})\n", root.module, exit_code);
            return -1;
        }
    }
    catch (const re::TargetLoadException& e)
    {
        fmt::print("\n!!! [FATAL] Failed to load target: {}\n", e.what());
    }
}