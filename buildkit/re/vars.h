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

        virtual std::optional<ulib::string> GetVar(ulib::string_view key) const = 0;
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

    ulib::string VarSubstitute(const VarContext &ctx, ulib::string_view str, ulib::string_view default_namespace = "");

    class LocalVarScope : public IVarNamespace
    {
    public:
        explicit LocalVarScope(VarContext *context, ulib::string_view alias = "",
                               const IVarNamespace *parent = nullptr, ulib::string_view parent_alias = "");

        explicit LocalVarScope(const LocalVarScope *parent, ulib::string_view alias = "")
            : LocalVarScope{parent ? parent->mContext : nullptr, alias, parent}
        {
        }

        explicit LocalVarScope(ulib::string_view alias) : LocalVarScope{{}, alias, nullptr}
        {
        }

        LocalVarScope(const LocalVarScope &other);
        LocalVarScope(LocalVarScope &&other) noexcept;

        ~LocalVarScope();

        void Adopt(VarContext *context, IVarNamespace *parent);

        void AddNamespace(ulib::string_view name, IVarNamespace *ns);

        void SetVar(ulib::string_view key, std::string value);

        void RemoveVar(ulib::string_view key);

        std::optional<ulib::string> GetVar(ulib::string_view key) const;

        std::optional<ulib::string> GetVarNoRecurse(ulib::string_view key) const;

        inline ulib::string Resolve(ulib::string_view str) const
        {
            // fmt::print(" * Resolving '{}': ", str);
            auto result = VarSubstitute(*mContext, str, mLocalName);
            // fmt::print("=> '{}'\n", result);
            return result;
        }

        inline std::string ResolveLocal(ulib::string_view key) const
        {
            if (auto var = GetVar(key))
                return Resolve(*var);
            else
                RE_THROW VarSubstitutionException(
                    "local variable '{}' not found [mLocalName='{}' mAlias='{}' mParentAlias='{}']", key, mLocalName,
                    mAlias, mParentAlias);
        }

        inline LocalVarScope Subscope(ulib::string_view alias = "")
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

        ulib::string mLocalName;
        ulib::string mAlias;
        ulib::string mParentAlias;

        std::unordered_map<std::string, std::string> mVars;

        void Init();
    };
} // namespace re
