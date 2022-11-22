#include <re/target.h>
#include <re/buildenv.h>
#include <re/path_util.h>
#include <re/build_desc.h>
#include <re/process_util.h>

#include <reproc++/run.hpp>

#include <fmt/format.h>
#include <fmt/color.h>

int main(int argc, const char** argv)
{
    try
    {
        auto path_to_me = re::GetCurrentExecutablePath();

        std::vector<std::string_view> args(argv, argv + argc);

        std::string_view path = ".";

        if (args.size() > 3)
            path = args[3];

        re::BuildEnv env;

        env.LoadCoreProjectTarget(path_to_me / "data" / "core-project");

        if (!re::DoesDirContainTarget(path))
        {
            fmt::print(stderr, " ! Directory '{}' does not contain a valid Re target. Quitting.\n", path);
            return -1;
        }

        auto& root = env.LoadTarget(path);

        auto re_arch = std::getenv("RE_ARCH");
        auto re_platform = std::getenv("RE_PLATFORM");

        auto out_dir = root.path / "out" / fmt::format("{}-{}", re_arch, re_platform);

        if (auto entry = root.GetCfgEntry<std::string>("output-directory"))
            out_dir = fmt::format(*entry, fmt::arg("arch", re_arch), fmt::arg("platform", re_platform));

        re::NinjaBuildDesc desc;

        desc.out_dir = out_dir;
        desc.artifact_out_format = root.GetCfgEntry<std::string>("artifact-dir-format", re::CfgEntryKind::Recursive).value_or("build");

        re::RunProcessOrThrow(
            "target",
            { (desc.out_dir / desc.GetArtifactDirectory(re::ResolveTargetParentRef(args[2].data(), &root)) / args[2].data()).u8string() },
            true,
            false
        );
    }
    catch (const std::exception& e)
    {
        fmt::print(
            fmt::emphasis::bold | bg(fmt::color::dark_red) | fg(fmt::color::white),
            "\n{}\n", e.what()
        );
    }
}
