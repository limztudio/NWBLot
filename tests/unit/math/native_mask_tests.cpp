// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_native_mask_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// This preserved mask formula qualifies discarded-lane flags in the same compiler and optimization context as the candidate.
NWB_NOINLINE bool BaselineCompareDouble3(SIMDVectorDouble left, SIMDVectorDouble right)noexcept{
#pragma STDC FENV_ACCESS ON
    const SIMDVectorDouble comparison = VectorDoubleEqual(left, right);
#if defined(NWB_HAS_NEON64)
    const uint64x2_t low = vshrq_n_u64(SIMDVectorDoubleDetail::GetLowBits(comparison), 63u);
    const uint64x2_t high = vshrq_n_u64(SIMDVectorDoubleDetail::GetHighBits(comparison), 63u);
    const u32 mask = static_cast<u32>(vgetq_lane_u64(low, 0) | (vgetq_lane_u64(low, 1) << 1u) | (vgetq_lane_u64(high, 0) << 2u) | (vgetq_lane_u64(high, 1) << 3u));
#else
    const u32 mask = VectorDoubleMoveMask(comparison);
#endif
    return (mask & VectorDoubleComponentMask::s_DoubleXYZ) == VectorDoubleComponentMask::s_DoubleXYZ;
}

template<bool Integer, u32 Dimension>
NWB_NOINLINE bool CompareDouble(SIMDVectorDouble left, SIMDVectorDouble right)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Integer){
        if constexpr(Dimension == 3u)
            return VectorDouble3EqualInt(left, right);
        else
            return VectorDouble4EqualInt(left, right);
    }
    else{
        if constexpr(Dimension == 3u)
            return VectorDouble3Equal(left, right);
        else
            return VectorDouble4Equal(left, right);
    }
}

template<bool Integer>
NWB_NOINLINE bool CompareSingle(SIMDVectorSingle left, SIMDVectorSingle right)noexcept{
#pragma STDC FENV_ACCESS ON
    if constexpr(Integer)
        return Vector4EqualInt(left, right);
    else
        return Vector4Equal(left, right);
}

NWB_NOINLINE u32 ExtractMask(SIMDVectorDouble value)noexcept{
#pragma STDC FENV_ACCESS ON
    return VectorDoubleMoveMask(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathNativeMask, RawSignBitsIgnoreLowWordSignAndNoncanonicalNaNPayloads){
#pragma STDC FENV_ACCESS ON
    using namespace __hidden_native_mask_tests;
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    // Packing binary64 high words must ignore low-word sign bits and must not classify signaling NaNs as floating-point values.
    constexpr u64 s_Magnitudes[]{ 0x0000000080000000ull, 0x0000000000000001ull, 0x7FF0000080000001ull, 0x7FF8123476543210ull };
    for(u32 expected = 0u; expected < 16u; ++expected){
        const SIMDVectorDouble value = VectorDoubleSetInt(
            s_Magnitudes[0] | (static_cast<u64>(expected & 1u) << 63u),
            s_Magnitudes[1] | (static_cast<u64>((expected >> 1u) & 1u) << 63u),
            s_Magnitudes[2] | (static_cast<u64>((expected >> 2u) & 1u) << 63u),
            s_Magnitudes[3] | (static_cast<u64>((expected >> 3u) & 1u) << 63u)
        );
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_EQ(ExtractMask(value), expected) << expected;
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0) << expected;
    }
}

TEST(MathNativeMask, PartialAndFullComparisonsPreserveSignalingLaneExceptions){
#pragma STDC FENV_ACCESS ON
    using namespace __hidden_native_mask_tests;
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    constexpr i32 s_RoundingModes[]{ FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO };
    for(const i32 roundingMode : s_RoundingModes){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));
        for(usize poisonLane = 0u; poisonLane < 4u; ++poisonLane){
            const SIMDVectorSingle referenceSingle = VectorReplicate(1.0f);
            const SIMDVectorDouble referenceDouble = VectorDoubleReplicate(1.0);
            const SIMDVectorSingle single = VectorSetIntByIndex(referenceSingle, 0x7F801234u, poisonLane);
            const SIMDVectorDouble value = VectorDoubleSetIntByIndex(referenceDouble, 0x7FF0000000001234ull, poisonLane);

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            EXPECT_FALSE(CompareSingle<false>(single, referenceSingle)) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), FE_INVALID) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            EXPECT_TRUE(CompareSingle<true>(single, single)) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            const bool baselinePartial = BaselineCompareDouble3(value, referenceDouble);
            const i32 baselinePartialFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
            EXPECT_EQ(baselinePartial, poisonLane == 3u) << roundingMode << ':' << poisonLane;
            if(poisonLane != 3u)
                EXPECT_EQ(baselinePartialFlags, FE_INVALID) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            const bool actualPartial = CompareDouble<false, 3u>(value, referenceDouble);
            const i32 actualPartialFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
            EXPECT_EQ(actualPartial, baselinePartial) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(actualPartialFlags, baselinePartialFlags) << roundingMode << ':' << poisonLane;
            if(poisonLane != 3u)
                EXPECT_EQ(actualPartialFlags, FE_INVALID) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            EXPECT_FALSE((CompareDouble<false, 4u>(value, referenceDouble))) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), FE_INVALID) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            EXPECT_TRUE((CompareDouble<true, 3u>(value, value))) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0) << roundingMode << ':' << poisonLane;

            ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
            EXPECT_TRUE((CompareDouble<true, 4u>(value, value))) << roundingMode << ':' << poisonLane;
            EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0) << roundingMode << ':' << poisonLane;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

