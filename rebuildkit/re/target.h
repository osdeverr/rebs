#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <filesystem>

#include <yaml-cpp/yaml.h>

#include "lang_provider.h"
#include "lang_locator.h"

namespace re
{
    enum class TargetType
    {
        Project,
        Executable,
        StaticLibrary,
        SharedLibrary,
        Custom
    };

    TargetType TargetTypeFromString(const std::string& type);
    const char* TargetTypeToString(TargetType type);

    using TargetConfig = YAML::Node;

    enum class CfgEntryKind
    {
        Recursive,
        NonRecursive
    };

    class TargetLoadException : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    struct SourceFile
    {
        std::string path;
        std::string extension;
    };

    struct TargetDependency
    {
        std::string raw;

        std::string ns;
        std::string name;
        std::string version;

        Target* resolved = nullptr;
    };

    inline std::string ModulePathCombine(const std::string& a, const std::string& b)
    {
        if (a.empty())
            return b;
        else if (b.empty())
            return a;
        else
            return a + "." + b;
    }

	class Target
	{
    public:
        static constexpr auto kTargetConfigFilename = "re.yml";

        //////////////////////////////////////////////////////////////

        Target() = default;
        Target(std::string_view dir_path, Target* pParent = nullptr);
        Target(std::string_view virtual_path, std::string_view name, TargetType type, const TargetConfig& config, Target* pParent = nullptr);

        Target(const Target&) = default;
        Target(Target&&) = default;

        //////////////////////////////////////////////////////////////

        TargetType type;

        std::string path;
        std::string name;

        std::string module;

        Target* parent = nullptr;

        std::vector<TargetDependency> dependencies;
        std::vector<SourceFile> sources;
        std::vector<std::unique_ptr<Target>> children;

        std::string config_path;
        TargetConfig config;

        /*
        // Contains the entire flattened dependency cache for this target in the order {children}..{dependencies recursively}..{itself}.
        // This is mainly a convenience for language providers so that the whole dependency cache doesn't have to be rebuilt every time.
        std::vector<Target*> flat_deps_cache;
        */

        //////////////////////////////////////////////////////////////

        template<class T>
        std::optional<T> GetCfgEntry(std::string_view key, CfgEntryKind kind = CfgEntryKind::NonRecursive) const
        {
            if (auto node = config[key.data()])
            {
                if constexpr (!std::is_same_v<T, TargetConfig>)
                    return node.template as<T>();
                else
                    return node;
            }
            else if (kind == CfgEntryKind::Recursive && parent)
                return parent->GetCfgEntry<T>(key, kind);
            else
                return std::nullopt;
        }

        template<class T>
        T GetCfgEntryOrThrow(std::string_view key, std::string_view message, CfgEntryKind kind = CfgEntryKind::NonRecursive) const
        {
            if (auto value = GetCfgEntry<T>(key, kind))
                return *value;
            else
                throw TargetLoadException(message.data());
        }

        /*
        template<class T>
        T GetCfgEntryOrDefault(std::string_view key, const T& default, CfgValueKind kind = CfgValueKind::NonRecursive)
        {
            return GetCfgEntry<T>(key, kind).value_or(default);
        }
        */

        void LoadBaseData();
        void LoadDependencies();
        void LoadMiscConfig();

        void LoadSourceTree(std::string path = "");

        static void CreateEmptyTarget(std::string_view path, TargetType type, std::string_view name);
	};

    inline static bool DoesDirContainTarget(std::string_view path)
    {
        std::filesystem::path fspath{ path };

        if (FILE* file = std::fopen((fspath / Target::kTargetConfigFilename).string().c_str(), "r"))
        {
            std::fclose(file);
            return true;
        }
        else
            return false;
    }

    inline static std::string ResolveTargetParentRef(std::string name, Target* parent = nullptr)
    {
        if (!name.empty() && name.front() == '.')
        {
            name.erase(0, 1);

            if (parent)
                return ModulePathCombine(parent->module, name);
            else
                return name;
        }
        else
        {
            return name;
        }
    }
}
