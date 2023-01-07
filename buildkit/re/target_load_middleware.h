#pragma once
#include <re/fs.h>
#include <re/target.h>

namespace re
{
    struct ITargetLoadMiddleware
    {
        virtual ~ITargetLoadMiddleware() = default;

        virtual bool SupportsTargetLoadPath(const fs::path& path) = 0;
        virtual std::unique_ptr<Target> LoadTargetWithMiddleware(const fs::path& path, const Target* ancestor) = 0;
    };
}
