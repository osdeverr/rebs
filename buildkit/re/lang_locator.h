#pragma once
#include <ulib/string.h>
#include "lang_provider.h"

namespace re
{
	class ILangLocator
	{
	public:
		virtual ~ILangLocator() = default;

		virtual ILangProvider* GetLangProvider(ulib::string_view name) = 0;
	};
}
