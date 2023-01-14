#include "github_dep_resolver.h"

#include <boost/algorithm/string/predicate.hpp>

namespace re
{
	Target* GithubDepResolver::ResolveTargetDependency(const Target& target, const TargetDependency& dep, DepsVersionCache* cache)
	{
		auto url = dep.name;

		if (!boost::ends_with(url, ".git"))
			url.append(".git");

		auto temp = std::getenv("RE_GITHUB_FORCE_SSH");
		auto force_ssh = temp && !strcmp(temp, "1");

		if (dep.ns == "github-ssh" || force_ssh)
			return mGit->ResolveGitDependency(target, dep, fmt::format("git@github.com:{}", url), dep.version, cache);
		else
			return mGit->ResolveGitDependency(target, dep, fmt::format("https://github.com/{}", url), dep.version, cache);
	}
	
	bool GithubDepResolver::SaveDependencyToPath(const TargetDependency& dep, const fs::path& path)
	{
        fs::create_directories(path);

		auto url = dep.name;

		if (!boost::ends_with(url, ".git"))
			url.append(".git");

		auto temp = std::getenv("RE_GITHUB_FORCE_SSH");
		auto force_ssh = temp && !strcmp(temp, "1");

		if (dep.ns == "github-ssh" || force_ssh)
			mGit->DownloadGitDependency(fmt::format("git@github.com:{}", url), dep.version, path);
		else
			mGit->DownloadGitDependency(fmt::format("https://github.com/{}", url), dep.version, path);
		
		return true;
	}
}
