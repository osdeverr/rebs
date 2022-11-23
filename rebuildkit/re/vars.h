#pragma once
#include <unordered_map>

#include <string>
#include <string_view>
#include <optional>

#include "error.h"

namespace re
{
	struct IVarNamespace
	{
		virtual ~IVarNamespace() = default;

		virtual std::optional<std::string> GetVar(const std::string& key) const = 0;
	};

	using VarContext = std::unordered_map<std::string, const IVarNamespace*>;

	class VarSubstitutionException : public Exception
	{
	public:
		template<class F, class... Args>
		VarSubstitutionException(const F& format, Args&&... args)
			: Exception{ "VarSubstitutionException: {}", fmt::format(format, std::forward<Args>(args)...) }
		{}
	};

	std::string VarSubstitute(const VarContext& ctx, const std::string& str, const std::string& default_namespace = "");

	class LocalVarScope : public IVarNamespace
	{
	public:
		explicit LocalVarScope(VarContext* context, const std::string& alias = "", const IVarNamespace* parent = nullptr, const std::string& parent_alias = "");

		explicit LocalVarScope(const LocalVarScope* parent, const std::string& alias = "")
			: LocalVarScope{ parent ? parent->mContext : nullptr, alias, parent }
		{}

		explicit LocalVarScope(const std::string& alias)
			: LocalVarScope{ {}, alias, nullptr }
		{}

		LocalVarScope(const LocalVarScope& other);
		LocalVarScope(LocalVarScope&& other) noexcept;

		~LocalVarScope();

		void Adopt(VarContext* context, IVarNamespace* parent);

		void AddNamespace(const std::string& name, IVarNamespace* ns);

		void SetVar(const std::string& key, std::string value);

		void RemoveVar(const std::string& key);

		std::optional<std::string> GetVar(const std::string& key) const;

		inline std::string Resolve(const std::string& str) const
		{
			return VarSubstitute(*mContext, str, mLocalName);
		}

		inline LocalVarScope Subscope(const std::string& alias = "")
		{
			return LocalVarScope{ mContext, alias, this };
		}

	private:
		VarContext* mContext;
		const IVarNamespace* mParent = nullptr;

		std::string mLocalName;
		std::string mAlias;
		std::string mParentAlias;

		std::unordered_map<std::string, std::string> mVars;

		void Init();
	};
}
