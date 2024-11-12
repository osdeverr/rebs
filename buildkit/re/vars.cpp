#include "vars.h"

#include <boost/xpressive/xpressive.hpp>

namespace re
{
    namespace
    {
        // \$\{(?:(\w+?):\s*)?([A-Za-z\-\_0-9]*)(?:\s*\|\s*\s*(?:\$(\w+?):\s*)?(.+?)?)?\}

        const auto kOuterVarRegex = boost::xpressive::sregex::compile(R"(\$\{(.*?)\})");
        const auto kVarRegex = boost::xpressive::sregex::compile(R"((?:([^:|]+)?:\s*)?([^|\s]*)(?:\s*\|\s*(.*))?)");

        ulib::string GetVarValue(const VarContext &ctx, ulib::string_view original, ulib::string_view var,
                                ulib::string_view default_namespace)
        {
            ulib::string ns = default_namespace;
            ulib::string key = "";
            ulib::string fallback = "";

            boost::xpressive::smatch match;

            std::string vvv{var};
            if (!boost::xpressive::regex_match(vvv, match, kVarRegex))
                RE_THROW VarSubstitutionException("invalid variable definition\n    in string '{}'", original);

            //////////////////////////////////////

            if (match[1])
                ns = match[1].str();

            if (match[2])
                key = match[2].str();
            else
                RE_THROW VarSubstitutionException("variable name not specified\n    in string '{}'", original);

            if (match[3])
                fallback = match[3].str();

            //////////////////////////////////////

            auto it = ctx.find(ns);

            if (it == ctx.end())
            {
                ulib::string namespaces;

                for (auto &[key, _] : ctx)
                    namespaces += ulib::format("\n    {}", key);

                RE_THROW VarSubstitutionException(
                    "var namespace '{}' not found\n    in string '{}'\n\n    Available namespaces:{}", ns, original,
                    namespaces);
            }

            //////////////////////////////////////

            if (auto var = it->second->GetVar(key))
            {
                return VarSubstitute(ctx, *var, default_namespace);
            }
            else
            {
                if (!fallback.empty())
                {
                    if (fallback.front() == '$')
                        return GetVarValue(ctx, original, fallback.substr(1), default_namespace);
                    else
                        return fallback;
                }
                else
                    RE_THROW VarSubstitutionException("variable '{}:{}' not defined\n    in string '{}'", ns, key,
                                                      original);
            }
        }
    } // namespace

    ulib::string VarSubstitute(const VarContext &ctx, ulib::string_view str, ulib::string_view default_namespace)
    {
        using namespace boost::xpressive;

        return regex_replace(
            std::string{str}, kOuterVarRegex,
            [&str, &ctx, &default_namespace](const smatch &match) {
                return GetVarValue(ctx, str, match[1].str(), default_namespace);
            },
            regex_constants::match_any);
    }

    LocalVarScope::LocalVarScope(VarContext *context, ulib::string_view alias, const IVarNamespace *parent,
                                 ulib::string_view parent_alias)
        : mContext{context}, mAlias{alias}, mParent{parent}, mParentAlias{parent_alias}
    {
        Init();
    }

    LocalVarScope::LocalVarScope(const LocalVarScope &other)
        : LocalVarScope(other.mContext, other.mAlias, other.mParent)
    {
    }

    LocalVarScope::LocalVarScope(LocalVarScope &&other) noexcept
    {
        mContext = std::move(other.mContext);
        mAlias = std::move(other.mAlias);
        mParent = std::move(other.mParent);

        Init();
    }

    LocalVarScope::~LocalVarScope()
    {
        if (mContext)
        {
            mContext->erase(mLocalName);

            if (!mAlias.empty())
                mContext->erase(mAlias);

            if (!mParentAlias.empty())
                mContext->erase(mParentAlias);
        }
    }

    void LocalVarScope::Adopt(VarContext *context, IVarNamespace *parent)
    {
        mContext = context;
        mParent = parent;

        Init();
    }

    void LocalVarScope::AddNamespace(ulib::string_view name, IVarNamespace *ns)
    {
        // fmt::print(" * [{}] Adding var namespace '{}'\n", mLocalName, name);
        (*mContext)[name] = ns;
    }

    void LocalVarScope::SetVar(ulib::string_view key, std::string value)
    {
        mVars[key] = std::move(value);
    }

    void LocalVarScope::RemoveVar(ulib::string_view key)
    {
        mVars.erase(key);
    }

    std::optional<ulib::string> LocalVarScope::GetVar(ulib::string_view key) const
    {
        if (auto value = GetVarNoRecurse(key))
            return value;
        else
            return mParent ? mParent->GetVar(key) : std::nullopt;
    }

    std::optional<ulib::string> LocalVarScope::GetVarNoRecurse(ulib::string_view key) const
    {
        auto it = mVars.find(key);

        if (it != mVars.end())
            return it->second;
        else
            return std::nullopt;
    }

    void LocalVarScope::Init()
    {
        mLocalName = fmt::format("@local-{}", (std::uintptr_t)this);

        if (mContext)
        {
            (*mContext)[mLocalName] = this;

            if (!mAlias.empty())
                (*mContext)[mAlias] = this;

            if (!mParentAlias.empty())
                (*mContext)[mParentAlias] = mParent;
        }

        // if (mParent)
        //	(*mContext)["local-parent"] = mParent;
    }
} // namespace re
