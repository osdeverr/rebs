#pragma once
#include <re/target.h>

namespace re
{
	TargetConfig GetFlatResolvedTargetCfg(const TargetConfig& cfg, const std::unordered_map<std::string, std::string>& mappings);

	TargetConfig GetResolvedTargetCfg(const Target& leaf, const std::unordered_map<std::string, std::string>& mappings);
}
