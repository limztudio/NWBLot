// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>

#include <fenv.h>


#if defined(NWB_PLATFORM_LINUX) && defined(NWB_HAS_AVX2)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_native_reduction_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Reduction{
enum Enum : u32{
    Dot2,
    Dot3,
    Dot4,
    Sum
};
};

// Instantiate each frozen formula separately so runtime dispatch does not alter the legacy SSE NaN operand selection.
template<Reduction::Enum Kind>
NWB_NOINLINE SIMDVectorDouble OriginalReduction(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Kind == Reduction::Dot2){
        lhs = VectorDoubleAndInt(lhs, s_DoubleSIMDMaskXY);
        rhs = VectorDoubleAndInt(rhs, s_DoubleSIMDMaskXY);
    }
    else if constexpr(Kind == Reduction::Dot3){
        lhs = VectorDoubleAndInt(lhs, s_DoubleSIMDMask3);
        rhs = VectorDoubleAndInt(rhs, s_DoubleSIMDMask3);
    }
    const SIMDVectorDouble values = Kind == Reduction::Sum ? lhs : VectorDoubleMultiply(lhs, rhs);
    SIMDVectorDouble result = VectorDoubleAdd(VectorDoubleSplatX(values), VectorDoubleSplatY(values));
    if constexpr(Kind == Reduction::Dot2)
        return result;
    result = VectorDoubleAdd(result, VectorDoubleSplatZ(values));
    if constexpr(Kind == Reduction::Dot3)
        return result;
    return VectorDoubleAdd(result, VectorDoubleSplatW(values));
}

template<Reduction::Enum Kind>
NWB_NOINLINE SIMDVectorDouble CurrentReduction(SIMDVectorDouble lhs, SIMDVectorDouble rhs)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Kind == Reduction::Dot2)
        return VectorDouble2Dot(lhs, rhs);
    else if constexpr(Kind == Reduction::Dot3)
        return VectorDouble3Dot(lhs, rhs);
    else if constexpr(Kind == Reduction::Dot4)
        return VectorDouble4Dot(lhs, rhs);
    else
        return VectorDoubleSum(lhs);
}

template<Reduction::Enum Kind>
static void CheckReductionEdge(SIMDVectorDouble input, SIMDVectorDouble multiplier, const i32 roundingMode, const usize index){
#pragma STDC FENV_ACCESS ON
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorDouble expected = OriginalReduction<Kind>(input, multiplier);
    const i32 expectedExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorDouble actual = CurrentReduction<Kind>(input, multiplier);
    const i32 actualExceptions = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    EXPECT_EQ(actualExceptions, expectedExceptions) << roundingMode << ':' << index << ':' << Kind;
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorDoubleGetIntByIndex(actual, lane), VectorDoubleGetIntByIndex(expected, lane)) << roundingMode << ':' << index << ':' << Kind << ':' << lane;
}

TEST(MathNativeReduction, MultipleNaNsInactiveSignalingLanesAndOrderedCancellationPreserveBitsAndExceptions){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    // Scalar-lane reductions can commute multiple NaN payloads or accidentally evaluate an unused signaling lane.
    constexpr u64 s_InputBits[][8]{
        { 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff8000000000125ull, 0xfff8000000000126ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 0x4000000000000000ull, 0x4008000000000000ull, 0x7ff0000000000125ull, 0xfff0000000000126ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 0x4000000000000000ull, 0x4008000000000000ull, 0x4010000000000000ull, 0xfff0000000000126ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 0x43b0000000000000ull, 0x3ff0000000000000ull, 0xc3b0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 0x7fefffffffffffffull, 0x7fefffffffffffffull, 0xffefffffffffffffull, 0xffefffffffffffffull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 1u, 0x8000000000000001ull, 0x8000000000000000ull, 0u, 0xbff0000000000000ull, 0xbff0000000000000ull, 0x3ff0000000000000ull, 0x3ff0000000000000ull },
        { 0x7ff8000000000123ull, 0xfff0000000000124ull, 0x7ff8000000000125ull, 0xfff0000000000126ull, 0xfff8000000000233ull, 0x7ff0000000000234ull, 0xfff0000000000235ull, 0x7ff8000000000236ull },
        { 0x7ff0000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000000ull, 0xfff0000000000000ull, 0xfff0000000000233ull, 0x7ff8000000000234ull, 0u, 0x7ff0000000000000ull }
    };
    constexpr i32 s_RoundingModes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    for(const i32 roundingMode : s_RoundingModes){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));
        for(usize index = 0u; index < LengthOf(s_InputBits); ++index){
            const SIMDVectorDouble input = VectorDoubleSetInt(s_InputBits[index][0], s_InputBits[index][1], s_InputBits[index][2], s_InputBits[index][3]);
            const SIMDVectorDouble multiplier = VectorDoubleSetInt(s_InputBits[index][4], s_InputBits[index][5], s_InputBits[index][6], s_InputBits[index][7]);
            CheckReductionEdge<Reduction::Dot2>(input, multiplier, roundingMode, index);
            CheckReductionEdge<Reduction::Dot3>(input, multiplier, roundingMode, index);
            CheckReductionEdge<Reduction::Dot4>(input, multiplier, roundingMode, index);
            CheckReductionEdge<Reduction::Sum>(input, multiplier, roundingMode, index);
        }
    }
}

template<u32 Dimension>
NWB_NOINLINE SIMDVectorDouble OriginalReciprocalLength(SIMDVectorDouble value)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Dimension == 2u)
        return VectorDoubleReciprocalSqrt(VectorDouble2LengthSq(value));
    else if constexpr(Dimension == 3u)
        return VectorDoubleReciprocalSqrt(VectorDouble3LengthSq(value));
    else
        return VectorDoubleReciprocalSqrt(VectorDouble4LengthSq(value));
}

template<u32 Dimension, bool Estimate>
NWB_NOINLINE SIMDVectorDouble CurrentReciprocalLength(SIMDVectorDouble value)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Dimension == 2u){
        if constexpr(Estimate)
            return VectorDouble2ReciprocalLengthEst(value);
        else
            return VectorDouble2ReciprocalLength(value);
    }
    else if constexpr(Dimension == 3u){
        if constexpr(Estimate)
            return VectorDouble3ReciprocalLengthEst(value);
        else
            return VectorDouble3ReciprocalLength(value);
    }
    else{
        if constexpr(Estimate)
            return VectorDouble4ReciprocalLengthEst(value);
        else
            return VectorDouble4ReciprocalLength(value);
    }
}

template<u32 Dimension>
static void CheckReciprocalLengthEdge(SIMDVectorDouble input){
#pragma STDC FENV_ACCESS ON
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorDouble expected = OriginalReciprocalLength<Dimension>(input);
    const i32 expectedFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorDouble actual = CurrentReciprocalLength<Dimension, false>(input);
    const i32 actualFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDVectorDouble estimate = CurrentReciprocalLength<Dimension, true>(input);
    const i32 estimateFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
    EXPECT_EQ(actualFlags, expectedFlags) << Dimension;
    EXPECT_EQ(estimateFlags, expectedFlags) << Dimension;
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(VectorDoubleGetIntByIndex(actual, lane), VectorDoubleGetIntByIndex(expected, lane)) << Dimension << ':' << lane;
        EXPECT_EQ(VectorDoubleGetIntByIndex(estimate, lane), VectorDoubleGetIntByIndex(expected, lane)) << Dimension << ':' << lane;
    }
}

TEST(MathNativeReduction, ReciprocalLengthsPreserveZeroDivisionSubnormalSquaresAndNonfiniteInactiveLanes){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );
    constexpr u64 s_Cases[][4]{
        { 0u, 0x8000000000000000ull, 0u, 0x8000000000000000ull },
        { 0x1e60000000000000ull, 0u, 0u, 0u },
        { 0x7ff0000000000000ull, 0u, 0u, 0u },
        { 0x4000000000000000ull, 0x4008000000000000ull, 0x7ff0000000000123ull, 0xfff0000000000124ull },
        { 0x4000000000000000ull, 0x4008000000000000ull, 0x4010000000000000ull, 0xfff0000000000124ull },
        { 0x7ff8000000000123ull, 0xfff8000000000124ull, 0x7ff0000000000125ull, 0xfff0000000000126ull }
    };
    constexpr i32 s_RoundingModes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    for(const i32 roundingMode : s_RoundingModes){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));
        for(const auto& bits : s_Cases){
            const SIMDVectorDouble input = VectorDoubleSetInt(bits[0], bits[1], bits[2], bits[3]);
            CheckReciprocalLengthEdge<2u>(input);
            CheckReciprocalLengthEdge<3u>(input);
            CheckReciprocalLengthEdge<4u>(input);
        }
    }
}

#if defined(NWB_PLATFORM_LINUX) && defined(NWB_HAS_AVX2)
static void TrapPriorityHandler(const i32, siginfo_t* information, void*)noexcept{
    _exit(information != nullptr && information->si_code == FPE_FLTINV ? 0 : 1);
}

TEST(MathNativeReduction, PackedDot3KeepsInvalidPriorityWhenAnotherActiveLaneOverflows){
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if(child == 0){
        struct sigaction action{};
        action.sa_sigaction = TrapPriorityHandler;
        action.sa_flags = SA_SIGINFO;
        if(sigemptyset(&action.sa_mask) != 0 || sigaction(SIGFPE, &action, nullptr) != 0)
            _exit(3);
        if(!ClearFloatingPointExceptions(FE_ALL_EXCEPT) || feenableexcept(FE_INVALID | FE_OVERFLOW) == -1)
            _exit(4);
        volatile u64 maximumBits = 0x7fefffffffffffffull;
        const SIMDVectorDouble lhs = VectorDoubleSetInt(maximumBits, 0x3ff0000000000000ull, 0u, 0u);
        const SIMDVectorDouble rhs = VectorDoubleSet(2.0, 1.0, Limit<f64>::s_Infinity, 0.0);
        volatile SIMDVectorDouble result = VectorDouble3Dot(lhs, rhs);
        _exit(VectorDoubleMoveMask(result) != 0u ? 2 : 5);
    }
    i32 status{};
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

