#include "target.h"

#include <magic_enum.hpp>

#include <fstream>
#include <re/fs.h>

#include <regex>

#include <re/debug.h>
#include <re/yaml_merge.h>

#include <ulib/string.h>

namespace re
{
    inline static constexpr auto kCaseInsensitiveComparePred = [](char lhs, char rhs) {
        return std::tolower(lhs) == std::tolower(rhs);
    };

    TargetType TargetTypeFromString(const std::string &type)
    {
        if (type == "project")
            return TargetType::Project;
        else if (type == "executable")
            return TargetType::Executable;
        else if (type == "static-library")
            return TargetType::StaticLibrary;
        else if (type == "shared-library")
            return TargetType::SharedLibrary;
        else if (type == "custom")
            return TargetType::Custom;
        else
            RE_THROW Exception("unknown target type {}", type);
    }

    const char *TargetTypeToString(TargetType type)
    {
        switch (type)
        {
        case TargetType::Project:
            return "project";
        case TargetType::Executable:
            return "executable";
        case TargetType::StaticLibrary:
            return "static-library";
        case TargetType::SharedLibrary:
            return "shared-library";
        case TargetType::Custom:
            return "custom";
        default:
            return "invalid";
        }
    }

    Target::Target(const fs::path &dir_path, Target *pParent)
    {
        path = fs::canonical(dir_path);
        parent = pParent;

        root = parent ? parent->root : this;

        RE_TRACE(" ***** LOADING TARGET: path = {}\n", path.generic_u8string());

        config_path = path / kTargetConfigFilename;

        std::ifstream f{config_path};
        config = YAML::Load(f);

        // Load all config partitions

        for (auto &entry : fs::directory_iterator{path})
        {
            ulib::string name = entry.path().filename().u8string();

            if (name.ends_with(".re.yml"))
            {
                std::ifstream merge_f{entry.path()};
                auto merge_c = YAML::Load(merge_f);

                MergeYamlNode(config, merge_c);
            }
        }

        name = GetCfgEntry<std::string>("name").value_or(path.filename().u8string());

        /*
        if (pParent)
        {
            lang_providers = pParent->lang_providers;
            lang_locator = pParent->lang_locator;
        }
        */

        LoadBaseData();
    }

    Target::Target(const fs::path &virtual_path, std::string_view name, TargetType type, const TargetConfig &config,
                   Target *pParent)
    {
        path = fs::canonical(virtual_path);

        parent = pParent;
        root = parent ? parent->root : this;

        this->type = type;
        this->config = config;
        this->name = name;
        this->module = name;
    }

    void Target::LoadBaseData()
    {
        auto type_str = GetCfgEntryOrThrow<std::string>("type", "target type not specified");
        type = TargetTypeFromString(type_str);

        if (!name.empty() && name.front() == '.')
        {
            name.erase(0, 1);

            if (parent)
                module = ModulePathCombine(parent->module, name);
        }
        else
        {
            module = name;
        }

        if (parent)
            root_path = parent->root_path;
    }

    void Target::LoadDependencies(std::string_view key)
    {
        auto used_key = key.empty() ? "deps" : key;

        // Use the relevant config instance.
        auto deps = resolved_config ? resolved_config[used_key.data()] : config[used_key.data()];

        if (deps)
        {
            for (auto node : deps)
            {
                bool exists = false;

                auto dep = ParseTargetDependencyNode(node, this);

                for (auto &existing : dependencies)
                    if (existing.raw == dep.raw && existing.extra_config_hash == dep.extra_config_hash)
                        exists = true;

                if (exists)
                    continue;

                dependencies.emplace_back(dep);
            }
        }

        if (resolved_config && build_var_scope)
        {
            if (auto uses = resolved_config["uses"])
            {
                for (const auto &kv : uses)
                {
                    auto key = kv.first.Scalar();

                    // fmt::print("{}\n", key);

                    auto &mapping = used_mapping[key];

                    // TODO: Move to ParseTargetDependencyNode
                    if (!mapping)
                        mapping = std::make_unique<TargetDependency>(
                            ParseTargetDependency(build_var_scope->Resolve(kv.second.Scalar()), this));
                }
            }
        }
    }

    void Target::LoadConditionalDependencies()
    {
        RE_TRACE("Target::LoadConditionalDependencies - {}: My root is {}!\n", module, root->module);

        LoadDependencies("deps");
        LoadDependencies("cond-deps");

        // features.clear();

        for (auto v : resolved_config["features"])
            if (!features[v.Scalar()])
                features[v.Scalar()] = nullptr;
    }

    void Target::LoadMiscConfig()
    {
        /*
        if (auto langs = GetCfgEntry<TargetConfig>("langs"))
        {
            if (!lang_locator)
                throw TargetLoadException("language locator not specified - 'langs' config entries may not be processed
        correctly");

            for (const auto& lang : *langs)
            {
                auto id = lang.as<std::string>();
                auto found = false;

                for (auto& provider : lang_providers)
                    if (provider->GetLangId() == id)
                        found = true;

                if (!found)
                {
                    if (auto provider = lang_locator->GetLangProvider(id))
                        lang_providers.push_back(provider);
                    else
                        throw TargetLoadException("language provider " + lang.as<std::string>() + " not found");
                }
            }
        }
        */
    }

    void Target::LoadSourceTree(fs::path path)
    {
        if (GetCfgEntry<bool>("disable-source-tree-load").value_or(false))
            return;

        if (path.empty())
            path = this->path;

        // fmt::print(" [DBG] Traversing '{}'\n", path.u8string());

        // rpnint" -- DEBUG: Traversing '%s' srcmodpath='%s'\n", fspath.string().c_str(), src_module_path.c_str());

        for (auto &entry : fs::directory_iterator{path})
        {
            auto filename = entry.path().filename().u8string();
            if (filename.front() == '.')
                continue;

            if (entry.is_directory())
            {
                if (fs::exists(entry.path() / ".re-ignore-this"))
                    continue;

                if (DoesDirContainTarget(entry.path()))
                {
                    auto target = std::make_unique<Target>(entry.path(), this);

                    if (target->GetCfgEntry<bool>("enabled").value_or(true))
                    {
                        target->LoadDependencies();
                        target->LoadMiscConfig();
                        target->LoadSourceTree();

                        children.emplace_back(std::move(target));
                    }
                }
                else
                    LoadSourceTree(entry.path());
            }

            if (entry.is_regular_file())
            {
                auto ext = entry.path().extension().u8string();

                if (ext.size() > 0)
                    ext = ext.substr(1);

                sources.push_back(SourceFile{entry.path(), ext});
            }
        }
    }

    void Target::CreateEmptyTarget(const fs::path &path, TargetType type, std::string_view name)
    {
        YAML::Emitter out;

        out << YAML::BeginMap;
        out << YAML::Key << "type";
        out << YAML::Value << TargetTypeToString(type);
        out << YAML::Key << "name";
        out << YAML::Value << name.data();
        out << YAML::EndMap;

        fs::create_directories(path);

        std::ofstream file{path / kTargetConfigFilename};
        file << out.c_str();
    }

    std::optional<std::string> Target::GetVar(const std::string &key) const
    {
        // fmt::print("target '{}'/'{}': ", module, key);

        if (key == "path")
            return path.u8string();
        else if (key == "module")
            return module;

        auto used_config = resolved_config ? resolved_config : config;

        if (used_config["vars"] && used_config["vars"][key].IsDefined())
        {
            // fmt::print("found in vars");
            return used_config["vars"][key].as<std::string>();
        }
        else if (used_config[key])
        {
            // fmt::print("found in used vars");
            return used_config[key].as<std::string>();
        }
        else if (auto entry = GetCfgEntry<std::string>(key, CfgEntryKind::Recursive))
        {
            // fmt::print("found in config with entry='{}'", entry.value());
            return entry;
        }
        else if (auto var = parent ? parent->GetVar(key) : std::nullopt)
        {
            // fmt::print("forward to parent ");
            return var;
        }
        else if (var_parent)
        {
            // fmt::print("forward to scope ");
            return var_parent->GetVar(key);
        }
        else
        {
            // fmt::print("target lost. ");
            return std::nullopt;
        }
    }

    std::pair<const LocalVarScope &, VarContext &> Target::GetBuildVarScope() const
    {
        if (build_var_scope)
            return std::make_pair(std::ref(*build_var_scope), std::ref(local_var_ctx));
        else if (parent)
            return parent->GetBuildVarScope();
        else
            RE_THROW TargetConfigException(this, "reached top of hierarchy without finding a valid build var scope");
    }

    TargetDependency *Target::GetUsedDependency(const std::string &name) const
    {
        auto it = used_mapping.find(name);

        if (it != used_mapping.end())
            return it->second.get();

        if (auto dep = parent ? parent->GetUsedDependency(name) : nullptr)
            return dep;

        if (auto dep = dep_parent ? dep_parent->GetUsedDependency(name) : nullptr)
            return dep;

        return nullptr;
    }

    Target *Target::FindChild(std::string_view name) const
    {
        for (auto &child : children)
            if (child->name == name)
                return child.get();

        return nullptr;
    }

    const std::regex kTargetDepRegex{
        R"(\s?(?:([a-zA-Z0-9.-]*)(?::))?\s?([^\s@=<>~\^]*)\s*(?:(@|==|<|<=|>|>=|~|\^)\s*([a-zA-Z0-9._-]*))?\s*(?:(?:\[)(.+)(?:\]))?)"};

    TargetDependency ParseTargetDependency(const std::string &str, const Target *pTarget)
    {
        std::smatch match;

        if (!std::regex_match(str, match, kTargetDepRegex))
            RE_THROW TargetDependencyException(pTarget, "dependency '{}' does not meet the format requirements", str);

        TargetDependency dep;

        dep.raw = str;
        dep.ns = match[1].str();
        dep.name = match[2].str();

        auto &kind_str = (dep.version_kind_str = match[3].str());

        if (kind_str == "@" || kind_str == "")
            dep.version_kind = DependencyVersionKind::RawTag;
        else if (kind_str == "==")
            dep.version_kind = DependencyVersionKind::Equal;
        else if (kind_str == ">")
            dep.version_kind = DependencyVersionKind::Greater;
        else if (kind_str == ">=")
            dep.version_kind = DependencyVersionKind::GreaterEqual;
        else if (kind_str == "<")
            dep.version_kind = DependencyVersionKind::Less;
        else if (kind_str == "<=")
            dep.version_kind = DependencyVersionKind::LessEqual;
        else if (kind_str == "~")
            dep.version_kind = DependencyVersionKind::SameMinor;
        else if (kind_str == "^")
            dep.version_kind = DependencyVersionKind::SameMajor;
        else
            RE_THROW TargetDependencyException(pTarget, "invalid kind tag '{}' in dependency '{}'", kind_str, str);

        dep.version = match[4].str();

        if (dep.version_kind != DependencyVersionKind::RawTag)
            dep.version_sv = semverpp::version{dep.version};

        if (match[5].matched)
        {
            ulib::string raw = match[5].str();
            raw = raw.replace(" ", "");

            dep.filters.clear();
            auto spl = raw.split(",");
            for (auto filter : spl)
                dep.filters.push_back(filter);
        }

        if (dep.name.empty())
            RE_THROW TargetDependencyException(pTarget, "dependency {} does not have a name specified", str);

        if (pTarget)
            dep.name = ResolveTargetParentRef(dep.name, pTarget);

        return dep;
    }

    TargetDependency ParseTargetDependencyNode(YAML::Node node, const Target *pTarget)
    {
        if (node.IsScalar())
        {
            return ParseTargetDependency(node.Scalar(), pTarget);
        }
        else if (node.IsMap())
        {
            // HACK: This YAML library sucks.
            for (auto kv : node)
            {
                auto result = ParseTargetDependency(kv.first.Scalar(), pTarget);

                result.extra_config = YAML::Clone(kv.second);

                YAML::Emitter emitter;
                emitter << result.extra_config;
                result.extra_config_data_hash = std::hash<std::string_view>{}(emitter.c_str());

                if (pTarget)
                {
                    result.extra_config_hash = std::hash<std::string_view>{}(pTarget->module);
                }
                else
                {
                    result.extra_config_hash = result.extra_config_data_hash;
                }

                return result;
            }
        }
        else
        {
            auto mark = node.Mark();
            RE_THROW TargetDependencyException(pTarget, "dependency node at {}:{} must be string or map", mark.line,
                                               mark.column);
            return {}; // Unreachable but the compiler complains
        }
    }

    std::string TargetDependency::ToString() const
    {
        return raw;
    }

    TargetException::TargetException(std::string_view type, const Target *target, const std::string &str)
        : Exception{"{} in target '{}':\n      {}", type, target ? target->module : "null", str}
    {
    }

    std::string ResolveTargetParentRef(std::string name, const Target *target)
    {
        std::string prefix = "";
        const Target *parent = nullptr;

        while (!name.empty() && name.front() == '.')
        {
            name.erase(0, 1);

            if (!parent)
                parent = target;

            if (parent && parent->parent)
                parent = parent->parent;
        }

        if (parent && !parent->module.empty())
            prefix = parent->module + ".";

        return prefix + name;
    }

    std::string GetEscapedModulePath(const Target &target)
    {
        auto module_escaped = target.module;

        std::replace(module_escaped.begin(), module_escaped.end(), '.', '_');
        std::replace(module_escaped.begin(), module_escaped.end(), ':', '_');
        std::replace(module_escaped.begin(), module_escaped.end(), '@', '_');

        return module_escaped;
    }
} // namespace re
