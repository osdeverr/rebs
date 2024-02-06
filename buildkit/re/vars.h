#pragma once
#include <unordered_map>

#include <optional>
#include <string>
#include <string_view>

#include "error.h"

namespace re
{
    struct IVarNamespace
    {
        virtual ~IVarNamespace() = default;

        virtual std::optional<std::string> GetVar(const std::string &key) const = 0;
    };

    using VarContext = std::unordered_map<std::string, const IVarNamespace *>;

    class VarSubstitutionException : public Exception
    {
    public:
        template <class F, class... Args>
        VarSubstitutionException(const F &format, Args &&...args)
            : Exception{"VarSubstitutionException: {}", fmt::format(format, std::forward<Args>(args)...)}
        {
        }
    };

    std::string VarSubstitute(const VarContext &ctx, const std::string &str, const std::string &default_namespace = "");

    class LocalVarScope : public IVarNamespace
    {
    public:
        explicit LocalVarScope(VarContext *context, const std::string &alias = "",
                               const IVarNamespace *parent = nullptr, const std::string &parent_alias = "");

        explicit LocalVarScope(const LocalVarScope *parent, const std::string &alias = "")
            : LocalVarScope{parent ? parent->mContext : nullptr, alias, parent}
        {
        }

        explicit LocalVarScope(const std::string &alias) : LocalVarScope{{}, alias, nullptr}
        {
        }

        LocalVarScope(const LocalVarScope &other);
        LocalVarScope(LocalVarScope &&other) noexcept;

        ~LocalVarScope();

        void Adopt(VarContext *context, IVarNamespace *parent);

        void AddNamespace(const std::string &name, IVarNamespace *ns);

        void SetVar(const std::string &key, std::string value);

        void RemoveVar(const std::string &key);

        std::optional<std::string> GetVar(const std::string &key) const;

        std::optional<std::string> GetVarNoRecurse(const std::string &key) const;

        inline std::string Resolve(const std::string &str) const
        {
            // fmt::print(" * Resolving '{}': ", str);
            auto result = VarSubstitute(*mContext, str, mLocalName);
            // fmt::print("=> '{}'\n", result);
            return result;
        }

        inline std::string ResolveLocal(const std::string &key) const
        {
            if (auto var = GetVar(key))
                return Resolve(*var);
            else
                RE_THROW VarSubstitutionException(
                    "local variable '{}' not found [mLocalName='{}' mAlias='{}' mParentAlias='{}']", key, mLocalName,
                    mAlias, mParentAlias);
        }

        inline LocalVarScope Subscope(const std::string &alias = "")
        {
            return LocalVarScope{mContext, alias, this};
        }

        inline VarContext *GetContext() const
        {
            return mContext;
        }

    private:
        VarContext *mContext;
        const IVarNamespace *mParent = nullptr;

        std::string mLocalName;
        std::string mAlias;
        std::string mParentAlias;

        std::unordered_map<std::string, std::string> mVars;

        void Init();
    };
} // namespace re
