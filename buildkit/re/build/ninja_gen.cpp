#include "ninja_gen.h"

#include <fmt/ostream.h>

#include <fstream>

#include <ulib/format.h>
#include <ulib/fmt/list.h>
#include <ulib/fmt/path.h>

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

        for (auto& [key, val] : desc.init_vars)
            out.print("{} = {}\n", key, val);

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
