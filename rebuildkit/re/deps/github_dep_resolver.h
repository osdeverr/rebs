#pragma once
#include "git_dep_resolver.h"

namespace re
{
	class GithubDepResolver : public IDepResolver
	{
	public:
		GithubDepResolver(GitDepResolver* pGit)
			: mGit{ pGit }
		{}

		Target* ResolveTargetDependency(const Target& target, const TargetDependency& dep);

	private:
		GitDepResolver* mGit;
	};
}
