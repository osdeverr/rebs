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
		const E& operator|(const E& e) const
		{
			throw boost::enable_error_info(e)
				<< TracedError(trace);

			return e;
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

#define RE_THROW ReThrowSugarProxy{boost::stacktrace::stacktrace()} | 
