#pragma once
#include <string_view>
#include <memory>

#include <re/fs.h>

namespace re
{
	class Target;

	struct ITargetLoader
	{
		virtual ~ITargetLoader() = default;
		
		virtual std::unique_ptr<Target> LoadFreeTarget(const fs::path& path) = 0;
		virtual Target* GetCoreTarget() = 0;
		virtual void RegisterLocalTarget(Target* pTarget) = 0;
	};
}