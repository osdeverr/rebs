#pragma once

#if __has_include(<filesystem>)
#include <filesystem>

namespace re
{
	namespace fs = std::filesystem;
}
#else
#include <ghc/filesystem.hpp>

namespace re
{
	namespace fs = ghc::filesystem;
}
#endif
