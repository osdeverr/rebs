#include "cpp2_translation.h"

#include <re/process_util.h>

namespace re
{
    void Cpp2Translation::ProcessTargetPostInit(Target &target)
    {
        auto command = target.build_var_scope->Resolve(target.GetCfgEntry<std::string>("cpp2-cppfront-binary").value_or("cppfront"));
        auto out_dir = target.build_var_scope->GetVar("cpp2-temp-dir").value_or(".re-cache/cppfront");

        for(auto& source : target.sources)
        {
            auto out_file = target.root_path / out_dir / target.module / (source.path.lexically_relative(target.path).u8string() + ".cpp");

            if(source.extension == "cpp2")
            {
                fs::create_directories(out_file.parent_path());

                re::RunProcessOrThrow("cppfront", {
                    command,
                    source.path.u8string(),
                    "-output",
                    out_file.u8string()
                }, true, true);

                source.path = out_file;
                source.extension = "cpp";
            }
        }
    }
}
