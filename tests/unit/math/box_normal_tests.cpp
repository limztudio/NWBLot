// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simdmath.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>

#include <cfenv>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_box_normal_tests{


// Keep production floating-point expressions in their normal optimization context while observing their flags at the caller.
NWB_NOINLINE SIMDVectorSingle NWB_SIMD_CALL EvaluateSingleBoxNormal(SIMDVectorSingle position, SIMDVectorSingle extents, SIMDVectorSingle fallback, const f32 threshold)noexcept{
    return SdfTests::BoxNormal(position, extents, fallback, threshold);
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MathBoxNormal, FiniteSquaredLengthDoesNotOverflowInUnusedReductionLanes){
#pragma STDC FENV_ACCESS ON
    using namespace __hidden_box_normal_tests;
    fenv_t originalEnvironment{};
    ASSERT_EQ(feholdexcept(&originalEnvironment), 0);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(fesetenv(&originalEnvironment) != 0)
            __builtin_trap();
    });

    // Z squared is finite but exceeds half the maximum float; doubling it in a dead reduction lane raises a spurious overflow.
    volatile u32 inputBits[]{ 0xc6ab1d80u, 0x51272bbbu, 0x5f60eb2cu, 0x677561fau, 0xc1aeb5b9u, 0x7213a692u, 0xd272655fu, 0xdd4e3655u, 0xa3419f79u, 0xb181e207u, 0xcedc8003u, 0x65e759c3u, 0x86a287b0u };
    const SIMDVectorSingle position = VectorSetInt(inputBits[0], inputBits[1], inputBits[2], inputBits[3]);
    const SIMDVectorSingle extents = VectorSetInt(inputBits[4], inputBits[5], inputBits[6], inputBits[7]);
    const SIMDVectorSingle fallback = VectorSetInt(inputBits[8], inputBits[9], inputBits[10], inputBits[11]);
    const f32 threshold = BitCast<f32>(static_cast<u32>(inputBits[12]));
    for(const i32 roundingMode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }){
        ASSERT_EQ(fesetround(roundingMode), 0);
        ASSERT_EQ(feclearexcept(FE_ALL_EXCEPT), 0);
        const SIMDVectorSingle normal = EvaluateSingleBoxNormal(position, extents, fallback, threshold);
        const i32 flags = fetestexcept(FE_ALL_EXCEPT);
        EXPECT_EQ(flags & FE_OVERFLOW, 0) << roundingMode;
        EXPECT_TRUE(VectorIsFinite(normal, VectorComponentMask::s_XYZ)) << roundingMode;
        EXPECT_GT(VectorGetZ(normal), 0.99999f) << roundingMode;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

