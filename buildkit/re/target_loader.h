#pragma once
#include <string_view>
#include <memory>

namespace re
{
	class Target;

	struct ITargetLoader
	{
		virtual ~ITargetLoader() = default;

		virtual std::unique_ptr<Target> LoadFreeTarget(const std::string& path) = 0;
		virtual Target* GetCoreTarget() = 0;
		virtual void RegisterLocalTarget(Target* pTarget) = 0;
	};
}
