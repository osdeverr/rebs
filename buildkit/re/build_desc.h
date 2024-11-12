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
        ulib::string name;
        ulib::string path;
    };

    struct BuildRule
    {
        ulib::string name;
        ulib::string tool;
        ulib::string cmdline;
        ulib::string description;

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

        ulib::string rule;
        ulib::string in;
        ulib::string out;

        BuildVars vars;

        std::vector<ulib::string> deps;

        const Target *pSourceTarget = nullptr;
        const SourceFile *pSourceFile = nullptr;
    };

    struct NinjaBuildDesc
    {
        fs::path out_dir;

        ulib::string object_out_format;
        ulib::string artifact_out_format;

        // Vars that go in the very beginning of the build file
        BuildVars init_vars;

        // Substituted variables: those WILL end up in the build script
        BuildVars vars;

        // Arbitrary generation state: this won't go anywhere
        BuildVars state;

        std::vector<BuildTool> tools;
        std::vector<BuildRule> rules;
        std::vector<BuildTarget> targets;

        std::vector<ulib::string> subninjas;

        Target *pRootTarget = nullptr;
        Target *pBuildTarget = nullptr;

        nlohmann::json meta;

        tsl::ordered_map<const Target *, fs::path> artifacts;

        ulib::string GetObjectDirectory(ulib::string_view module) const
        {
            return init_vars.at("re_target_object_directory_" + module);
        }

        ulib::string GetArtifactDirectory(ulib::string_view module) const
        {
            return init_vars.at("re_target_artifact_directory_" + module);
        }

        bool HasArtifactsFor(ulib::string_view module) const
        {
            return init_vars.find("re_target_artifact_directory_" + module) != init_vars.end();
        }
    };
} // namespace re
