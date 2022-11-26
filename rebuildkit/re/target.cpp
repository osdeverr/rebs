#include "target.h"

#include <ghc/filesystem.hpp>
#include <magic_enum.hpp>

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
    }

    void Target::LoadDependencies()
    {
        if (auto deps = GetCfgEntry<TargetConfig>("deps"))
        {
            for (const auto &dep : *deps)
            {
                auto str = dep.as<std::string>();

                std::regex dep_regex{"(.+?:)?([^@]+)(@.+)?"};
                std::smatch match;

                if (!std::regex_match(str, match, dep_regex))
                    RE_THROW TargetDependencyException(this, "dependency {} does not meet the format requirements", str);

                TargetDependency dep;

                dep.raw = str;
                dep.ns = match[1].str();
                dep.name = match[2].str();
                dep.version = match[3].str();

                // Remove the trailing ':' character
                if (!dep.ns.empty())
                    dep.ns.pop_back();

                // Remove the leading '@' character
                if (!dep.version.empty())
                    dep.version = dep.version.substr(1);

                if (dep.name.empty())
                    RE_THROW TargetDependencyException(this, "dependency {} does not have a name specified", str);

                dep.name = ResolveTargetParentRef(dep.name, parent);
                dependencies.emplace_back(std::move(dep));
            }
        }
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

        std::filesystem::create_directories(path);

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

    std::string TargetDependency::ToString() const
    {
        std::string result;

        if (!ns.empty())
            result += ns + ":";

        result += name;

        if (!version.empty())
            result += "@" + version;

        return result;
    }

    TargetException::TargetException(std::string_view type, const Target *target, const std::string &str)
        : Exception{"{} in target '{}':\n      {}", type, target ? target->module : "null", str}
    {
    }
}
