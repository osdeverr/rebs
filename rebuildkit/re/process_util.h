#pragma once
#include <vector>
#include <string>
#include <string_view>

namespace re
{
    int RunProcessOrThrow(std::string_view program_name, const std::vector<std::string>& cmdline, bool output = false, bool throw_on_bad_exit = false);
}
