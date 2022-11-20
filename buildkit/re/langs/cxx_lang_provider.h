#pragma once
#include <re/lang_provider.h>

#include <yaml-cpp/yaml.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace re
{
	using CxxBuildEnvData = YAML::Node;

	class CxxLangProvider : public ILangProvider
	{
	public:
		static constexpr auto kLangId = "cpp";

		CxxLangProvider(std::string_view env_search_path);
		CxxLangProvider(const CxxLangProvider&) = default;
		CxxLangProvider(CxxLangProvider&&) = default;

		virtual const char* GetLangId() { return kLangId; }

		virtual void InitInBuildDesc(NinjaBuildDesc& desc);
		virtual bool InitBuildTarget(NinjaBuildDesc& desc, const Target& target);
		virtual void ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file);
		virtual void CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target);

	private:
		std::string mEnvSearchPath;
		std::unordered_map<std::string, CxxBuildEnvData> mEnvCache;

		CxxBuildEnvData& LoadEnvOrThrow(std::string_view name, const Target& invokee);
	};
}
