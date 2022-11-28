#pragma once
#include <re/buildenv.h>
#include "environment_var_namespace.h"

namespace re
{
	class DefaultBuildContext
	{
	public:
		DefaultBuildContext();
		DefaultBuildContext(const DefaultBuildContext&) = delete;

		void LoadDefaultEnvironment(const fs::path& re_path);

		inline LocalVarScope& GetVars() { return mVars; }

		inline void SetVar(const std::string& key, std::string value) { mVars.SetVar(key, value); }
		inline std::optional<std::string> GetVar(const std::string& key) { return mVars.GetVar(key); }
		inline void RemoveVar(const std::string& key) { mVars.RemoveVar(key); }

		Target& LoadTarget(const fs::path& path);

		NinjaBuildDesc GenerateBuildDescForTarget(Target& target);
		NinjaBuildDesc GenerateBuildDescForTargetInDir(const fs::path& path);

		int BuildTarget(const NinjaBuildDesc& desc);
		void InstallTarget(const NinjaBuildDesc& desc);

		int BuildTargetInDir(const fs::path& path)
		{
			auto desc = GenerateBuildDescForTargetInDir(path);
			return BuildTarget(desc);
		}

		BuildEnv* GetBuildEnv() const { return mEnv.get(); }

	private:
		VarContext mVarContext;
		LocalVarScope mVars;
		EnvironmentVarNamespace mSystemEnvVars;

		fs::path mRePath;

		std::unique_ptr<BuildEnv> mEnv;

		std::vector<std::unique_ptr<ILangProvider>> mLangs;
		std::vector<std::unique_ptr<IDepResolver>> mDepResolvers;
	};
}
