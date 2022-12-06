#pragma once
#include <string_view>
#include <fmt/format.h>
#include <fmt/color.h>

namespace re
{
    enum class UserOutputLevel
    {
        Off,
        Problems,
        Error,
        Warn,
        Info,
        Debug,

        // Trace outputs require specific Re builds.
        Trace,

        All
    };

    class IUserOutput
    {
    public:
        virtual ~IUserOutput() = default;

        virtual void DoPrint(UserOutputLevel level, fmt::text_style style, std::string_view text) = 0;

		template<class F, class... Args>
		void Print(UserOutputLevel level, fmt::text_style style, const F& format, Args&&... args)
		{
            DoPrint(level, style, fmt::format(format, std::forward<Args>(args)...));
        }

		template<class F, class... Args>
		void Trace(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Trace, style, format, std::forward<Args>(args)...);
        }

		template<class F, class... Args>
		void Debug(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Debug, style, format, std::forward<Args>(args)...);
        }

		template<class F, class... Args>
		void Info(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Info, style, format, std::forward<Args>(args)...);
        }

		template<class F, class... Args>
		void Warn(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Warn, style, format, std::forward<Args>(args)...);
        }

		template<class F, class... Args>
		void Error(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Error, style, format, std::forward<Args>(args)...);
        }
    };
}
