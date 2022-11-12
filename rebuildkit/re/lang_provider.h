#pragma once
#include <string_view>

namespace re
{
	class ILangProvider
	{
	public:
		virtual ~ILangProvider() = default;

		virtual bool SupportsFileExtension(std::string_view extension) = 0;

		// TODO: Process methods...
	};
}
