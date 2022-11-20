#pragma once
#include <string_view>
#include "lang_provider.h"

namespace re
{
	class ILangLocator
	{
	public:
		virtual ~ILangLocator() = default;

		virtual ILangProvider* GetLangProvider(std::string_view name) = 0;
	};
}
