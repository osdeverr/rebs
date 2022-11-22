#include "github_dep_resolver.h"

#include <boost/algorithm/string/predicate.hpp>

namespace re
{
	Target* GithubDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		auto path = dep.name;

		if (!boost::ends_with(path, ".git"))
			path.append(".git");

		if (dep.ns == "github-ssh")
			return mGit->ResolveGitDependency(target, dep, fmt::format("git@github.com:{}", path), dep.version);
		else
			return mGit->ResolveGitDependency(target, dep, fmt::format("https://github.com/{}", path), dep.version);
	}
}
