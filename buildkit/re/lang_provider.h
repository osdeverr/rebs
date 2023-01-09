#pragma once
#include <string_view>

namespace re
{
	class Target;
	struct SourceFile;

	struct NinjaBuildDesc;

	class ILangProvider
	{
	public:
		virtual ~ILangProvider() = default;

		virtual const char* GetLangId() = 0;

		virtual void InitInBuildDesc(NinjaBuildDesc& desc) = 0;

		virtual void InitLinkTargetEnv(NinjaBuildDesc& desc, Target& target) = 0;
		virtual bool InitBuildTargetRules(NinjaBuildDesc& desc, const Target& target) = 0;

		virtual void ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file) = 0;
		virtual void CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target) = 0;
	};
}
