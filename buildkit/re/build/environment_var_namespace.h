#pragma once
#include <re/vars.h>

namespace re
{
	struct EnvironmentVarNamespace : public IVarNamespace
	{
		std::optional<ulib::string> GetVar(ulib::string_view key) const;
	};
}
