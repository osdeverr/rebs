#include "target.h"

#include <ghc/filesystem.hpp>
#include <magic_enum.hpp>

#include <regex>

namespace re
{
    inline static constexpr auto kCaseInsensitiveComparePred = [](char lhs, char rhs) { return std::tolower(lhs) == std::tolower(rhs); };

    TargetType TargetTypeFromString(const std::string& type)
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
            throw TargetLoadException("unknown target type " + type);
    }

    const char* TargetTypeToString(TargetType type)
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

	Target::Target(std::string_view dir_path, Target* pParent)
	{
        path = std::filesystem::canonical(dir_path).string();
        parent = pParent;

        ghc::filesystem::path fspath{ path };

        config_path = (fspath / kTargetConfigFilename).string();
        config = YAML::LoadFile(config_path);

        name = GetCfgEntry<std::string>("name").value_or(fspath.filename().string());

        /*
        if (pParent)
        {
            lang_providers = pParent->lang_providers;
            lang_locator = pParent->lang_locator;
        }
        */

        LoadBaseData();
	}

    Target::Target(std::string_view virtual_path, std::string_view name, TargetType type, const TargetConfig& config, Target* pParent)
    {
        path = std::filesystem::canonical(virtual_path).string();
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

            if(parent)
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
            for (const auto& dep : *deps)
            {
                auto str = dep.as<std::string>();

                std::regex dep_regex{ "(.+?:)?([^@]+)(@.+)?" };
                std::smatch match;

                if (!std::regex_match(str, match, dep_regex))
                    throw TargetLoadException("dependency " + str + " does not meet the format requirements");

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
                    throw TargetLoadException("dependency " + str + " does not have a name specified");

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

    void Target::LoadSourceTree(std::string path)
    {
        if (path.empty())
            path = this->path;

        ghc::filesystem::path fspath{ path };
        std::string dirname = fspath.filename().string();

        // rpnint" -- DEBUG: Traversing '%s' srcmodpath='%s'\n", fspath.string().c_str(), src_module_path.c_str());

        for (auto& entry : ghc::filesystem::directory_iterator{ path })
        {
            auto filename = entry.path().filename().string();
            if (filename.starts_with("."))
                continue;

            if (entry.is_directory())
            {
                if (DoesDirContainTarget(entry.path().string()))
                {
                    auto target = std::make_unique<Target>(entry.path().string(), this);

                    if (target->GetCfgEntry<bool>("enabled").value_or(true))
                    {
                        target->LoadDependencies();
                        target->LoadMiscConfig();
                        target->LoadSourceTree();

                        children.emplace_back(std::move(target));
                    }
                }
                else
                    LoadSourceTree(entry.path().string());
            }

            if (entry.is_regular_file())
            {
                auto ext = entry.path().extension().string();

                if (ext.size() > 0)
                    ext = ext.substr(1);

                sources.push_back(SourceFile{ entry.path().string(), ext });
            }
        }
    }

    void Target::CreateEmptyTarget(std::string_view path, TargetType type, std::string_view name)
    {
        YAML::Emitter out;

        out << YAML::BeginMap;
        out << YAML::Key << "type";
        out << YAML::Value << TargetTypeToString(type);
        out << YAML::Key << "name";
        out << YAML::Value << name.data();
        out << YAML::EndMap;

        std::filesystem::create_directories(path);

        std::ofstream file{ path.data() + std::string("/re.yml") };
        file << out.c_str();
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
}
