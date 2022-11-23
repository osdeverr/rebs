#pragma once
#include <re/fs.h>
#include <re/build_desc.h>

namespace re
{
	void GenerateNinjaBuildFile(const NinjaBuildDesc& desc, const fs::path& out_dir);
}
