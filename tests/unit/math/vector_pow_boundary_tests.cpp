// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/not_null.h>
#include <global/simdmath.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>

#include <cerrno>

#if defined(NWB_PLATFORM_WINDOWS)
#include <float.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vector_pow_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PowerWords = u32 __attribute__((ext_vector_type(4)));
using PowerEvaluate = SIMDVectorSingle(*)(SIMDVectorSingle, SIMDVectorSingle)noexcept;

struct alignas(SIMDVectorSingle) PowerObservation{
    SIMDVectorSingle value;
    i32 flags;
    i32 error;
    u32 nativeStatus;
    u64 rawNativeStatus;
};
static_assert(alignof(Expected<PowerObservation>) >= alignof(PowerObservation));

NWB_NOINLINE SIMDVectorSingle FrozenPower(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorSet(Pow(VectorGetX(x), VectorGetX(y)), Pow(VectorGetY(x), VectorGetY(y)), Pow(VectorGetZ(x), VectorGetZ(y)), Pow(VectorGetW(x), VectorGetW(y)));
}

NWB_NOINLINE SIMDVectorSingle CurrentPower(const SIMDVectorSingle x, const SIMDVectorSingle y)noexcept{
    return VectorPow(x, y);
}

[[nodiscard]] Expected<PowerObservation> ObservePower(const NotNull<PowerEvaluate> evaluate, const SIMDVectorSingle x, const SIMDVectorSingle y, const i32 initialFlags, const u64 initialNativeFlags)noexcept{
#pragma STDC FENV_ACCESS ON
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    const u64 clearNativeStatus = 0u;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(clearNativeStatus));
#else
    static_cast<void>(initialNativeFlags);
#endif
#if defined(NWB_PLATFORM_WINDOWS)
    _clearfp();
#endif
    if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT) || !RaiseFloatingPointExceptions(initialFlags))
        return MakeUnexpected(Failure{});
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    u64 seededNativeStatus{};
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(seededNativeStatus));
    seededNativeStatus |= initialNativeFlags;
    __asm__ __volatile__("msr fpsr, %0" : : "r"(seededNativeStatus));
#endif
    errno = E2BIG;
    const SIMDVectorSingle value = evaluate.get()(x, y);
    const i32 error = errno;
    const i32 flags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    u32 nativeStatus = 0u;
#if defined(NWB_PLATFORM_WINDOWS)
    nativeStatus = _statusfp();
#endif
    u64 rawNativeStatus{};
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(rawNativeStatus));
#endif
    return PowerObservation{ value, flags, error, nativeStatus, rawNativeStatus };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Math, VectorPowerPreservesMixedSpecialsAndAdjacentSrgbRoundingWithSeededState){
#pragma STDC FENV_ACCESS ON
    const i32 originalError = errno;
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    u64 originalNativeStatus{};
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(originalNativeStatus));
#endif
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restore([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            __builtin_trap();
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
        __asm__ __volatile__("msr fpsr, %0" : : "r"(originalNativeStatus));
#endif
        errno = originalError;
    });

    struct PowerCase{ PowerWords x, y; };
    constexpr PowerCase s_Cases[]{
        { { 1u, 0x80000001u, 0x007fffffu, 0x3f800000u }, { 0x3f800000u, 0x3f800000u, 0x3f800000u, 0x4019999au } },
        { { 0u, 0x3f800000u, 0x3e800000u, 0x3f000000u }, { 0x3ed55555u, 0xc0800000u, 0x80000000u, 0x3f800000u } },
        { { 0x37800000u, 0x47800000u, 0u, 0x3f800000u }, { 0xc0800000u, 0x40800000u, 0x40400000u, 0u } },
        { { 0x80000000u, 0u, 0xbf800000u, 0x7f7fffffu }, { 0xc0400000u, 0xbf800000u, 0x3f000000u, 0x40000000u } },
        { { 0x7fc00123u, 0x7f800125u, 0xbf800000u, 0x3f800000u }, { 0u, 0x3f800000u, 0xff800126u, 0x7f800125u } },
        { { 1u, 0x007fffffu, 0x00800000u, 0xff800000u }, { 0x40000000u, 0x3f000000u, 0x40200000u, 0xc0400000u } },
        { { 0x3d32b0b2u, 0x3d32b0b3u, 0x3d32b0b4u, 0x3eb048afu }, { 0x3ed55555u, 0x3ed55555u, 0x3ed55555u, 0x3ed55555u } },
        { { 0x3eb048aeu, 0x3eb048afu, 0x3eb048b0u, 0x3d2dc666u }, { 0x3ed55555u, 0x3ed55555u, 0x3ed55555u, 0xc13fc46au } }
    };
    constexpr i32 s_Modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    constexpr i32 s_InitialFlags[]{ 0, FE_INEXACT };
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    constexpr u32 s_DenormalModes[]{ _DN_SAVE, _DN_FLUSH };
#else
    constexpr u32 s_DenormalModes[]{ 0u };
#endif
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
    constexpr u64 s_NativeSeeds[]{ 0u, 0x80u, 0x08000000u, 0x08000080u };
#else
    constexpr u64 s_NativeSeeds[]{ 0u };
#endif
    for(const u32 denormalMode : s_DenormalModes){
#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
        u32 control{};
        ASSERT_EQ(_controlfp_s(&control, denormalMode, _MCW_DN), 0);
        ASSERT_EQ(control & _MCW_DN, denormalMode);
#endif
        for(const i32 mode : s_Modes){
            ASSERT_TRUE(SetFloatingPointRoundingMode(mode));
            for(const u64 nativeSeed : s_NativeSeeds){
                for(const i32 seed : s_InitialFlags){
                    for(usize index = 0u; index < LengthOf(s_Cases); ++index){
                        const SIMDVectorSingle x = BitCast<SIMDVectorSingle>(s_Cases[index].x), y = BitCast<SIMDVectorSingle>(s_Cases[index].y);
                        const auto expected = ObservePower(MakeNotNull<PowerEvaluate>(FrozenPower), x, y, seed, nativeSeed), actual = ObservePower(MakeNotNull<PowerEvaluate>(CurrentPower), x, y, seed, nativeSeed);
                        ASSERT_TRUE(expected);
                        ASSERT_TRUE(actual);
                        EXPECT_EQ(actual->flags, expected->flags) << denormalMode << ':' << mode << ':' << seed << ':' << index;
                        EXPECT_EQ(actual->error, expected->error) << denormalMode << ':' << mode << ':' << seed << ':' << index;
                        EXPECT_EQ(actual->nativeStatus, expected->nativeStatus) << denormalMode << ':' << mode << ':' << seed << ':' << index;
                        EXPECT_EQ(actual->rawNativeStatus, expected->rawNativeStatus) << denormalMode << ':' << mode << ':' << seed << ':' << index;
                        for(usize lane = 0u; lane < 4u; ++lane)
                            EXPECT_EQ(VectorGetIntByIndex(actual->value, lane), VectorGetIntByIndex(expected->value, lane)) << denormalMode << ':' << mode << ':' << seed << ':' << index << ':' << lane;
                    }
                }
            }
        }
    }
}


#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_HAS_NEON64) && !defined(_M_ARM64EC)
TEST(Math, VectorPowerKeepsDenormalInputStatusWithInvalidTrapEnabled){
#pragma STDC FENV_ACCESS ON
    const i32 originalError = errno;
    u64 originalNativeStatus{};
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(originalNativeStatus));
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restore([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            __builtin_trap();
        __asm__ __volatile__("msr fpsr, %0" : : "r"(originalNativeStatus));
        errno = originalError;
    });

    constexpr u32 s_DenormalModes[]{ _DN_SAVE, _DN_FLUSH };
    constexpr i32 s_Modes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    constexpr i32 s_InitialFlags[]{ 0, FE_INEXACT };
    const SIMDVectorSingle x = VectorSetInt(1u, 0x3f800000u, 0x3f800000u, 0x3f800000u);
    const SIMDVectorSingle y = VectorSet(2.0f, 1.0f, 1.0f, 1.0f);
    constexpr u64 s_NativeSeeds[]{ 0u, 0x80u, 0x08000000u, 0x08000080u };
    for(const u32 denormalMode : s_DenormalModes){
        u32 control{};
        ASSERT_EQ(_controlfp_s(&control, denormalMode, _MCW_DN), 0);
        ASSERT_EQ(control & _MCW_DN, denormalMode);
        ASSERT_EQ(_controlfp_s(&control, control & ~_EM_INVALID, _MCW_EM), 0);
        for(const i32 mode : s_Modes){
            ASSERT_TRUE(SetFloatingPointRoundingMode(mode));
            for(const u64 nativeSeed : s_NativeSeeds){
                for(const i32 seed : s_InitialFlags){
                    const auto expected = ObservePower(MakeNotNull<PowerEvaluate>(FrozenPower), x, y, seed, nativeSeed), actual = ObservePower(MakeNotNull<PowerEvaluate>(CurrentPower), x, y, seed, nativeSeed);
                    ASSERT_TRUE(expected);
                    ASSERT_TRUE(actual);
                    EXPECT_EQ(actual->flags, expected->flags) << denormalMode << ':' << mode << ':' << seed;
                    EXPECT_EQ(actual->error, expected->error) << denormalMode << ':' << mode << ':' << seed;
                    EXPECT_EQ(actual->nativeStatus, expected->nativeStatus) << denormalMode << ':' << mode << ':' << seed;
                    EXPECT_EQ(actual->rawNativeStatus, expected->rawNativeStatus) << denormalMode << ':' << mode << ':' << seed;
                    for(usize lane = 0u; lane < 4u; ++lane)
                        EXPECT_EQ(VectorGetIntByIndex(actual->value, lane), VectorGetIntByIndex(expected->value, lane)) << denormalMode << ':' << mode << ':' << seed << ':' << lane;
                }
            }
        }
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

