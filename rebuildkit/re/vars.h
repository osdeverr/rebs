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

		virtual std::optional<std::string> GetVar(const std::string& key) = 0;
	};

	using VarContext = std::unordered_map<std::string, IVarNamespace*>;

	class VarSubstitutionException : public Exception
	{
	public:
		template<class F, class... Args>
		VarSubstitutionException(const F& format, Args&&... args)
			: Exception{ "VarSubstitutionException: {}", fmt::format(format, std::forward<Args>(args)...) }
		{}
	};

	std::string VarSubstitute(const VarContext& ctx, const std::string& str);

	class LocalVarScope : public IVarNamespace
	{
	public:
		explicit LocalVarScope(VarContext context = {}, const std::string& alias = "", IVarNamespace* parent = nullptr);

		explicit LocalVarScope(const std::string& alias)
			: LocalVarScope{ {}, alias, nullptr }
		{}

		LocalVarScope(const LocalVarScope& other);
		LocalVarScope(LocalVarScope&& other) noexcept;

		void AddNamespace(const std::string& name, IVarNamespace* ns);

		void SetVar(const std::string& key, std::string value);

		void RemoveVar(const std::string& key);

		std::optional<std::string> GetVar(const std::string& key);

		inline std::string Substitute(const std::string& str) const
		{
			return VarSubstitute(mContext, str);
		}

		inline LocalVarScope Subscope()
		{
			return LocalVarScope{ mContext, mAlias, this };
		}

	private:
		VarContext mContext;
		IVarNamespace* mParent = nullptr;

		std::string mAlias;

		std::unordered_map<std::string, std::string> mVars;

		void Init();
	};
}
