#pragma once

#if __has_include(<filesystem>)
#include <filesystem>

namespace re
{
    namespace fs = std::filesystem;
}
#else
#error Your compiler does not fully support C++17! Re requires the <filesystem> header to be present.
#endif
