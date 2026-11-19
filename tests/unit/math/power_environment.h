// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/simdmath.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/floating_point_control.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::MathCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr b8 ShouldSkipNeon32SqrtCase()noexcept{
#if defined(FOUNDATION_HAS_NEON) && !defined(FOUNDATION_HAS_NEON64)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] inline constexpr b8 HasDoubleFmaResidual()noexcept{
#if defined(FOUNDATION_HAS_FMA3) || defined(FOUNDATION_HAS_NEON)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] inline constexpr b8 HasSingleFmaResidual()noexcept{
#if defined(FOUNDATION_HAS_FMA3) || defined(FOUNDATION_HAS_NEON64)
    return true;
#else
    return false;
#endif
}

[[nodiscard]] FOUNDATION_INLINE b8 ConfigurePowerDenormalMode([[maybe_unused]] const u32 denormalMode, [[maybe_unused]] const b8 unmaskInvalid)noexcept{
#if defined(FOUNDATION_PLATFORM_WINDOWS) && defined(FOUNDATION_HAS_NEON64) && !defined(_M_ARM64EC)
    u32 control = 0u;
    if(_controlfp_s(&control, denormalMode, _MCW_DN) != 0)
        return false;
    if((control & _MCW_DN) != denormalMode)
        return false;
    if(unmaskInvalid && _controlfp_s(&control, control & ~_EM_INVALID, _MCW_EM) != 0)
        return false;
#endif
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// These expressions use the host C++ compiler's contraction and operator rules.
template<typename T>
FOUNDATION_NOINLINE T ReferenceMultiplyAdd(const T first, const T second, const T addend)noexcept{
    return first * second + addend;
}

template<typename T>
FOUNDATION_NOINLINE T ReferenceSeparateProduct(const T first, const T second, const T addend)noexcept{
    const T product = first * second;
    return product + addend;
}

template<typename T>
FOUNDATION_NOINLINE T ReferenceNegativeProduct(const T first, const T second, const T addend)noexcept{
    return addend - first * second;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_WINDOWS) && defined(FOUNDATION_HAS_NEON64) && !defined(_M_ARM64EC)
#define NWB_GTEST_POWER_STATUS_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GTEST_POWER_STATUS_CASE(Suite, Case, Fn)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

