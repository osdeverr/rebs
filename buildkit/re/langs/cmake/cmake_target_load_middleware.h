#pragma once
#include <re/target_load_middleware.h>
#include <re/user_output.h>

namespace re
{
    class CMakeTargetLoadMiddleware : public ITargetLoadMiddleware
    {
    public:
        CMakeTargetLoadMiddleware(const fs::path& adapter_path, IUserOutput* out)
        : mAdapterPath{adapter_path}, mOut{out}
        {}

        bool SupportsTargetLoadPath(const fs::path& path);

        std::unique_ptr<Target> LoadTargetWithMiddleware(
            const fs::path& path,
            const Target* ancestor,
            const TargetDependency* dep_source
        );

    private:
        fs::path mAdapterPath;
        IUserOutput* mOut;
    };
}
