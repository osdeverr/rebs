#pragma once
#include <unordered_map>

namespace re
{
	// We will likely transition to some other map type in the future!
	template<class K, class V>
	using Map = std::unordered_map<K, V>;
}
