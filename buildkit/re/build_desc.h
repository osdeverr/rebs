#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <fmt/format.h>

#include <re/fs.h>
#include <re/target.h>
#include <re/vars.h>

#include <tsl/ordered_map.h>

namespace re
{
    using BuildVars = std::unordered_map<std::string, std::string>;

    struct BuildTool
    {
        std::string name;
        std::string path;
    };

    struct BuildRule
    {
        std::string name;
        std::string tool;
        std::string cmdline;
        std::string description;

        BuildVars vars;
    };

    enum class BuildTargetType
    {
        Auxiliar,
        Object,
        Artifact,
        Alias
    };

    struct BuildTarget
    {
        BuildTargetType type;

        std::string rule;
        std::string in;
        std::string out;

        BuildVars vars;

        std::vector<std::string> deps;

        const Target *pSourceTarget = nullptr;
        const SourceFile *pSourceFile = nullptr;
    };

    struct NinjaBuildDesc
    {
        fs::path out_dir;

        std::string object_out_format;
        std::string artifact_out_format;

        // Vars that go in the very beginning of the build file
        BuildVars init_vars;

        // Substituted variables: those WILL end up in the build script
        BuildVars vars;

        // Arbitrary generation state: this won't go anywhere
        BuildVars state;

        std::vector<BuildTool> tools;
        std::vector<BuildRule> rules;
        std::vector<BuildTarget> targets;

        std::vector<std::string> subninjas;

        Target *pRootTarget = nullptr;
        Target *pBuildTarget = nullptr;

        nlohmann::json meta;

        tsl::ordered_map<const Target *, fs::path> artifacts;

        std::string GetObjectDirectory(const std::string &module) const
        {
            return init_vars.at("re_target_object_directory_" + module);
        }

        std::string GetArtifactDirectory(const std::string &module) const
        {
            return init_vars.at("re_target_artifact_directory_" + module);
        }

        bool HasArtifactsFor(const std::string &module) const
        {
            return init_vars.find("re_target_artifact_directory_" + module) != init_vars.end();
        }
    };
} // namespace re
