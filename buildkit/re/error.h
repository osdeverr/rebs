/**
 * @file re/error.h
 * @author osdever
 * @brief Re error facilities such as stacktraced errors and more
 * @version 0.2.8
 * @date 2023-01-14
 * 
 * @copyright Copyright (c) osdever 2023
 */

#pragma once
#include <boost/stacktrace.hpp>
#include <boost/exception/all.hpp>

#include <fmt/format.h>

namespace re
{
	/**
	 * @brief A tag class to distinguing Re stacktraced errors from those possibly defined elsewhere.
	 */
	struct tag_stacktrace;

	/**
	 * @brief Error info containing a stacktrace.
	 */
	using TracedError = boost::error_info<tag_stacktrace, boost::stacktrace::stacktrace>;
	
	/**
	 * @brief A helper class allowing the usage of the RE_THROW drop-in macro.
	 */
	struct ReThrowSugarProxy
	{
		boost::stacktrace::stacktrace trace;

		template<class E>
		auto operator|(const E& e) const
		{
			return boost::enable_error_info(e)
				<< TracedError(trace);
		}
	};

	/**
	 * @brief An exception with formatted error message support.
	 */
	class Exception : public std::runtime_error
	{
	public:
		/**
		 * @brief Construct a new Exception object with a formatted error message.
		 * 
         * @tparam F The format string's type (auto-deduced)
         * @tparam Args Format argument types (auto-deduced)
		 * 
		 * @param format The format string
		 * @param args Format arguments
		 */
		template<class F, class... Args>
		Exception(const F& format, Args&&... args)
			: std::runtime_error{fmt::format(format, std::forward<Args>(args)...)}
		{}
	};
}

/**
 * @brief Throw an exception with a stacktrace leading to the caller.
 * 
 * Intended to use as a drop-in replacement of the `throw` operator.
 * 
 * @example RE_THROW re::Exception("Something failed with error code {}", 42);
 */
#define RE_THROW throw ::re::ReThrowSugarProxy{boost::stacktrace::stacktrace()} | 
