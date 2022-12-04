#pragma once
#include <re/fs.h>

#ifdef WIN32
#include <Windows.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <limits.h>
#endif

namespace re
{
	inline fs::path GetCurrentExecutablePath()
	{
#if defined(WIN32)
		char buf[256] = "";
		GetModuleFileNameA(NULL, buf, sizeof buf);

#elif defined(__APPLE__)
		char buf[PATH_MAX];
		uint32_t bufsize = PATH_MAX;
		_NSGetExecutablePath(buf, &bufsize);

#elif defined(__linux__)
		auto buf = "/proc/self/exe";
#endif

		return fs::canonical(buf).parent_path();
	}

	inline fs::path GetReDataPath()
	{
		return GetCurrentExecutablePath();

#ifdef WIN32
		return GetCurrentExecutablePath();
#else
		return "/etc/rebs/";
#endif
	}

	inline fs::path GetReDynamicDataPath()
	{
#ifdef WIN32
		return GetCurrentExecutablePath();
#else
		return fs::path{std::getenv("HOME")} / ".re-dyn-data";
#endif
	}
}
