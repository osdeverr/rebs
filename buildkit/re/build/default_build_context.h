#pragma once
#include <re/buildenv.h>
#include <re/target_feature.h>
#include <re/user_output.h>

#include "environment_var_namespace.h"
#include <ulib/yaml.h>

namespace re
{
    class DefaultBuildContext : public IUserOutput
    {
    public:
        DefaultBuildContext();
        DefaultBuildContext(const DefaultBuildContext &) = delete;

        void LoadDefaultEnvironment(const fs::path &data_path, const fs::path &dynamic_data_path);

        inline LocalVarScope &GetVars()
        {
            return mVars;
        }

        inline void SetVar(ulib::string_view key, std::string value)
        {
            mVars.SetVar(key, value);
        }
        inline std::optional<ulib::string> GetVar(ulib::string_view key)
        {
            return mVars.GetVar(key);
        }
        inline void RemoveVar(ulib::string_view key)
        {
            mVars.RemoveVar(key);
        }

        Target &LoadTarget(const fs::path &path);

        ulib::yaml LoadCachedParams(const fs::path &path);
        void SaveCachedParams(const fs::path &path, const ulib::yaml &node);

        void ResolveAllTargetDependencies(Target *pRootTarget);

        NinjaBuildDesc GenerateBuildDescForTarget(Target &root_target, Target *build_target = nullptr);
        NinjaBuildDesc GenerateBuildDescForTargetInDir(const fs::path &path);

        void SaveTargetMeta(const NinjaBuildDesc &desc);

        int BuildTarget(const NinjaBuildDesc &desc);
        void InstallTarget(const NinjaBuildDesc &desc);

        int BuildTargetInDir(const fs::path &path)
        {
            auto desc = GenerateBuildDescForTargetInDir(path);
            return BuildTarget(desc);
        }

        BuildEnv *GetBuildEnv() const
        {
            return mEnv.get();
        }

        virtual void DoPrint(UserOutputLevel level, fmt::text_style style, std::string_view text) override;

        void UpdateOutputSettings();

        void ApplyTemplateInDirectory(const fs::path &dir, std::string_view template_name);

        void CreateTargetFromTemplate(const fs::path &out_path, std::string_view template_name,
                                      std::string_view target_name);

    private:
        VarContext mVarContext;
        LocalVarScope mVars;
        EnvironmentVarNamespace mSystemEnvVars;

        fs::path mDataPath;

        std::unique_ptr<BuildEnv> mEnv;

        std::vector<std::unique_ptr<ILangProvider>> mLangs;
        std::vector<std::unique_ptr<IDepResolver>> mDepResolvers;
        std::vector<std::unique_ptr<ITargetFeature>> mTargetFeatures;
        std::vector<std::unique_ptr<ITargetLoadMiddleware>> mTargetLoadMiddlewares;

        UserOutputLevel mOutLevel = UserOutputLevel::Info;
        bool mOutColors = true;

        std::unique_ptr<DepsVersionCache> mDepsVersionCache;

        int RunNinjaBuild(const fs::path &script, const Target *root);

        void CopyTemplateToDirectory(const fs::path &dir, const fs::path &template_dir);
    };
} // namespace re
