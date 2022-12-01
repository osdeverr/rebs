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
		return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
	}
	
	inline std::filesystem::path GetReDataPath()
	{
		return GetCurrentExecutablePath();
		
#ifdef WIN32
		return GetCurrentExecutablePath();
#else
		return "/etc/rebs/";
#endif
	}
	
	inline std::filesystem::path GetReDynamicDataPath()
	{
#ifdef WIN32
		return GetCurrentExecutablePath();
#else
		return std::filesystem::path{std::getenv("HOME")} / ".re-dyn-data";
#endif
	}
}
