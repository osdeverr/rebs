#pragma once
#include "target.h"

#include <string_view>
#include <memory>

namespace re
{
	struct IDepResolver
	{
		virtual ~IDepResolver() = default;

		virtual Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep) = 0;
	};
}