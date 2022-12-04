#include "vars.h"

#include <boost/xpressive/xpressive.hpp>

namespace re
{
	namespace
	{
		// \$\{(?:(\w+?):\s*)?([A-Za-z\-\_0-9]*)(?:\s*\|\s*\s*(?:\$(\w+?):\s*)?(.+?)?)?\}

		const auto kOuterVarRegex = boost::xpressive::sregex::compile(R"(\$\{(.*?)\})");
		const auto kVarRegex = boost::xpressive::sregex::compile(R"((?:(\w+?):\s*)?([A-Za-z\-\_0-9]*)(?:\s*\|\s*(.*))?)");

		std::string GetVarValue(const VarContext& ctx, const std::string& original, const std::string& var, const std::string& default_namespace)
		{
			std::string ns = default_namespace;
			std::string key = "";
			std::string fallback = "";

			boost::xpressive::smatch match;

			if(!boost::xpressive::regex_match(var, match, kVarRegex))
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
				RE_THROW VarSubstitutionException("var namespace '{}' not found\n    in string '{}'", ns, original);

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
					RE_THROW VarSubstitutionException("variable '{}:{}' not defined\n    in string '{}'", ns, key, original);
			}
		}
	}

	std::string VarSubstitute(const VarContext& ctx, const std::string& str, const std::string& default_namespace)
	{
		using namespace boost::xpressive;

		return regex_replace(
			str, kOuterVarRegex,
			[&str, &ctx, &default_namespace](const smatch& match) { return GetVarValue(ctx, str, match[1].str(), default_namespace); },
			regex_constants::match_any
		);
	}

	LocalVarScope::LocalVarScope(VarContext* context, const std::string& alias, const IVarNamespace* parent, const std::string& parent_alias)
		: mContext{ context }, mAlias{ alias }, mParent{ parent }, mParentAlias{ parent_alias }
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

	void LocalVarScope::Adopt(VarContext* context, IVarNamespace* parent)
	{
		mContext = context;
		mParent = parent;

		Init();
	}

	void LocalVarScope::AddNamespace(const std::string& name, IVarNamespace* ns)
	{
		(*mContext)[name] = ns;
	}

	void LocalVarScope::SetVar(const std::string& key, std::string value)
	{
		mVars[key] = std::move(value);
	}

	void LocalVarScope::RemoveVar(const std::string& key)
	{
		mVars.erase(key);
	}

	std::optional<std::string> LocalVarScope::GetVar(const std::string& key) const
	{
		auto it = mVars.find(key);

		// fmt::print("'{}'/'{}': ", mAlias, key);

		if (it != mVars.end())
		{
			// fmt::print("found with val '{}' ", it->second);
			return it->second;
		}
		else
		{
			/*
			if (mParent)
				fmt::print("forward to ");
			else
				fmt::print("all hope lost. ");
			*/

			return mParent ? mParent->GetVar(key) : std::nullopt;
		}
	}

	void LocalVarScope::Init()
	{
		mLocalName = fmt::format("@local-{}", (std::uintptr_t) this);

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
}
