#include "target.h"

#include <ghc/filesystem.hpp>
#include <magic_enum.hpp>

#include <boost/algorithm/string.hpp>
#include <regex>

namespace re
{
    inline static constexpr auto kCaseInsensitiveComparePred = [](char lhs, char rhs)
    { return std::tolower(lhs) == std::tolower(rhs); };

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

    Target::Target(const fs::path& dir_path, Target *pParent)
    {
        path = fs::canonical(dir_path);
        parent = pParent;

        config_path = path / kTargetConfigFilename;

        std::ifstream f{ config_path };
        config = YAML::Load(f);

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

    Target::Target(const fs::path& virtual_path, std::string_view name, TargetType type, const TargetConfig &config, Target *pParent)
    {
        path = fs::canonical(virtual_path);
        parent = pParent;

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

        auto dep_from_str = [this](const std::string& str)
        {
            std::regex dep_regex{ R"(\s*(?:(.+?:)?)\s*(?:([^@\s]+))\s*(?:(@[^\s]*)?)(?:\s*)?(?:(?:\[)(.+)(?:\]))?)" };
            std::smatch match;

            if (!std::regex_match(str, match, dep_regex))
                RE_THROW TargetDependencyException(this, "dependency {} does not meet the format requirements", str);

            TargetDependency dep;

            dep.raw = str;
            dep.ns = match[1].str();
            dep.name = match[2].str();
            dep.version = match[3].str();

            if (match[4].matched)
            {
                auto raw = match[4].str();

                boost::algorithm::erase_all(raw, " ");
                boost::split(dep.filters, raw, boost::is_any_of(","));
            }

            // Remove the trailing ':' character
            if (!dep.ns.empty())
                dep.ns.pop_back();

            // Remove the leading '@' character
            if (!dep.version.empty())
                dep.version = dep.version.substr(1);

            if (dep.name.empty())
                RE_THROW TargetDependencyException(this, "dependency {} does not have a name specified", str);

            dep.name = ResolveTargetParentRef(dep.name, this);

            return dep;
        };

        if (deps)
        {
            for (const auto &dep : deps)
            {
                auto str = dep.as<std::string>();

                bool exists = false;

                for (auto& existing : dependencies)
                    if (existing.raw == str)
                        exists = true;

                if (exists)
                    continue;

                dependencies.emplace_back(dep_from_str(str));
            }
        }

        if (resolved_config && build_var_scope)
        {
            if (auto uses = resolved_config["uses"])
            {
                for (const auto& kv : uses)
                {
                    auto key = kv.first.Scalar();

                    // fmt::print("{}\n", key);

                    auto& mapping = used_mapping[key];

                    if (!mapping)
                        mapping = std::make_unique<TargetDependency>(dep_from_str(build_var_scope->Resolve(kv.second.Scalar())));
                }
            }
        }
    }

    void Target::LoadConditionalDependencies()
    {
        LoadDependencies("deps");
        LoadDependencies("cond-deps");
    }

    void Target::LoadMiscConfig()
    {
        /*
        if (auto langs = GetCfgEntry<TargetConfig>("langs"))
        {
            if (!lang_locator)
                throw TargetLoadException("language locator not specified - 'langs' config entries may not be processed correctly");

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

    void Target::CreateEmptyTarget(const fs::path& path, TargetType type, std::string_view name)
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

    std::optional<std::string> Target::GetVar(const std::string& key) const
    {
        // fmt::print("target '{}'/'{}': ", module, key);

        if (config["vars"] && config["vars"][key].IsDefined())
        {
            // fmt::print("found in vars");
            return config["vars"][key].as<std::string>();
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

    std::pair<const LocalVarScope&, VarContext&> Target::GetBuildVarScope() const
    {
        if (build_var_scope)
            return std::make_pair(std::ref(*build_var_scope), std::ref(local_var_ctx));
        else if (parent)
            return parent->GetBuildVarScope();
        else
            RE_THROW TargetConfigException(this, "reached top of hierarchy without finding a valid build var scope");
    }

    TargetDependency* Target::GetUsedDependency(const std::string& name) const
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

    Target* Target::FindChild(std::string_view name) const
    {
        for (auto& child : children)
            if (child->name == name)
                return child.get();

        return nullptr;
    }

    std::string TargetDependency::ToString() const
    {
        return raw;
    }

    TargetException::TargetException(std::string_view type, const Target *target, const std::string &str)
        : Exception{"{} in target '{}':\n      {}", type, target ? target->module : "null", str}
    {
    }

    std::string ResolveTargetParentRef(std::string name, Target* target)
    {
        std::string prefix = "";
        Target* parent = nullptr;

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
}