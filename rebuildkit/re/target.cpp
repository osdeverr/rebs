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

	Target::Target(std::string_view dir_path, Target* pParent)
	{
        path = std::filesystem::canonical(dir_path).string();
        parent = pParent;

        if (pParent)
        {
            lang_providers = pParent->lang_providers;
            lang_locator = pParent->lang_locator;
        }

        LoadBaseData();
	}

    void Target::LoadBaseData()
    {
        ghc::filesystem::path fspath{ path };

        config_path = (fspath / kTargetConfigFilename).string();
        config = YAML::LoadFile(config_path);

        name = GetCfgEntry<std::string>("name").value_or(fspath.filename().string());

        auto type_str = GetCfgEntryOrThrow<std::string>("type", "target type not specified");
        type = TargetTypeFromString(type_str);

        module = parent ? parent->module : "";
        module = ModulePathCombine(module, name);
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

                //dep.ns = match[0].str();
                dep.name = str;// match[1].str();
                //dep.version = match[2].str();

                if (dep.name.empty())
                    throw TargetLoadException("dependency " + str + " does not have a name specified");

                dependencies.emplace_back(std::move(dep));
            }
        }
    }

    void Target::LoadMiscConfig()
    {
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

                for(auto& lang : lang_providers)
                    if (lang->SupportsFileExtension(ext))
                    {
                        sources.push_back(SourceFile{ entry.path().string(), ext, lang });
                        break;
                    }
            }
        }
    }
}
