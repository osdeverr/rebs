/**
 * @file re/user_output.h
 * @author Nikita Ivanov
 * @brief User output interfaces
 * @version 0.2.8
 * @date 2023-01-14
 * 
 * @copyright Copyright (c) 2023 Nikita Ivanov
 * 
 */

#pragma once
#include <string_view>
#include <fmt/format.h>
#include <fmt/color.h>

namespace re
{
    /**
     * @brief User Output Level
     */
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

    /**
     * @brief Represents an interface for user output printing.
     */
    class IUserOutput
    {
    public:
        virtual ~IUserOutput() = default;

        /**
         * @brief Requests to print the specified message. The implementation can decide not to.
         * 
         * @param level The message's severity level
         * @param style The message's text style
         * @param text The message text
         */
        virtual void DoPrint(UserOutputLevel level, fmt::text_style style, std::string_view text) = 0;

        /**
         * @brief Formats and requests to print the specified message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param level The message's severity level
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Print(UserOutputLevel level, fmt::text_style style, const F& format, Args&&... args)
		{
            DoPrint(level, style, fmt::format(format, std::forward<Args>(args)...));
        }

        /**
         * @brief Requests to print the specified trace-level message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Trace(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Trace, style, format, std::forward<Args>(args)...);
        }

        /**
         * @brief Requests to print the specified debug message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Debug(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Debug, style, format, std::forward<Args>(args)...);
        }

        /**
         * @brief Requests to print the specified info message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Info(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Info, style, format, std::forward<Args>(args)...);
        }

        /**
         * @brief Requests to print the specified warning message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Warn(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Warn, style, format, std::forward<Args>(args)...);
        }

        /**
         * @brief Requests to print the specified error message. The implementation can decide not to.
         * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
         * 
         * @param style The message's text style
		 * @param format The format string
		 * @param args Format arguments
         */
		template<class F, class... Args>
		void Error(fmt::text_style style, const F& format, Args&&... args)
		{
            Print(UserOutputLevel::Error, style, format, std::forward<Args>(args)...);
        }
    };
}
