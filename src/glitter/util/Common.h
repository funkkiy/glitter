#pragma once

#include <type_traits>
#include <limits>
#include <utility>

template <typename Into, typename From> constexpr Into narrow_into(From x)
{
    static_assert(std::is_arithmetic_v<From> && std::is_arithmetic_v<Into>, "narrow_into requires arithmetic types");
    static_assert(std::is_integral_v<From> && std::is_integral_v<Into>, "narrow_into requires integral types");
    static_assert(!std::is_same_v<Into, From>, "narrow_into requires From and Into to be different types");
    static_assert(sizeof(From) >= sizeof(Into), "narrow_into requires From to be bigger than Into");

    if (std::cmp_less(x, std::numeric_limits<Into>::min())) {
        return std::numeric_limits<Into>::min();
    }

    if (std::cmp_greater(x, std::numeric_limits<Into>::max())) {
        return std::numeric_limits<Into>::max();
    }

    return static_cast<Into>(x);
}

#define GLITTER_CONCAT(x, y) GLITTER_CONCAT_IMPL(x, y)
#define GLITTER_CONCAT_IMPL(x, y) x ## y

#ifdef _MSC_VER
#define GLITTER_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define GLITTER_FORCE_INLINE inline __attribute__((always_inline))
#else
#define GLITTER_FORCE_INLINE inline
#endif
