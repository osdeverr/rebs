/**
 * @file re/target.h
 * @author Nikita Ivanov
 * @brief Build target infrastructure
 * @version 0.2.8
 * @date 2023-01-14
 * 
 * @copyright Copyright (c) 2023 Nikita Ivanov
 */

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

#include <semverpp/version.hpp>

namespace re
{
    /**
     * @brief A type of a Target object.
     */
    enum class TargetType
    {
        /**
         * @brief Targets of this type do not have any sources and only contain other subtargets.
         */
        Project,

        /**
         * @brief Targets of this type get built normally and can be run using the `re run` command.
         */
        Executable,

        /**
         * @brief Targets of this type can be linked to other executable and library targets via dependencies.
        */
        StaticLibrary,
        
        /**
         * @brief There currently is no way to link to shared library targets - however, they can be built and used manually.
        */
        SharedLibrary,

        /**
         * @brief Custom targets are currently work-in-progress.
         */
        Custom
    };

    /**
     * @brief Constructs a TargetType from its string representation.
     * 
     * @param type The string to convert.
     * @return The resulting target type.
     * 
     * @throws Exception Thrown if the string does not represent a valid target type.
     */
    TargetType TargetTypeFromString(const std::string& type);

    /**
     * @brief Converts a TargetType to a string.
     * 
     * @param type The TargetType to convert.
     * @return A const char* representing the target type that can be fed back to TargetTypeFromString.
     */
    const char* TargetTypeToString(TargetType type);

    using TargetConfig = YAML::Node;

    /**
     * @brief An option determining whether to search a target config option recursively in the target's parents.
     */
    enum class CfgEntryKind
    {
        /**
         * @brief Search the configurations recursively.
         */
        Recursive,

        /**
         * @brief Only search the current target's configuration.
         */
        NonRecursive
    };

    /**
     * @brief A single source file loaded in a Target.
     */
    struct SourceFile
    {
        /**
         * @brief The source file's absolute path.
         */
        fs::path path;

        /**
         * @brief The source file's extension (for convenience)
         */
        std::string extension;
    };

    /**
     * @brief The kind of a dependency's version specification.
     */
    enum class DependencyVersionKind
    {
        /**
         * @brief Do not perform any version comparisons and try resolving the raw specified tag.
         */
        RawTag,

        /**
         * @brief (SemVer) Look for a version exactly matching the specified one.
         */
        Equal,
        
        /**
         * @brief (SemVer) Look for a version greater than the specified one.
         */
        Greater,
        
        /**
         * @brief (SemVer) Look for a version greater than or equal to the specified one.
         */
        GreaterEqual,
        
        /**
         * @brief (SemVer) Look for a version less than the specified one.
         */
        Less,
        
        /**
         * @brief (SemVer) Look for a version less than or equal to the specified one.
         */
        LessEqual,

        /**
         * @brief (SemVer) Look for the newest version with the same major and minor values but any patch value.
         * 
         * Behavior is analogous to npm's '~1.0.0' specifier.
         */
        SameMinor,
        
        /**
         * @brief (SemVer) Look for the newest version with the same major value but any patch and minor values.
         * 
         * Behavior is analogous to npm's '^1.0.0' specifier.
         */
        SameMajor,
    };

    /**
     * @brief A single dependency of a Target.
     * 
     * Dependencies are passed to IDepResolver implementations to do with as they please.
     */
    struct TargetDependency
    {
        /**
         * @brief The dependency's original string representation.
         */
        std::string raw;

        /**
         * @brief The dependency namespace to search in.
         */
        std::string ns;

        /**
         * @brief The dependency's name (can include just a name, a path, a link or anything)
         */
        std::string name;

        /**
         * @brief The dependency's version. This should ideally be a valid SemVer value.
         */
        std::string version;

        /**
         * @brief The dependency's version as a SemVer value.
         */
        semverpp::version version_sv;

        /**
         * @brief The dependency's version kind.
         */
        DependencyVersionKind version_kind;

        /**
         * @brief A list of "filters" for the dependency.
         * 
         * These are in most cases subtargets to depend on, so as to not build parts of the dependency you don't need.
         */
        std::vector<std::string> filters;

        /**
         * @brief This dependency's resulting resolved target list.
         * 
         * This is filled at dependency resolution time. Dependencies whose TargetDependency::resolved
         * isn't empty are considered resolved and ignored in subsequent resolution passes.
         */
        std::vector<Target*> resolved = {};

        /**
         * @brief The extra config (eCfg) for this dependency.
         * 
         * Extra configs allow dependents to manually override dependencies' target config fields.
         * This creates a _new_ target for every unique eCfg node there is under the hood.
         */
        YAML::Node extra_config{YAML::NodeType::Undefined};

        /**
         * @brief A unique value identifying this dependency's extra config node owner.
         */
        std::size_t extra_config_hash = 0;

        /**
         * @brief A unique value identifying this dependency's extra config node data.
         */
        std::size_t extra_config_data_hash = 0;

        /**
         * @brief Converts this dependency into a string.
         * 
         * @return std::string The resulting string.
         */
        std::string ToString() const;
    };

    /**
     * @brief Parses a string into a TargetDependency object.
     *
     * The string must be in the format `[ns:]<name>[@version] [\[filters...\]]`.
     * 
     * @param str The string to parse
     * @param pTarget The target whose dependencies are being parsed
     * 
     * @return TargetDependency The resulting dependency
     * 
     * @throws TargetDependencyException Thrown if the string has an invalid format
     */
    TargetDependency ParseTargetDependency(const std::string& str, const Target* pTarget = nullptr);
    
    /**
     * @brief Parses a YAML node into a TargetDependency object.
     *
     * The node's string value or first key must be in the format `[ns:]<name>[@version] [\[filters...\]]`.
     * If the node is an object, its first key's value will be used as the eCfg node for this dependency.
     * 
     * @param node The YAML node to parse
     * @param pTarget The target whose dependencies are being parsed
     * 
     * @return TargetDependency The resulting dependency
     * 
     * @throws TargetDependencyException Thrown if the node's depstring has an invalid format
     */
    TargetDependency ParseTargetDependencyNode(YAML::Node node, const Target* pTarget = nullptr);

    /**
     * @brief Combines two target module paths with respect to their possible emptiness.
     * 
     * If a is empty, returns b.
     * If b is empty, returns a.
     * If both are not empty, returns "{a}.{b}".
     * 
     * @param a The first module path (can be empty)
     * @param b The second module path (can be empty)
     * 
     * @return std::string A module path that combines the two passed to this function.
     */
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

    /**
     * @brief An exception pertaining to a Target object.
     */
    class TargetException : public Exception
    {
    public:
        /**
         * @brief Construct a new TargetException object.
         * 
         * @param type The exception's category type
         * @param target The Target at fault for this exception
         * @param str The error message
         */
        TargetException(std::string_view type, const Target* target, const std::string& str);

        /**
         * @brief Construct a new TargetException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param type The exception's category type
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetException(std::string_view type, const Target* target, const F& format, Args&&... args)
            : TargetException{ type, target, fmt::format(format, std::forward<Args>(args)...) }
        {}
    };

    /**
     * @brief An exception that occurred while loading a Target.
     */
    class TargetLoadException : public TargetException
    {
    public:
        /**
         * @brief Construct a new TargetLoadException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetLoadException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetLoadException", target, format, std::forward<Args>(args)... }
        {}
    };

    /**
     * @brief An exception related to a Target's configuration.
     */
    class TargetConfigException : public TargetException
    {
    public:
        /**
         * @brief Construct a new TargetConfigException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetConfigException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetConfigException", target, format, std::forward<Args>(args)... }
        {}
    };

    /**
     * @brief An exception that occurred while resolving a Target's dependencies.
     */
    class TargetDependencyException : public TargetException
    {
    public:
        /**
         * @brief Construct a new TargetDependencyException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetDependencyException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetDependencyException", target, format, std::forward<Args>(args)... }
        {}
    };

    /**
     * @brief An exception that occurred while building a Target.
     */
    class TargetBuildException : public TargetException
    {
    public:
        /**
         * @brief Construct a new TargetBuildException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetBuildException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetBuildException", target, format, std::forward<Args>(args)... }
        {}
    };

    /**
     * @brief An exception that occurred because a Target had uncached dependencies that were not allowed.
     */
    class TargetUncachedDependencyException : public TargetException
    {
    public:
        /**
         * @brief Construct a new TargetUncachedDependencyException object and format its error message.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param target The Target at fault for this exception
         * @param format The format string
         * @param args Format arguments
         */
        template<class F, class... Args>
        TargetUncachedDependencyException(const Target* target, const F& format, Args&&... args)
            : TargetException{ "TargetUncachedDependencyException", target, format, std::forward<Args>(args)... }
        {}
    };

    struct ITargetFeature;

    /**
     * @brief A single buildable target for the Re build system.
     * 
     * Contains various configuration fields and variable scopes.
     */
    class Target : public IVarNamespace
	{
    public:
        /**
         * @brief The default target config filename.
         */
        static constexpr auto kTargetConfigFilename = "re.yml";

        //////////////////////////////////////////////////////////////

        /**
         * @brief Construct a new Target object with default values.
         */
        Target() = default;

        /**
         * @brief Construct a new Target object from a real FS path and an optional parent.
         * 
         * This is not to be used outside of BuildEnv as it ignores all middleware.
         * 
         * @param dir_path The directory to load the target config from.
         * @param pParent The target's parent (null if no parent)
         */
        Target(const fs::path& dir_path, Target* pParent = nullptr);

        /**
         * @brief Construct a new Target object from a virtual FS path (which may or may not contain a real target)
         * and the target's core properties
         * 
         * This allows you to construct a target without a re.yml file, like in vcpkg or CMake dependencies.
         * 
         * @param virtual_path The purported "path" of the target - this may or may not include a config file for it, as it's not loaded at all
         * @param name The target's name
         * @param type The target's type
         * @param config The target's initial configuration
         * @param pParent The target's parent (null if no parent)
         */
        Target(const fs::path& virtual_path, std::string_view name, TargetType type, const TargetConfig& config, Target* pParent = nullptr);

        Target(const Target&) = default;
        Target(Target&&) = default;

        //////////////////////////////////////////////////////////////

        /**
         * @brief The target's type.
         */
        TargetType type;

        /**
         * @brief The target's source path.
         * 
         * This is not guaranteed to contain a target config file. Please use the Target::config field
         * or the Target::resolved_config field to access target configuration values.
         */
        fs::path path;

        /**
         * @brief The target's ultimate root target's path
         */
        fs::path root_path;

        /**
         * @brief The target's original name.
         */
        std::string name;

        /**
         * @brief The target's final module name.
         */
        std::string module;

        /**
         * @brief The target's parent (or nullptr).
         */
        Target* parent = nullptr;

        /**
         * @brief The target's ultimate root target.
         */
        Target* root = nullptr;

        /**
         * @brief A list of the target's dependencies.
         */
        std::vector<TargetDependency> dependencies;

        /**
         * @brief A list of the target's source files.
         */
        std::vector<SourceFile> sources;

        /**
         * @brief A list of the target's child targets.
         */
        std::vector<std::unique_ptr<Target>> children;

        /**
         * @brief A path to the target's config file.
         */
        fs::path config_path;

        /**
         * @brief The target's YAML configuration.
         */
        TargetConfig config;

        /**
         * @brief The target's "flat" config representation.
         * 
         * Resolved configs are only built once and are "flattened" with respect to their conditional sections and inheritance.
         */
        TargetConfig resolved_config{ YAML::NodeType::Undefined };

        /**
         * @brief A set of targets that depend on this target.
         */
        std::unordered_set<const Target*> dependents;

        /**
         * @brief This target's parent variable scope.
         */
        LocalVarScope* var_parent = nullptr;

        /**
         * @brief This target's local variable context.
         */
        mutable VarContext local_var_ctx;

        /**
         * @brief This target's local variable scope for the 'target:' namespace.
         */
        std::optional<LocalVarScope> target_var_scope;

        /**
         * @brief This target's local variable scope for the 'build:' namespace.
         */
        std::optional<LocalVarScope> build_var_scope;

        /**
         * @brief This target's `uses` dependency map.
         */
        std::unordered_map<std::string, std::unique_ptr<TargetDependency>> used_mapping;
        
        /**
         * @brief This target's parent with respect to dependencies.
         */
        const Target* dep_parent = nullptr;

        /**
         * @brief A map of this target's used target features.
         */
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

        /**
         * @brief Gets a configuration entry with respect to its kind.
         * 
         * @tparam T The config entry's type
         * 
         * @param key The key of this entry in the target config
         * @param kind The entry kind to use
         * 
         * @return std::optional<T> The configuration entry's value or std::nullopt
         */
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

        /**
         * @brief Gets a configuration entry with respect to its kind.
         * 
         * @tparam T The config entry's type
         * 
         * @param key The key of this entry in the target config
         * @param message The error message to use if the entry is not found
         * @param kind The entry kind to use
         * 
         * @return T The configuration entry's value
         * 
         * @throws TargetConfigException Thrown if the config entry does not exist.
         */
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

        /**
         * @brief Loads the target's base data like its type and name.
         */
        void LoadBaseData();
        
        /**
         * @brief Loads the target's dependency list.
         * 
         * @param key For internal use.
         */
        void LoadDependencies(std::string_view key = "");

        /**
         * @brief Loads the target's conditional dependencies.
         */
        void LoadConditionalDependencies();

        /**
         * @brief Loads the target's miscellaneous config.
         * 
         * Currently does nothing.
         */
        void LoadMiscConfig();

        /**
         * @brief Recursively loads the target's sources from the specified directory.
         * 
         * The recursive search stops looking further if a directory contains a file named `.re-ignore-this`.
         * 
         * @param path The path to search.
         */
        void LoadSourceTree(fs::path path = "");

        /**
         * @brief Creates an empty target configuration file at the specified path.
         * 
         * @param path The path to create the config at
         * @param type The new target's type
         * @param name The new target's name
         */
        static void CreateEmptyTarget(const fs::path& path, TargetType type, std::string_view name);
	};

    /**
     * @brief Checks whether a directory contains a Re target config file.
     * 
     * @param path The path to check
     * 
     * @return true If the directory contains a `re.yml` file.
     * @return false If the directory does not contain a `re.yml` file.
     */
    inline static bool DoesDirContainTarget(const fs::path& path)
    {
        return fs::exists(path / Target::kTargetConfigFilename);
    }

    /**
     * @brief Resolves parent-referencing module paths like `.libhello` with a period at the beginning.
     * 
     * @param name The parent-referencing module path
     * @param target The target to resolve the path against
     * 
     * @return std::string The fully resolved absolute module path
     */
    std::string ResolveTargetParentRef(std::string name, const Target* target);

    /**
     * @brief Escapes the passed target's module path to not include problematic characters.
     * 
     * @param target The target whose module path to escape
     * 
     * @return std::string The fully escaped module path
     */
    std::string GetEscapedModulePath(const Target& target);
}
