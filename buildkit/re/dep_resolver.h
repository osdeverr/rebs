#pragma once
#include "target.h"

#include <string_view>
#include <memory>

#include <re/vars.h>
#include <re/fs.h>

namespace re
{
	struct IDepResolver
	{
		virtual ~IDepResolver() = default;

		virtual Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep) = 0;
		virtual Target* ResolveCoercedTargetDependency(const Target& target, const Target& dep) { return nullptr; }

		virtual bool SaveDependencyToPath(const TargetDependency& dep, const fs::path& path) { return false; }
	};
}
