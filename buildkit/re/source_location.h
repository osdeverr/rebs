#pragma once
#include <cstdint>

namespace re
{
    class SourceLocation
    {
    public:
        constexpr SourceLocation(std::uint_least32_t line, std::uint_least32_t column, const char *file,
                                 const char *function)
            : mLine{line}, mColumn{column}, mFile{file}, mFunction{function}
        {
        }

        constexpr std::uint_least32_t line() const noexcept
        {
            return mLine;
        }

        constexpr std::uint_least32_t column() const noexcept
        {
            return mColumn;
        }

        constexpr const char *file_name() const noexcept
        {
            return mFile;
        }

        constexpr const char *function_name() const noexcept
        {
            return mFunction;
        }

    private:
        std::uint_least32_t mLine, mColumn;
        const char *mFile;
        const char *mFunction;
    };
} // namespace re

#ifdef _MSC_VER
#define RE_IMPL_CURR_FUNCTION_SIGNATURE __FUNCSIG__
#else
#define RE_IMPL_CURR_FUNCTION_SIGNATURE __PRETTY_FUNCTION__
#endif

#define RE_IMPL_CURR_SOURCE_LOCATION()                                                                                 \
    ::re::SourceLocation                                                                                               \
    {                                                                                                                  \
        __LINE__, 0, __FILE__, RE_IMPL_CURR_FUNCTION_SIGNATURE                                                         \
    }
