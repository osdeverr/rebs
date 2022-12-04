#pragma once
#include <boost/stacktrace.hpp>
#include <boost/exception/all.hpp>

#include <fmt/format.h>

namespace re
{
	struct tag_stacktrace;

	using TracedError = boost::error_info<tag_stacktrace, boost::stacktrace::stacktrace>;

	/*
	template <class E>
	inline void throw_with_trace(const E& e) {
		throw boost::enable_error_info(e)
			<< TracedError(boost::stacktrace::stacktrace());
	}
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

	class Exception : public std::runtime_error
	{
	public:
		template<class F, class... Args>
		Exception(const F& format, Args&&... args)
			: std::runtime_error{fmt::format(format, std::forward<Args>(args)...)}
		{}
	};
}

#define RE_THROW throw ReThrowSugarProxy{boost::stacktrace::stacktrace()} | 
