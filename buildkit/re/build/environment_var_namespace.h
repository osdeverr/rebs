#pragma once
#include <re/vars.h>

namespace re
{
	struct EnvironmentVarNamespace : public IVarNamespace
	{
		std::optional<std::string> GetVar(const std::string& key) const;
	};
}
