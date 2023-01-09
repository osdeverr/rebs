#pragma once
#include <re/lang_provider.h>
#include <re/build_desc.h>

namespace re
{
	class CMakeLangProvider : public ILangProvider
	{
	public:
		static constexpr auto kLangId = "cmake";
		
		CMakeLangProvider(LocalVarScope* var_scope);
		CMakeLangProvider(const CMakeLangProvider&) = default;
		CMakeLangProvider(CMakeLangProvider&&) = default;

		virtual const char* GetLangId() { return kLangId; }

		virtual void InitInBuildDesc(NinjaBuildDesc& desc);

		virtual void InitLinkTargetEnv(NinjaBuildDesc& desc, Target& target);
		virtual bool InitBuildTargetRules(NinjaBuildDesc& desc, const Target& target);

		virtual void ProcessSourceFile(NinjaBuildDesc& desc, const Target& target, const SourceFile& file);
		virtual void CreateTargetArtifact(NinjaBuildDesc& desc, const Target& target);

	private:		
		LocalVarScope* mVarScope;
    };
}
