#pragma once
#include <filesystem>

#ifdef WIN32
#include <Windows.h>
#endif

namespace re
{
	inline std::filesystem::path GetCurrentExecutablePath()
	{
#ifdef WIN32
		char buf[256] = "";
		GetModuleFileNameA(NULL, buf, sizeof buf);
		return std::filesystem::path{ buf }.parent_path();
#else
		return std::filesystem::canonical("/proc/self/exe").directory();
#endif
	}
}
