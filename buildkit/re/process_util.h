#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>


#include "error.h"
#include "fs.h"

#include <ulib/string.h>

namespace re
{
    int RunProcessOrThrow(ulib::string_view program_name, const fs::path &path, const ulib::list<ulib::string>& cmdline,
                                 bool output, bool throw_on_bad_exit, std::optional<fs::path> working_directory = std::nullopt);

// #ifdef WIN32
//     int RunProcessOrThrowWindows(std::string_view program_name, std::vector<std::string> cmdline, bool output = false,
//                                  bool throw_on_bad_exit = false,
//                                  std::optional<fs::path> working_directory = std::nullopt);
// #endif

    class ProcessRunException : public Exception
    {
    public:
        using Exception::Exception;
    };
} // namespace re
