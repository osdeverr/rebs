#include "cxx_header_projection.h"

namespace re
{
    void CxxHeaderProjection::ProcessTargetPostInit(Target &target)
    {
        auto symlink_root = target.build_var_scope->ResolveLocal("cxx-header-projection-root");
        auto symlinks_cache = target.root_path / symlink_root / target.module;

        // target.config["cxx-root-include-path"] = symlinks_cache.u8string();
        target.resolved_config["cxx-root-include-path"] = symlinks_cache.u8string();

        if (fs::exists(symlinks_cache))
            return;

        auto full_module = target.module;

        auto current = &target;

        auto leaf_name = current->name;
        auto leaf_path = current->path;

        fs::path path = "";

        while (current)
        {
            auto override_path = target.GetCfgEntry<std::string>("cxx-header-projection-path", CfgEntryKind::Recursive);

            if (override_path)
            {
                path = *override_path / path;
                break;
            }
            else
                path = current->name / path;

            current = current->parent;
        }

        path = symlinks_cache / path;

        fs::create_directories(path.parent_path().parent_path());

        // fmt::print(" * HEADER PROJECTION: Creating symlink {} => {}\n", leaf_path.u8string(), path.u8string());

        // if (!::CreateSymbolicLinkW(to.wstring().c_str(), from.wstring().c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
        //     fmt::print("Error: {}\n", ::GetLastError());

        std::error_code ec;
        fs::create_directory_symlink(leaf_path, path, ec);

        if (ec)
            fmt::print("Error creating symlink: {} {}\n", ec.message(), ec.value());
    }
}