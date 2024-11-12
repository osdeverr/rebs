#include "source_translation.h"

#include <re/process_util.h>

// #include <boost/algorithm/string.hpp>
#include <ulib/format.h>
#include <ulib/string.h>

namespace re
{
    void SourceTranslation::ProcessTargetPostInit(Target &target)
    {
        auto out_dir =
            target.build_var_scope->GetVar("source-translation-temp-dir").value_or(".re-cache/source-translation");
        auto out_root = target.root_path / out_dir / target.module;

        for (auto &source : target.sources)
        {
            if (ulib::string{source.path.u8string()}.starts_with(out_root.u8string()))
                continue;

            auto in_path = source.path;
            ulib::string base_path = source.path.lexically_relative(target.path).u8string();

            int num_steps = 0;

            if (auto steps = target.resolved_config.search("source-translation-steps"))
            {
                if (steps->is_sequence())
                {
                    for (auto step : *steps)
                    {
                        auto vars = LocalVarScope{&*target.build_var_scope, "translation"};

                        auto step_suffix = ulib::format("_st_step{}", num_steps);

                        auto step_name = step["name"];

                        auto out_ext_node = step["out-extension"];
                        ulib::string out_ext =
                            !out_ext_node.is_null() ? out_ext_node.scalar() : ulib::string_view{source.extension};

                        auto out_path = out_root / (base_path + step_suffix + "." + out_ext);

                        vars.SetVar("source-file", in_path.generic_u8string());
                        vars.SetVar("out-file", out_path.generic_u8string());

                        bool supported = false;

                        auto extensions = step.search("extensions");
                        if (extensions)
                        {
                            for (auto extension : *extensions)
                            {
                                if (extension.scalar() == source.extension)
                                {
                                    supported = true;
                                    break;
                                }
                            }
                        }
                        else
                        {
                            supported = true;
                        }

                        if (!supported)
                            continue;

                        // fmt::print("translating {} -> {}\n", source.path.generic_u8string(),
                        // out_path.generic_u8string());

                        target.unused_sources.push_back(source);

                        source.path = out_path;
                        source.extension = out_ext;

                        in_path = out_path;

                        num_steps++;
                    }
                }
            }
        }
    }

    void SourceTranslation::ProcessTargetPreBuild(Target &target)
    {
        auto out_dir =
            target.build_var_scope->GetVar("source-translation-temp-dir").value_or(".re-cache/source-translation");
        auto out_root = target.root_path / out_dir / target.module;

        for (auto &source : target.unused_sources)
        {
            auto in_path = source.path;
            ulib::string base_path = source.path.lexically_relative(target.path).u8string();

            int num_steps = 0;

            for (auto step : target.resolved_config["source-translation-steps"])
            {
                auto vars = LocalVarScope{&*target.build_var_scope, "translation_exec"};

                ulib::string step_suffix = ulib::format("_st_step{}", num_steps);

                auto out_ext_node = step.search("out-extension");
                auto out_ext = out_ext_node ? out_ext_node->scalar() : ulib::string_view{source.extension};

                auto out_path = out_root / (base_path + step_suffix + "." + out_ext);

                vars.SetVar("source-file", in_path.generic_u8string());
                vars.SetVar("out-file", out_path.generic_u8string());

                auto extensions = step["extensions"];

                ulib::string sc = step["command"].scalar();
                ulib::list<ulib::string> command = sc.split(" ");

                for (auto &part : command)
                    part = vars.Resolve(part);

                fs::create_directories(out_path.parent_path());

                auto step_name = step.search("name");
                re::RunProcessOrThrow(step_name ? step_name->scalar() : step_suffix.substr(1), {}, command, true,
                                      true);

                // fmt::print("translating {} -> {}\n", source.path.generic_u8string(), out_path.generic_u8string());

                in_path = out_path;

                num_steps++;
            }
        }
    }
} // namespace re
