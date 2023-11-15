#include "source_translation.h"

#include <re/process_util.h>

#include <boost/algorithm/string.hpp>

namespace re
{
    void SourceTranslation::ProcessTargetPostInit(Target &target)
    {
        auto out_dir =
            target.build_var_scope->GetVar("source-translation-temp-dir").value_or(".re-cache/source-translation");
        auto out_root = target.root_path / out_dir / target.module;

        for (auto &source : target.sources)
        {
            if (boost::algorithm::starts_with(source.path, out_root))
                continue;

            auto in_path = source.path;
            auto base_path = source.path.lexically_relative(target.path).u8string();

            int num_steps = 0;

            for (auto step : target.resolved_config["source-translation-steps"])
            {
                auto vars = LocalVarScope{&*target.build_var_scope, "translation"};

                auto step_suffix = fmt::format("_st_step{}", num_steps);

                auto step_name = step["name"];

                auto out_ext_node = step["out-extension"];
                auto out_ext = out_ext_node ? out_ext_node.Scalar() : source.extension;

                auto out_path = out_root / (base_path + step_suffix + "." + out_ext);

                vars.SetVar("source-file", in_path.generic_u8string());
                vars.SetVar("out-file", out_path.generic_u8string());

                auto extensions = step["extensions"];

                bool supported = false;

                if (extensions)
                {
                    for (auto extension : extensions)
                    {
                        if (source.extension == extension.Scalar())
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

                std::vector<std::string> command;
                boost::algorithm::split(command, step["command"].Scalar(), boost::is_any_of(" "));

                for (auto &part : command)
                    part = vars.Resolve(part);

                fs::create_directories(out_path.parent_path());

                if (target.build_var_scope->GetVar("building-sources").value_or("false") == "true" ||
                    target.build_var_scope->GetVar("do-source-translation").value_or("false") == "true")
                {
                    re::RunProcessOrThrow(step_name ? step_name.Scalar() : step_suffix.substr(1), {}, command, true, true);
                }
                    
                // fmt::print("translating {} -> {}\n", source.path.generic_u8string(), out_path.generic_u8string());

                target.unused_sources.push_back(source);

                source.path = out_path;
                source.extension = out_ext;

                in_path = out_path;

                num_steps++;
            }
        }
    }
} // namespace re
