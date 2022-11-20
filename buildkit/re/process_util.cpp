#include "process_util.h"
#include "target.h"

#include <fmt/format.h>

#include <reproc++/reproc.hpp>

namespace re
{
    int RunProcessOrThrow(std::string_view program_name, const std::vector<std::string>& cmdline, bool output, bool throw_on_bad_exit)
    {
        reproc::options options;
        options.redirect.parent = output;

        reproc::process process;

        auto start_ec = process.start(cmdline, options);
        if (start_ec)
        {
            RE_THROW ProcessRunException("{} failed to start: {}", program_name, start_ec.message());
        }

        auto [exit_code, end_ec] = process.wait(reproc::infinite);

        // process.read(reproc::stream::out, );

        if (end_ec)
        {
            RE_THROW ProcessRunException("{} failed to run: {} (exit_code={})", program_name, end_ec.message(), exit_code);
        }

        if (throw_on_bad_exit && exit_code != 0)
        {
            RE_THROW ProcessRunException("{} failed: exit_code={}", program_name, exit_code);
        }

        return exit_code;
    }
}
