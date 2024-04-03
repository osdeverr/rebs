#include <re/packages/re_package_client.h>

#include "re/error.h"
#include <filesystem>
#include <re/build/default_build_context.h>
#include <re/build/ninja_gen.h>

#include <re/path_util.h>
#include <re/process_util.h>

#include <re/deps_version_cache.h>
#include <re/version.h>

#include <fmt/args.h>
#include <fmt/color.h>
#include <fmt/format.h>
#include <fmt/os.h>
#include <fmt/ostream.h>

#include <fstream>
#include <iostream>

// #include <boost/algorithm/string.hpp>
// #include <filesystem>

#include <ulib/format.h>
#include <ulib/split.h>
#include <ulib/string.h>

#ifdef WIN32
#include <ulib/env.h>
#include <ulib/process.h>

namespace re
{
    void SetupMsvcEnv(re::DefaultBuildContext &context)
    {
        try
        {
            const auto kVcInfoStyle = fg(fmt::color::dim_gray); // bg(fmt::color::orange_red);

            auto existing_vc_tools = std::getenv("VCToolsVersion");

            if (existing_vc_tools != nullptr || context.GetVar("no-auto-msvc-env") == "true")
            {
                if (existing_vc_tools)
                    context.Debug(kVcInfoStyle, "\n- Using existing MSVC toolchain (version: {})\n", existing_vc_tools);
                else
                    context.Debug(kVcInfoStyle, "\n- MSVC toolchain not defined! This might cause errors.\n");

                return;
            }

            ulib::process find_msvc{re::GetCurrentExecutablePath() / "win32-helpers" / "find-msvc.exe",
                                    {},
                                    ulib::process::pipe_stdout | ulib::process::pipe_stderr};

            if (find_msvc.wait() != 0)
            {
                auto result = nlohmann::json::parse(find_msvc.err().read_all());
                RE_THROW re::Exception("Failed to set up MSVC environment:\n  [{}] {}",
                                       result["error_code"].get<std::string>(),
                                       result["error_message"].get<std::string>());
            }

            auto result = nlohmann::json::parse(find_msvc.out().read_all());

            for (auto &[key, value] : result["environment"].items())
            {
                // fmt::print("{} -> {}\n", key, value.get<std::string>());

                context.SetVar(key, value.get<std::string>());
                ulib::setenv(key, value.get<std::string>());
            }

            /*
            ulib::add_path(context.GetVars().Resolve("${env:WindowsSdkVerBinPath}/${re:host-arch}"));
            ulib::add_path(context.GetVars().Resolve(
                "${env:VSINSTALLDIR}/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin"));
            ulib::add_path(
                context.GetVars().Resolve("${env:VSINSTALLDIR}/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"));
            ulib::add_path(
                context.GetVars().Resolve("${env:VCToolsInstallDir}/bin/Host${re:host-arch}/${re:host-arch}"));
            */

            context.Debug(kVcInfoStyle, "\n- Using MSVC toolchain (version: {})\n",
                          result["vc_tools_version"].get<std::string>());
        }
        catch (const std::exception &e)
        {
            const auto kWarnStyle = fmt::emphasis::bold | fg(fmt::color::yellow); // bg(fmt::color::orange_red);

            context.Warn({}, "\n");
            context.Warn(kWarnStyle,
                         " ! Failed to automatically set up the MSVC environment:\n"
                         "     {}\n"
                         "     (type: {})\n\n"
                         "   The build may fail unless you manually initialize MSVC!\n",
                         e.what(), typeid(e).name());
            context.Warn({}, "\n");
        }
    }
} // namespace re
#endif

int main(int argc, const char **argv)
{
#ifdef WIN32
    std::setlocale(LC_ALL, "en-US.utf8");

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    SetThreadUILanguage(LANG_ENGLISH);
    SetThreadLocale(MAKELCID(LANG_ENGLISH, SORT_DEFAULT));

#endif

    // auto code = __std_fs_code_page();
    // __std_fs_code_page();
    // sizeof(code);

    // printf("codepage: %d\n", code);

    re::DefaultBuildContext context;

    try
    {
        std::vector<std::string_view> args(argv, argv + argc);

        std::unordered_map<std::string, std::string> target_cfg_overrides;

        // TODO: Add error handling to variable parsing
        for (auto it = args.begin(); it != args.end();)
        {
            constexpr char kDefaultPrefix[] = "--";
            constexpr char kVarPrefix[] = "--var.";
            constexpr char kTargetPrefix[] = "--target.";

            // NPM-style -- parameter breaker handling
            if (*it == kDefaultPrefix)
            {
                args.erase(it, it + 1);
                break;
            }

            if (it->find(kTargetPrefix) == 0)
            {
                auto key = it->substr(sizeof kTargetPrefix - 1);
                auto &value = *(it + 1);

                target_cfg_overrides[key.data()] = value.data();
                it = args.erase(it, it + 2);
            }
            else if (it->find(kVarPrefix) == 0)
            {
                auto key = it->substr(sizeof kVarPrefix - 1);
                auto &value = *(it + 1);

                context.SetVar(key.data(), value.data());
                it = args.erase(it, it + 2);
            }
            else if (it->find(kDefaultPrefix) == 0)
            {
                auto key = it->substr(sizeof kDefaultPrefix - 1);
                auto &value = *(it + 1);

                // context.Info({}, "setting var {} to {} ({})\n", key, value, kDefaultPrefix);

                context.SetVar(key.data(), value.data());

                it = args.erase(it, it + 2);
            }
            else
                it++;
        }

        auto setup_re_argv = [&context, &args](std::size_t begin_offset) {
            // Setting up the ${re:argv} variable containing the full command line minus the options Re utilizes

            std::string re_argv = "";

            for (auto it = args.begin() + begin_offset; it != args.end(); it++)
            {
                if (!re_argv.empty())
                    re_argv.append(" ");

                re_argv.append(*it);
            }

            context.SetVar("argv", re_argv);
        };

        auto apply_cfg_overrides = [&target_cfg_overrides](re::Target *pTarget) {
            for (auto &[k, v] : target_cfg_overrides)
                pTarget->config[k] = v;
        };

        context.UpdateOutputSettings();
        // context.LoadDefaultEnvironment(L"D:/Programs/ReBS/bin");

        context.SetVar("configuration", "release");

#ifdef WIN32
        SetupMsvcEnv(context);
#endif

        constexpr auto kBuildPathVar = "path";

        auto init_re_env = [&context, &args] {
            if (!context.GetVar(kBuildPathVar))
            {
                // Go down the directory tree until we hit the root, then use the most distant re.yml.

                auto path = re::fs::current_path();
                auto target_path = path;

                while (path.has_relative_path())
                {
                    if (context.GetBuildEnv()->CanLoadTargetFrom(path))
                    {
                        target_path = path;
                    }

                    if (re::fs::exists(path / ".re-root"))
                        break;

                    path = path.parent_path();
                }

                if (!context.GetBuildEnv()->CanLoadTargetFrom(target_path))
                {
                    context.Error(fmt::emphasis::bold | fmt::fg(fmt::color::crimson),
                                  "\nYou are not in a Re target tree!\n"
                                  "Please change your directory or create a new Re project: 're new'\n\n");
                    std::exit(-1);
                }

                re::fs::current_path(target_path);
            }
        };

        auto handle_partial_build = [&context](re::Target *root, std::optional<std::string> filter) -> re::Target * {
            if (!filter)
                return nullptr;

            ulib::list<ulib::string> parts = ulib::split(*filter, ".");

            auto temp = root;

            for (auto &part : parts)
            {
                if (!part.empty())
                    temp = temp->FindChild(part);

                if (!temp)
                    throw re::TargetBuildException(root, "unresolved partial build filter '{}' for '{}'", *filter,
                                                   root->module);
            }

            if (!temp)
                throw re::TargetBuildException(root, "unresolved partial dependency filter '{}' for '{}'", *filter,
                                               root->module);

            context.Info(fg(fmt::color::blue_violet) | fmt::emphasis::bold,
                         "\n ! Partial build - Processing target '{}'\n\n", temp->module);

            return temp;
        };

        context.LoadDefaultEnvironment(re::GetReDataPath(), re::GetReDynamicDataPath());

        auto partial_build_filter = context.GetVar("target");

        if (args.size() == 1)
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            context.SetVar("building-sources", "true");

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);

            return context.BuildTarget(context.GenerateBuildDescForTarget(target, maybe_partial_build));
            // return context.BuildTargetInDir(L"D:/PlakSystemsSW/NetUnitCollection");
        }
        else if (args[1] == "new")
        {
            if (args.size() < 4)
                throw re::Exception("re new: invalid command line\n\tusage: re new <template> <name> [with <additional "
                                    "templates...>] [--path <path>]");

            auto &type = args[2];
            auto &name = args[3];

            std::string path{name};

            if (path.front() == '.')
                path = path.substr(1);

            context.UpdateOutputSettings();

            path = context.GetVar(kBuildPathVar).value_or(path);

            fmt::print("\n");
            context.CreateTargetFromTemplate(path, type, name);

            if (args.size() > 4 && args[4] == "with")
            {
                for (auto it = args.begin() + 5; it != args.end(); it++)
                {
                    auto tpl = *it;

                    if (tpl.back() == ',')
                        tpl = tpl.substr(0, tpl.size() - 1);

                    context.ApplyTemplateInDirectory(path, tpl);
                }
            }

            fmt::print("\n");
            fmt::print("    To build the new target, type:\n");
            fmt::print("        > cd {}\n", path);
            fmt::print("        > re\n");
            fmt::print("\n");
            fmt::print("    To edit the new target, modify the {}/re.yml file.\n", path);
            fmt::print("\n");
        }
        else if (args[1] == "template")
        {
            if (args.size() < 3)
                throw re::Exception("re template: invalid command line\n\tusage: re template <operation> [args]");

            if (args[2] == "apply")
            {
                if (args.size() < 4)
                    throw re::Exception("re template apply: invalid command line\n\tusage: re template apply "
                                        "<templates...> [--path <path>]");

                auto path = context.GetVar(kBuildPathVar).value_or(".");

                fmt::print("\n");

                for (auto it = args.begin() + 3; it != args.end(); it++)
                {
                    auto tpl = *it;

                    if (tpl.back() == ',')
                        tpl = tpl.substr(0, tpl.size() - 1);

                    context.ApplyTemplateInDirectory(path, tpl);
                }

                fmt::print("\n");
            }
            else
                throw re::Exception("re template: invalid operation '{}'\n\tsupported operations: [apply]", args[2]);
        }
        else if (args[1] == "do")
        {
            init_re_env();

            if (args.size() < 3)
                throw re::Exception("re do: invalid command line\n\tusage: re do <action-category> [path | .]");

            setup_re_argv(3);

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            context.SetVar("building-sources", "true");

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);

            auto desc = context.GenerateBuildDescForTarget(target, maybe_partial_build);
            auto env = context.GetBuildEnv();

            auto deps = env->GetSingleTargetDepSet(desc.pBuildTarget);

            auto action_type = args[2];

            for (auto &dep : deps)
                env->RunStructuredTask(dep, &desc, action_type, "pre-build");

            context.BuildTarget(desc);

            auto style = fmt::emphasis::bold | fg(fmt::color::aquamarine);
            context.Info(style, " - Running custom actions for '{}'\n\n", action_type);

            for (auto &dep : deps)
            {
                env->RunStructuredTask(dep, &desc, action_type, "post-build");
                env->RunActionsCategorized(dep, &desc, action_type);
            }

            return 0;
        }
        else if (args[1] == "config" || args[1] == "conf" || args[1] == "cfg")
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");
            auto yaml = context.LoadCachedParams(path);

            if (args.size() == 2)
            {
                // Print the current config and quit.

                YAML::Emitter emitter;
                emitter << yaml;

                fmt::print("{}\n", emitter.c_str());

                return 0;
            }

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
            auto path = args.size() > 2 && args[2].front() != '.' ? args[2] : ".";

            context.SetVar(kBuildPathVar, std::string{path});
            init_re_env();

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            context.SetVar("generate-build-meta", "true");

            if (args.size() > 3 && args[3] == "cached-only")
                context.SetVar("auto-load-uncached-deps", "false");

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto desc = context.GenerateBuildDescForTarget(target);
            context.SaveTargetMeta(desc);

            // fmt::print("{}", desc.meta.dump(4));

            return 0;
        }
        else if (args[1] == "summary")
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);
            context.GenerateBuildDescForTarget(target, maybe_partial_build);

            context.GetBuildEnv()->DebugShowVisualBuildInfo();
        }
        else if (args[1] == "list" || args[1] == "ls")
        {
            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);
            context.GenerateBuildDescForTarget(target, maybe_partial_build);

            const auto kStyleTargetName = fg(fmt::color::sky_blue);
            const auto kStyleTargetType = fg(fmt::color::dim_gray);

            auto targets = context.GetBuildEnv()->GetTargetsInDependencyOrder();

            if (context.GetVar("list-deps").value_or("false") != "true")
            {
                targets.erase(std::remove_if(targets.begin(), targets.end(),
                                             [&context](auto pTarget) {
                                                 return !pTarget->build_var_scope ||
                                                        pTarget->resolved_config["is-external-dep"].Scalar() == "true";
                                             }),
                              targets.end());
            }

            context.Info(fmt::emphasis::bold, "\nTargets loaded ({}):\n", targets.size());

            for (auto &pTarget : targets)
            {
                context.Info({}, " - ");
                context.Info(kStyleTargetName | fmt::emphasis::bold, "{}", pTarget->module);
                context.Info(kStyleTargetType, " {}\n", TargetTypeToString(pTarget->type));
            }

            context.Info(kStyleTargetType, "\n");
        }
        else if (args[1] == "run")
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            context.SetVar("building-sources", "true");

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            const auto style = fmt::emphasis::bold | fg(fmt::color::yellow);

            auto get_run_target = [&context, &target](const std::string &str) -> re::Target * {
                if (auto absolute = context.GetBuildEnv()->GetTargetOrNull(str))
                {
                    return absolute;
                }
                else
                {
                    ulib::list<ulib::string> parts = ulib::split(str, ".");
                    auto temp = &target;

                    for (auto &part : parts)
                    {
                        if (!part.empty())
                            temp = temp->FindChild(part);

                        if (!temp)
                            return nullptr;
                    }

                    return temp;
                }
            };

            re::Target *run_target = nullptr;

            if (auto var = context.GetVar("target"))
            {
                run_target = get_run_target(*var);
            }
            else if (args.size() > 2 /*&& args[2].front() == '.'*/ && (run_target = get_run_target(args[2].data())))
            {
                args.erase(args.begin() + 2);
            }
            else if (auto var = context.GetVar("default-run-target"))
            {
                run_target = get_run_target(*var);
            }
            else
            {
                auto desc = context.GenerateBuildDescForTarget(target);

                // Show a dialog asking the user to choose.

                std::vector<re::Target *> choices;
                std::size_t index;

                for (auto &[target, _] : desc.artifacts)
                {
                    if (target->type != re::TargetType::Executable)
                        continue;

                    choices.push_back((re::Target *)target);
                }

                if (choices.empty())
                {
                    throw re::TargetException("TargetRunException", desc.pBuildTarget, "Nothing to run");
                }
                else if (choices.size() == 1)
                {
                    run_target = choices[0];
                }
                else
                {
                    if (context.GetVar("no-run-choice").value_or("false") == "true")
                        throw re::TargetException("TargetRunException", desc.pBuildTarget,
                                                  "Artifact not specified and can't be interactively selected");

                    context.Info(style, "\n * This project has {} executable targets to run. Please choose one:\n\n",
                                 choices.size());

                    std::size_t i = 0;

                    for (auto &choice : choices)
                    {
                        context.Info(fg(fmt::color::dim_gray), "   [{}] ", i++);
                        context.Info(fg(fmt::color::yellow), "{}\n", choice->module);
                    }

                    context.Info({}, "\n Choose: ", choices.size() - 1);

                    std::cin >> index;

                    run_target = choices.at(index);
                }
            }

            if (!run_target)
                throw re::TargetException("TargetRunException", nullptr, "Couldn't find an artifact to run");

            if (run_target->type != re::TargetType::Executable)
                throw re::TargetException("TargetRunException", nullptr, "Only executable targets can be run");

            setup_re_argv(2);

            auto desc = context.GenerateBuildDescForTarget(target, run_target);
            context.BuildTarget(desc);

            auto it = desc.artifacts.find(run_target);

            if (it != desc.artifacts.end())
            {
                auto &exe_path = it->second;

                std::vector<std::string> run_args(args.begin() + 2, args.end());

                auto working_dir = context.GetVar("working-dir").value_or(exe_path.parent_path().u8string());

                context.Info(style, " * Running target '{}' from '{}'\n\n", run_target->module, exe_path.u8string());
                return re::RunProcessOrThrow(run_target->module, exe_path, run_args, true, false, working_dir);
            }
            else
            {
                throw re::TargetException("TargetRunException", run_target,
                                          "This target does not provide any artifacts");
            }
        }
        else if (args[1] == "version")
        {
            context.Info({}, "\n  Re version: ");
            context.Info(fg(fmt::color::yellow), "{}\n", re::GetBuildVersionTag());
            context.Info({}, "  Re build revision: ");
            context.Info(fg(fmt::color::yellow), "{}\n", re::GetBuildRevision());
            context.Info({}, "  Built at: ");
            context.Info(fg(fmt::color::yellow), "{} {}\n\n", __DATE__, __TIME__);
        }
        else if (args[1] == "tool")
        {
            init_re_env();

            if (args.size() < 3)
                throw re::Exception("re tool requires at least two arguments: re tool <tool> [args...]");

            auto path = ".";

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            auto &target = context.LoadTarget(path);
            apply_cfg_overrides(&target);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);
            auto desc = context.GenerateBuildDescForTarget(target, maybe_partial_build);

            std::vector<std::string> run_args(args.begin() + 2, args.end());

            auto working_dir = context.GetVar("working-dir").value_or(".");
            return re::RunProcessOrThrow("tool", "", run_args, true, false, working_dir);
        }
        else if (args[1] == "upgrade")
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            auto &target = context.LoadTarget(path);
            auto lock_path = target.path / "re-deps-lock.json";

            context.SetVar("clean-deps-cache", "true");

            if (re::fs::exists(lock_path))
                re::fs::remove(lock_path);

            auto maybe_partial_build = handle_partial_build(&target, partial_build_filter);
            context.GenerateBuildDescForTarget(target, maybe_partial_build);
        }
        else
        {
            init_re_env();

            auto path = context.GetVar(kBuildPathVar).value_or(".");

            context.LoadCachedParams(path);
            context.UpdateOutputSettings();

            context.SetVar("building-sources", "true");

            auto root = &context.LoadTarget(path);
            apply_cfg_overrides(root);

            std::size_t partial_paths_offset = 1;

            if (args.size() > 1 && (args[1] == "build" || args[1] == "b"))
                partial_paths_offset++;

            if (args.size() > partial_paths_offset)
            {
                if (!context.GetVar("no-meta"))
                    context.SetVar("no-meta", "true");

                for (auto i = partial_paths_offset; i < args.size(); i++)
                {
                    auto &filter = args[i];

                    auto desc =
                        context.GenerateBuildDescForTarget(*root, handle_partial_build(root, std::string{filter}));
                    context.BuildTarget(desc);

                    // context.Info({}, "\n");
                }
            }
            else
            {
                auto maybe_partial_build = handle_partial_build(root, partial_build_filter);

                auto desc = context.GenerateBuildDescForTarget(*root, maybe_partial_build);
                context.BuildTarget(desc);
            }

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
    catch (const re::TargetUncachedDependencyException &e)
    {
        return 5;
    }
    catch (const std::exception &e)
    {
        try
        {
            std::string message = "";

            const re::ExceptionCallStack *st = boost::get_error_info<re::TracedError>(e);
            if (st)
            {
                int i = 0;

                for (auto &f : *st)
                {
                    auto name = f.function_name();
                    auto path = re::fs::path{f.file_name()};

                    // if (name.find("re::") != name.npos)
                    message.append(fmt::format("  at {} @ {}:{}\n", name, path.filename().u8string(), f.line()));
                }
            }

            const auto kErrorStyle = fmt::emphasis::bold | fg(fmt::color::crimson); // bg(fmt::color::orange_red);

            context.Error({}, "\n");
            context.Error(kErrorStyle, "error: {}\n(type: {})\n{}", e.what(), typeid(e).name(), message);
            context.Error({}, "\n\n");
        }
        catch (const std::exception &ie)
        {
            std::string str = e.what();
            printf("ex: %s\n\n", str.c_str());
            context.Error({}, "Exception occurred while logging exception: {}\n", ie.what());
            context.Error({}, "type: {}\n\n", typeid(e).name());
        }

        /*
                fmt::print(
                    stderr,
                    bg(fmt::color{ 0x090909 }) | fg(fmt::color::light_coral),
                    "\n\n{}", message
                );

                fmt::print(
                    stderr,
                    "\n\n"
                );
                */

        return 1;
    }
}
