#pragma once
#include <string_view>
#include <memory>

#include <re/fs.h>

#include "dep_resolver.h"

namespace re
{
	class Target;
	struct TargetDependency;

	struct ITargetLoader
	{
		virtual ~ITargetLoader() = default;
		
		// The "ancestor" target here is NOT for parenting purposes. It allows load middlewares and other parts of Re
		// to access data about whatever _caused the target to get loaded_. Free targets don't really have parents.
		virtual std::unique_ptr<Target> LoadFreeTarget(
			const fs::path& path,
			const Target* ancestor = nullptr,
			const TargetDependency* dep_source = nullptr
		) = 0;

		virtual Target* GetCoreTarget() = 0;
		virtual void RegisterLocalTarget(Target* pTarget) = 0;
		virtual IDepResolver* GetDepResolver(ulib::string_view name) = 0;
	};
}
