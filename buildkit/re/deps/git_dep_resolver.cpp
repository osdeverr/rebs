#include <boost/process.hpp>

#include "git_dep_resolver.h"

#include <fmt/color.h>
#include <fmt/format.h>

#include <re/fs.h>
#include <re/process_util.h>

#include <re/target_cfg_utils.h>
#include <re/yaml_merge.h>

#include <re/deps_version_cache.h>

#include <fstream>

namespace re
{
    Target *GitDepResolver::ResolveTargetDependency(const Target &target, const TargetDependency &dep,
                                                    DepsVersionCache *cache)
    {
        return ResolveGitDependency(target, dep, dep.name, dep.version, cache);
    }

    Target *GitDepResolver::ResolveGitDependency(const Target &target, const TargetDependency &dep,
                                                 std::string_view url, std::string branch, DepsVersionCache *cache)
    {
        if (cache)
        {
            auto git_ls_remote = [](const re::TargetDependency &, std::string_view url) -> std::vector<std::string> {
                boost::process::ipstream is; // reading pipe-stream
                boost::process::child c(boost::process::search_path("git"), "ls-remote", "--refs", "--tags", url.data(),
                                        boost::process::std_out > is);

                c.wait();

                std::vector<std::string> result;

                while (!is.eof())
                {
                    std::string hash;
                    auto &tag = result.emplace_back();

                    is >> hash;
                    is >> tag;

                    if (tag.empty())
                        continue;

                    // fmt::print("test: {} {}\n", hash, tag);

                    constexpr char kRefsTags[] = "refs/tags/";
                    auto pos = tag.find(kRefsTags);
                    if (pos != tag.npos)
                        tag.erase(pos, sizeof kRefsTags - 1);
                }

                //  fmt::print("running: {} eof: {}\n", c.running(), is.eof());

                return result;
            };

            branch = cache->GetLatestVersionMatchingRequirements(target, dep, url, git_ls_remote);
        }

        auto cached_dir = fmt::format("git.{}.{}@{}", dep.ns, dep.name, branch);

        cached_dir.erase(std::remove(cached_dir.begin(), cached_dir.end(), ' '), cached_dir.end());

        // Hack lol
        for (auto &c : cached_dir)
            if (c == '/' || c == ':')
                c = '_';

        auto [scope, context] = target.GetBuildVarScope();

        auto re_arch = scope.ResolveLocal("arch");
        auto re_platform = scope.ResolveLocal("platform");
        auto re_config = scope.ResolveLocal("configuration");

        auto triplet = fmt::format("{}-{}-{}", re_arch, re_platform, re_config);

        if (dep.extra_config_hash)
            triplet += fmt::format("-ecfg-{}", dep.extra_config_hash);

        auto cache_path = cached_dir + "-" + triplet;

        std::string cutout_filter = "";

        if (dep.filters.size() >= 1 && dep.filters[0].front() == '/')
        {
            cutout_filter = dep.filters[0].substr(1);
            cache_path += cutout_filter;
        }

        if (auto &cached = mTargetCache[cache_path])
            return cached.get();

        auto cache_name = ".re-cache";
        auto git_cached = target.root_path / cache_name / cached_dir;

        fs::create_directories(git_cached);

        auto dep_str = dep.ToString();

        if (!fs::exists(git_cached / ".git"))
        {
            if (scope.ResolveLocal("auto-load-uncached-deps") != "true")
                RE_THROW TargetUncachedDependencyException(
                    &target, "Cannot resolve uncached dependency {} - autoloading is disabled", dep_str);

            mOut->Info(fmt::emphasis::bold | fg(fmt::color::light_blue), "[{}] Restoring package {}...\n",
                       target.module, dep_str);

            auto start_time = std::chrono::high_resolution_clock::now();

            fs::remove_all(git_cached);
            DownloadGitDependency(url, branch, git_cached);

            auto end_time = std::chrono::high_resolution_clock::now();

            mOut->Info(fmt::emphasis::bold | fg(fmt::color::light_blue), "\n[{}] Restored package {} ({:.2f}s)\n",
                       target.module, dep_str,
                       std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() / 1000.f);
        }
        else
        {
            mOut->Info(fmt::emphasis::bold | fg(fmt::color::light_blue), "[{}] Package {} already available\n",
                       target.module, dep_str);
        }

        if (cutout_filter.size())
            git_cached /= cutout_filter;

        auto &result = (mTargetCache[cache_path] = mLoader->LoadFreeTarget(git_cached, &target, &dep));

        result->root_path = target.root_path;

        result->config["arch"] = re_arch;
        result->config["platform"] = re_platform;
        result->config["configuration"] = re_config;

        if (dep.extra_config)
            MergeYamlNode(result->config, dep.extra_config);

        result->var_parent = target.var_parent;
        result->local_var_ctx = context;
        result->build_var_scope.emplace(&result->local_var_ctx, "build", &scope);

        result->module = fmt::format("git.{}.{}", triplet, result->module);

        result->LoadDependencies();
        result->LoadMiscConfig();
        result->LoadSourceTree();

        mLoader->RegisterLocalTarget(result.get());
        return result.get();
    }

    void GitDepResolver::DownloadGitDependency(std::string_view url, std::string_view branch, const fs::path &to)
    {
        std::vector<std::string> cmdline = {"git", "clone", "--depth", "1"};

        if (!branch.empty())
        {
            cmdline.emplace_back("--branch");
            cmdline.emplace_back(branch.data());
        }

        cmdline.emplace_back(url.data());
        cmdline.emplace_back(to.u8string());

        RunProcessOrThrow("git", {}, cmdline, false, true);
    }

    bool GitDepResolver::SaveDependencyToPath(const TargetDependency &dep, const fs::path &path)
    {
        fs::create_directories(path);
        DownloadGitDependency(dep.name, dep.version, path);
        return true;
    }
} // namespace re
