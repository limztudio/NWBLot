// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compact_string.adhd.h"
#include "builtin_shadow_global.adhd.h"

#include <global/floating_point_environment.h>
#include <tests/common/floating_point_environment_guard.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AdhdCompactString, SearchAndBoundariesUseCanonicalStoredText){
    const ACompactString value("Find\\NEEDLE");
    EXPECT_EQ(NWB::Tests::findCompact(value), 5u);
    EXPECT_EQ(NWB::Tests::firstCompact(value), static_cast<u8>('f'));
    EXPECT_EQ(NWB::Tests::lastCompact(value), static_cast<u8>('e'));
    EXPECT_EQ(NWB::Tests::findCompact(ACompactString("missing")), ACompactString::s_NPos);
}


TEST(AdhdCompiler, ScalarLerpEvaluatesSideEffectOnce){
    i32 counter = 1;
    EXPECT_FLOAT_EQ(NWB::Tests::lerpWithCounter(counter), 6.0f);
    EXPECT_EQ(counter, 2);
}


struct LerpMoveOnly{
    i32 value = 10;
    LerpMoveOnly() = default;
    explicit LerpMoveOnly(i32 v) : value(v){}
    LerpMoveOnly(const LerpMoveOnly&) = delete;
    LerpMoveOnly(LerpMoveOnly&&) = default;
    LerpMoveOnly operator-(const LerpMoveOnly& rhs)&&{ return LerpMoveOnly(value - rhs.value); }
    LerpMoveOnly operator*(LerpMoveOnly&& rhs)&&{ return LerpMoveOnly(value * rhs.value); }
    LerpMoveOnly operator+(LerpMoveOnly&& rhs)&{ return LerpMoveOnly(value + rhs.value); }
    LerpMoveOnly operator+(LerpMoveOnly&& rhs)&&{ return LerpMoveOnly(value + rhs.value + 100); }
};

TEST(AdhdCompiler, ScalarLerpPreservesMoveOnlyOperandCategories){
    LerpMoveOnly value(2);
    const LerpMoveOnly result = NWB::Tests::lerpMoveOnly(value);
    EXPECT_EQ(result.value, 82);
    EXPECT_EQ(value.value, 2);
}


TEST(AdhdCompiler, UserCallableShadowsBuiltinInGeneratedCode){
    EXPECT_EQ(NWB::Tests::callShadowedMax(3, 7), -4);
}


TEST(AdhdCompiler, UserCallableShadowsBuiltinForSimd){
    EXPECT_FLOAT_EQ(NWB::Tests::callShadowedVectorMax(), -4.0f);
}


TEST(AdhdCompiler, ResolvedBuiltinStillUsesSimdImplementation){
    EXPECT_FLOAT_EQ(NWB::Tests::callBuiltinVectorMin(), 3.0f);
}


TEST(AdhdCompiler, LocalCallableShadowsBuiltin){
    EXPECT_EQ(NWB::Tests::callShadowedLocalMin(), 17);
}


TEST(AdhdCompiler, NestedSimdCallKeepsUserCallableIdentity){
    EXPECT_FLOAT_EQ(NWB::Tests::callNestedShadowedVector(), -4.0f);
}


TEST(AdhdCompiler, TransposeDoesNotFuseUserDefinedMultiply){
    EXPECT_FLOAT_EQ(NWB::Tests::callShadowedMatrixMul(), 3.0f);
}


TEST(AdhdCompiler, LocalCallableShadowsSimdReduction){
    EXPECT_FLOAT_EQ(NWB::Tests::callShadowedLocalDot(), -4.0f);
}


TEST(AdhdCompiler, LocalParameterShadowsBuiltinConstant){
    EXPECT_EQ(NWB::Tests::callShadowedEntityConstant(37), 37);
}


TEST(AdhdCompiler, UnshadowedBuiltinConstantStillResolves){
    EXPECT_TRUE(NWB::Tests::readBuiltinEntityConstant() == NWB::Core::ECS::s_InvalidEntityId);
}


TEST(AdhdCompiler, GlobalConstantShadowsBuiltinInTypeInference){
    EXPECT_EQ(NWB::Tests::readShadowedGlobalConstant(), 44);
}


TEST(AdhdCompiler, ScalarLerpKeepsFirstTemporaryCategory){
    const LerpMoveOnly result = NWB::Tests::lerpTemporary<LerpMoveOnly>();
    EXPECT_EQ(result.value, 110);
}


TEST(AdhdCompiler, TrimViewBorrowsLocalCompactString){
    EXPECT_EQ(NWB::Tests::trimLocalCompact(), 7u);
}


TEST(AdhdCompiler, StringAndCompactSubstrPreserveOwnershipAndSingleEvaluation){
    i32 recvCounter = 0;
    i32 posCounter = 0;
    i32 countCounter = 0;
    const AString result = NWB::Tests::extractLongStringSubstrOnce(recvCounter, posCounter, countCounter);
    EXPECT_EQ(recvCounter, 1);
    EXPECT_EQ(posCounter, 1);
    EXPECT_EQ(countCounter, 1);
    EXPECT_EQ(result.size(), 72u);
    EXPECT_EQ(AStringView(result.data(), result.size()), AStringView("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_tail_segm"));

    const ACompactString compact("Header/SubstrPayload/Tail");
    const ACompactString sliced = NWB::Tests::extractCompactSubstr(compact, 7u, 13u);
    EXPECT_EQ(sliced.view(), AStringView("substrpayload"));
}


TEST(AdhdCompiler, HighByteFrontBackAndIndexMaintainUnsignedSemanticsAndMutation){
    static constexpr char s_ViewBytes[] = { static_cast<char>(0x80u), static_cast<char>(0x7fu), static_cast<char>(0xffu) };
    static constexpr char s_CompactBytes[] = { static_cast<char>(0x80u), static_cast<char>(0xfeu), '\0' };
    const AStringView viewValue(s_ViewBytes, sizeof(s_ViewBytes));
    const ACompactString compactValue(s_CompactBytes);

    const u32 expected = (0x80u + 0xffu + 0x80u + 0xfeu)
        + static_cast<u32>(1000 + 0x80)
        + static_cast<u32>(2000 + 0xfe)
        + (0x40u + 0x81u + 0x80u);
    EXPECT_EQ(NWB::Tests::exerciseHighByteStringOps(viewValue, compactValue), expected);
    EXPECT_EQ(NWB::Tests::borrowLocalStringAcrossValueCall(), 76u + 72u);
}


TEST(AdhdCompiler, SimdLaneComparisonAndFiniteChecksIgnoreInactiveAndShortCircuitedSignalingNaNs){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    const NWB::Tests::FloatingPointEnvironmentGuard restoreEnvironment(*originalEnvironment);

    volatile u32 snan32 = 0x7f800123u;
    volatile u64 snan64 = 0x7ff0000000000123ull;
    const SIMDVectorSingle inactive32 = VectorSetInt(0u, 0u, 0u, snan32);
    const SIMDVectorSingle shortCircuit32 = VectorSetInt(0x3f800000u, snan32, 0u, snan32);
    const SIMDVectorSingle active32 = VectorSetInt(0u, snan32, 0u, 0u);
    const SIMDVectorDouble inactive64 = VectorDoubleSetInt(0ull, 0ull, 0ull, snan64);
    const SIMDVectorDouble shortCircuit64 = VectorDoubleSetInt(0x3ff0000000000000ull, snan64, 0ull, snan64);
    const SIMDVectorDouble active64 = VectorDoubleSetInt(0ull, snan64, 0ull, 0ull);

    for(const i32 roundingMode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::anyLaneGreater3Single(inactive32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::anyLaneGreater3Single(shortCircuit32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::anyLaneGreater3Single(active32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT) & FE_INVALID, FE_INVALID);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::anyLaneGreater3Double(inactive64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::anyLaneGreater3Double(shortCircuit64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_FALSE(NWB::Tests::anyLaneGreater3Double(active64, VectorDoubleZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT) & FE_INVALID, FE_INVALID);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::anyLaneGreater2Single(shortCircuit32, VectorZero()));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);

        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        EXPECT_TRUE(NWB::Tests::finiteLane3Single(inactive32));
        EXPECT_FALSE(NWB::Tests::finiteLane3Single(active32));
        EXPECT_TRUE(NWB::Tests::finiteLane3Double(inactive64));
        EXPECT_FALSE(NWB::Tests::finiteLane3Double(active64));
        EXPECT_EQ(TestFloatingPointExceptions(FE_ALL_EXCEPT), 0);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

