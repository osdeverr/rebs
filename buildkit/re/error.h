/**
 * @file re/error.h
 * @author Nikita Ivanov
 * @brief Re error facilities such as stacktraced errors and more
 * @version 0.2.8
 * @date 2023-01-14
 *
 * @copyright Copyright (c) 2023 Nikita Ivanov
 */

#pragma once
#include <boost/exception/all.hpp>
#include <boost/stacktrace.hpp>

#include <fmt/format.h>

#include <stack>
#include <yaml-cpp/yaml.h>

#include <re/source_location.h>

namespace re
{
    using ExceptionCallStack = std::vector<SourceLocation>;

    inline thread_local ExceptionCallStack gExceptionCallStack;

    struct CallStackPin
    {
        CallStackPin(SourceLocation loc)
        {
            gExceptionCallStack.emplace_back(loc);
        }

        ~CallStackPin()
        {
            gExceptionCallStack.pop_back();
        }
    };

    /**
     * @brief A tag class to distinguing Re stacktraced errors from those possibly defined elsewhere.
     */
    struct tag_stacktrace;

    /**
     * @brief Error info containing a stacktrace.
     */
    using TracedError = boost::error_info<tag_stacktrace, ExceptionCallStack>;

    /**
     * @brief A helper class allowing the usage of the RE_THROW drop-in macro.
     */
    struct ReThrowSugarProxy
    {
        template <class E>
        auto operator|(const E &e) const
        {
            return boost::enable_error_info(e) << TracedError(gExceptionCallStack);
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
        template <class F, class... Args>
        Exception(const F &format, Args &&...args)
            : std::runtime_error{fmt::format(format, std::forward<Args>(args)...)}
        {
        }
    };

} // namespace re

/**
 * @brief Throw an exception with a stacktrace leading to the caller.
 *
 * Intended to use as a drop-in replacement of the `throw` operator.
 *
 * @example RE_THROW re::Exception("Something failed with error code {}", 42);
 */
#define RE_THROW ::re::gExceptionCallStack.push_back(RE_IMPL_CURR_SOURCE_LOCATION()), throw ::re::ReThrowSugarProxy{} |

#define RE_ERROR_BEGIN_BLOCK()                                                                                         \
    ::re::CallStackPin _call_stack_pin_##__LINE__{RE_IMPL_CURR_SOURCE_LOCATION()};                                     \
    try                                                                                                                \
    {

// #define RE_ERROR_IMPL_CATCH_YAML_ERROR catch(YAML::)

#define RE_ERROR_IMPL_CATCH_GENERIC()                                                                                  \
    catch (const std::exception &ex)                                                                                   \
    {                                                                                                                  \
        const ::re::ExceptionCallStack *st = boost::get_error_info<::re::TracedError>(e);                              \
        if (st)                                                                                                        \
            throw;                                                                                                     \
        throw ::re::ReThrowSugarProxy{} | ex;                                                                          \
    }

#define RE_ERROR_CATCH_COMMON()                                                                                        \
    }                                                                                                                  \
    RE_ERROR_IMPL_CATCH_GENERIC()
