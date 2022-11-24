#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <optional>

#include "error.h"

namespace re
{
    int RunProcessOrThrow(
        std::string_view program_name,
        const std::vector<std::string>& cmdline,
        bool output = false,
        bool throw_on_bad_exit = false,
        std::optional<std::string_view> working_directory = std::nullopt
    );
    int RunProcessOrThrowWindows(
        std::string_view program_name,
        const std::vector<std::wstring>& cmdline,
        bool output = false,
        bool throw_on_bad_exit = false,
        std::optional<std::wstring_view> working_directory = std::nullopt
    );

    class ProcessRunException : public Exception
    {
    public:
        using Exception::Exception;
    };
}
