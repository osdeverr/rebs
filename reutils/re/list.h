#pragma once
#include <vector>

namespace re
{
	// We will likely transition to some other container type in the future!
	template<class T>
	using List = std::vector<T>;
}
