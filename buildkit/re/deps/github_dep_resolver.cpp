#include "github_dep_resolver.h"

#include <boost/algorithm/string/predicate.hpp>

namespace re
{
	Target* GithubDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep)
	{
		auto path = dep.name;

		if (!boost::ends_with(path, ".git"))
			path.append(".git");

		auto temp = std::getenv("RE_GITHUB_FORCE_SSH");
		auto force_ssh = temp && !strcmp(temp, "1");

		if (dep.ns == "github-ssh" || force_ssh)
			return mGit->ResolveGitDependency(target, dep, fmt::format("git@github.com:{}", path), dep.version);
		else
			return mGit->ResolveGitDependency(target, dep, fmt::format("https://github.com/{}", path), dep.version);
	}
}
