#pragma once
#include <re/buildenv.h>
#include <re/target_feature.h>
#include <re/user_output.h>

#include <re/deps/global_dep_resolver.h>

#include "environment_var_namespace.h"

namespace re
{
	class DefaultBuildContext : public IUserOutput
	{
	public:
		DefaultBuildContext();
		DefaultBuildContext(const DefaultBuildContext&) = delete;

		void LoadDefaultEnvironment(const fs::path& data_path, const fs::path& dynamic_data_path);

		inline LocalVarScope& GetVars() { return mVars; }

		inline void SetVar(const std::string& key, std::string value) { mVars.SetVar(key, value); }
		inline std::optional<std::string> GetVar(const std::string& key) { return mVars.GetVar(key); }
		inline void RemoveVar(const std::string& key) { mVars.RemoveVar(key); }

		Target& LoadTarget(const fs::path& path);

		YAML::Node LoadCachedParams(const fs::path& path);
		void SaveCachedParams(const fs::path& path, const YAML::Node& node);

		NinjaBuildDesc GenerateBuildDescForTarget(Target& target);
		NinjaBuildDesc GenerateBuildDescForTargetInDir(const fs::path& path);

		void SaveTargetMeta(const NinjaBuildDesc& desc);

		int BuildTarget(const NinjaBuildDesc& desc);
		void InstallTarget(const NinjaBuildDesc& desc);

		int BuildTargetInDir(const fs::path& path)
		{
			auto desc = GenerateBuildDescForTargetInDir(path);
			return BuildTarget(desc);
		}

		BuildEnv* GetBuildEnv() const { return mEnv.get(); }
		GlobalDepResolver* GetGlobalDepResolver() const { return mGlobalDepResolver.get(); }

        virtual void DoPrint(UserOutputLevel level, fmt::text_style style, std::string_view text) override;

		void UpdateOutputSettings();

	private:
		VarContext mVarContext;
		LocalVarScope mVars;
		EnvironmentVarNamespace mSystemEnvVars;

		fs::path mDataPath;

		std::unique_ptr<BuildEnv> mEnv;

		std::vector<std::unique_ptr<ILangProvider>> mLangs;
		std::vector<std::unique_ptr<IDepResolver>> mDepResolvers;
		std::vector<std::unique_ptr<ITargetFeature>> mTargetFeatures;

		UserOutputLevel mOutLevel = UserOutputLevel::Info;
		bool mOutColors = true;

		std::unique_ptr<GlobalDepResolver> mGlobalDepResolver;
	};
}
