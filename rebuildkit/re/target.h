#pragma once
#include <string>
#include <string_view>
#include <optional>

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

        ILangProvider* provider;
    };

    struct TargetDependency
    {
        std::string ns;
        std::string name;
        std::string version;
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
        std::vector<Target> children;

        std::vector<ILangProvider*> lang_providers;
        ILangLocator* lang_locator = nullptr;

        TargetConfig config;

        //////////////////////////////////////////////////////////////

        template<class T>
        std::optional<T> GetCfgEntry(std::string_view key, CfgEntryKind kind = CfgEntryKind::NonRecursive)
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
        T GetCfgEntryOrThrow(std::string_view key, std::string_view message, CfgEntryKind kind = CfgEntryKind::NonRecursive)
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
	};
}
