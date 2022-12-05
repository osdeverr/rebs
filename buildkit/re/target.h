#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <unordered_set>

#include <re/error.h>
#include <re/fs.h>
#include <re/vars.h>

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

    struct SourceFile
    {
        fs::path path;
        std::string extension;
    };

    struct TargetDependency
    {
        std::string raw;

        std::string ns;
        std::string name;
        std::string version;

        std::vector<std::string> filters;

        std::vector<Target*> resolved = {};

        std::string ToString() const;
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

    class Target;

    class TargetException : public Exception
    {
    public:
        TargetException(std::string_view type, const Target* target, const std::string& str);

        template<class F, class... Args>
        TargetException(std::string_view type, const Target* target, const F& format, Args&&... args)
            : TargetException{ type, target, fmt::format(format, std::forward<Args>(args)...) }
        {}
    };

    class TargetLoadException : public TargetException
    {
    public:
        template<class F, class... Args>
        TargetLoadException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetLoadException", target, format, std::forward<Args>(args)... }
        {}
    };

    class TargetConfigException : public TargetException
    {
    public:
        template<class F, class... Args>
        TargetConfigException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetConfigException", target, format, std::forward<Args>(args)... }
        {}
    };

    class TargetDependencyException : public TargetException
    {
    public:
        template<class F, class... Args>
        TargetDependencyException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetDependencyException", target, format, std::forward<Args>(args)... }
        {}
    };

    class TargetBuildException : public TargetException
    {
    public:
        template<class F, class... Args>
        TargetBuildException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetBuildException", target, format, std::forward<Args>(args)... }
        {}
    };

    class TargetUncachedDependencyException : public TargetException
    {
    public:
        template<class F, class... Args>
        TargetUncachedDependencyException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetUncachedDependencyException", target, format, std::forward<Args>(args)... }
        {}
    };

    struct ITargetFeature;

    class Target : public IVarNamespace
	{
    public:
        static constexpr auto kTargetConfigFilename = "re.yml";

        //////////////////////////////////////////////////////////////

        Target() = default;
        Target(const fs::path& dir_path, Target* pParent = nullptr);
        Target(const fs::path& virtual_path, std::string_view name, TargetType type, const TargetConfig& config, Target* pParent = nullptr);

        Target(const Target&) = default;
        Target(Target&&) = default;

        //////////////////////////////////////////////////////////////

        TargetType type;

        fs::path path;
        fs::path root_path;

        std::string name;

        std::string module;

        Target* parent = nullptr;

        std::vector<TargetDependency> dependencies;
        std::vector<SourceFile> sources;
        std::vector<std::unique_ptr<Target>> children;

        fs::path config_path;
        TargetConfig config;

        // This is the "flat" config representation. It is only built once.
        TargetConfig resolved_config{ YAML::NodeType::Undefined };

        // Targets that depend on this target.
        std::unordered_set<const Target*> dependents;

        LocalVarScope* var_parent = nullptr;

        mutable VarContext local_var_ctx;

        std::optional<LocalVarScope> target_var_scope;
        std::optional<LocalVarScope> build_var_scope;

        std::unordered_map<std::string, std::unique_ptr<TargetDependency>> used_mapping;
        const Target* dep_parent = nullptr;

        std::unordered_map<std::string, ITargetFeature*> features;

        std::optional<std::string> GetVar(const std::string& key) const;

        std::pair<const LocalVarScope&, VarContext&> GetBuildVarScope() const;

        TargetDependency* GetUsedDependency(const std::string& name) const;

        Target* FindChild(std::string_view name) const;
        
        template<class T>
        bool HasFeature() const
        {
            return features.find(T::kFeatureName) != features.end();
        }

        template<class T>
        T* FindFeature() const
        {
            auto it = features.find(T::kFeatureName);

            if (it != features.end())
                return (T *)it->second;
            else
                return nullptr;
        }

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
                RE_THROW TargetConfigException(this, message.data());
        }

        /*
        template<class T>
        T GetCfgEntryOrDefault(std::string_view key, const T& default, CfgValueKind kind = CfgValueKind::NonRecursive)
        {
            return GetCfgEntry<T>(key, kind).value_or(default);
        }
        */

        void LoadBaseData();
        void LoadDependencies(std::string_view key = "");
        void LoadConditionalDependencies();
        void LoadMiscConfig();

        void LoadSourceTree(fs::path path = "");

        static void CreateEmptyTarget(const fs::path& path, TargetType type, std::string_view name);
	};

    inline static bool DoesDirContainTarget(const fs::path& path)
    {
        return fs::exists(path / Target::kTargetConfigFilename);
    }

    std::string ResolveTargetParentRef(std::string name, Target* target);

    inline static std::string GetEscapedModulePath(const Target& target)
    {
        auto module_escaped = target.module;
        std::replace(module_escaped.begin(), module_escaped.end(), '.', '_');
        return module_escaped;
    }
}
