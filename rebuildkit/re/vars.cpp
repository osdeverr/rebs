#include "vars.h"

#include <boost/xpressive/xpressive.hpp>

namespace re
{
	namespace
	{
		std::string GetVarValueImpl(const VarContext& ctx, const std::string& original, const std::string& ns, const std::string& key, const std::string& fallback_ns, const std::string& fallback)
		{
			auto it = ctx.find(ns);

			if (it == ctx.end())
				RE_THROW VarSubstitutionException("var namespace '{}' not found\n    in string '{}'", ns, original);

			//////////////////////////////////////

			if (auto var = it->second->GetVar(key))
			{
				return VarSubstitute(ctx, *var);
			}
			else
			{
				if (!fallback.empty())
				{
					if (!fallback_ns.empty())
						return GetVarValueImpl(ctx, original, fallback_ns, fallback, "", "");
					else
						return fallback;
				}
				else
					RE_THROW VarSubstitutionException("variable '{}:{}' not defined\n    in string '{}'", ns, key, original);
			}
		}

		std::string GetVarValue(const VarContext& ctx, const std::string& original, const boost::xpressive::smatch& match)
		{
			std::string ns = "local";
			std::string key = "";
			std::string fallback_var_ns = "";
			std::string fallback = "";

			//////////////////////////////////////

			if (match[1])
				ns = match[1].str();

			if (match[2])
				key = match[2].str();
			else
				RE_THROW VarSubstitutionException("variable name not specified\n    in string '{}'", original);

			if (match[3])
				fallback_var_ns = match[3].str();

			if (match[4])
				fallback = match[4].str();

			//////////////////////////////////////

			return GetVarValueImpl(ctx, original, ns, key, fallback_var_ns, fallback);
		}
	}

	std::string VarSubstitute(const VarContext& ctx, const std::string& str)
	{
		using namespace boost::xpressive;

		sregex envar = sregex::compile(R"(\$\{(?:(\w+?):\s*)?([A-Za-z\-\_0-9]*)(?:\s*\|\s*\s*(?:\$(\w+?):\s*)?(.+?)?)?\})");
		return regex_replace(str, envar, [str, ctx](const smatch& match) { return GetVarValue(ctx, str, match); }, regex_constants::match_any);
	}

	LocalVarScope::LocalVarScope(VarContext context, const std::string& alias, IVarNamespace* parent)
		: mContext{ std::move(context) }, mAlias{ alias }, mParent{ parent }
	{
		Init();
	}

	LocalVarScope::LocalVarScope(const LocalVarScope& other)
		: LocalVarScope(other.mContext, other.mAlias, other.mParent)
	{
	}

	LocalVarScope::LocalVarScope(LocalVarScope&& other) noexcept
	{
		mContext = std::move(other.mContext);
		mAlias = std::move(other.mAlias);
		mParent = std::move(other.mParent);

		Init();
	}

	void LocalVarScope::AddNamespace(const std::string& name, IVarNamespace* ns)
	{
		mContext[name] = ns;
	}

	void LocalVarScope::SetVar(const std::string& key, std::string value)
	{
		mVars[key] = std::move(value);
	}

	void LocalVarScope::RemoveVar(const std::string& key)
	{
		mVars.erase(key);
	}

	std::optional<std::string> LocalVarScope::GetVar(const std::string& key)
	{
		auto it = mVars.find(key);

		if (it != mVars.end())
			return it->second;
		else
			return mParent ? mParent->GetVar(key) : std::nullopt;
	}

	void LocalVarScope::Init()
	{
		mContext["local"] = this;

		if (mParent)
			mContext["local-parent"] = mParent;

		if (!mAlias.empty())
			mContext[mAlias] = this;
	}
}
