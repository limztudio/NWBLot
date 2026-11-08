// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <gtest/gtest.h>

#include <global/atomic_identity.h>
#include <global/auto_registration.h>
#include <global/basic_string.h>
#include <global/binary.h>
#include <global/binary_big_endian.h>
#include <global/binary_little_endian.h>
#include <global/bit.h>
#include <global/bit_matrix.h>
#include <global/call_text.h>
#include <global/checked_sum.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/csv_text.h>
#include <global/disjoint_set.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/fixed_buffer.h>
#include <global/hierarchical_index_set.h>
#include <global/integer_rect.h>
#include <global/limit.h>
#include <global/name_hash.h>
#include <global/not_null.h>
#include <global/overflow.h>
#include <global/process_memory_map.h>
#include <global/projected_index.h>
#include <global/quantization.h>
#include <global/simplemath.h>
#include <global/simd/color.h>
#include <global/sha1.h>
#include <global/append_rollback.h>
#include <global/range_partition.h>
#include <global/range_algorithms.h>
#include <global/latin1_text.h>
#include <global/interval.h>
#include <global/source_text.h>
#include <global/string_hash.h>
#include <global/statistics.h>
#include <global/sync.h>
#include <global/termination.h>
#include <global/text_utils.h>
#include <global/text_replacements.h>
#include <global/unicode/grapheme.h>
#include <global/utf8_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_numeric_shared_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, NumericSharedHelpersPreserveEdgeCases){
    const Array<f64, 2u> signedZeroSamples{ 0.0, -0.0 };
    const OnlineF64Statistics zeroStatistics = ComputeOnlineF64Statistics(signedZeroSamples);
    EXPECT_EQ(zeroStatistics.count, 2u);
    EXPECT_EQ(BitCast<u64>(zeroStatistics.minimum), 0x8000000000000000ull);
    EXPECT_EQ(BitCast<u64>(zeroStatistics.maximum), 0x8000000000000000ull);
    EXPECT_EQ(QuantizeUNormScalar(-Limit<f32>::s_Infinity, 255.0f), 0.0f);
    EXPECT_EQ(QuantizeUNormScalar(Limit<f32>::s_Infinity, 255.0f), 255.0f);
    EXPECT_EQ(QuantizeUNormScalar(0.5f, 255.0f), 128.0f);
    EXPECT_EQ(QuantizeSNormScalar(-0.5f, 127.0f), -64.0f);
    EXPECT_EQ(BitCast<u32>(QuantizeSNormScalar(-0.0f, 127.0f)), 0u);
    EXPECT_EQ(QuantizeSNormScalar(BitCast<f32>(0x7fc01234u), 127.0f), -127.0f);
    const SIMDVectorSingle signedColor = VectorQuantizeSNorm(VectorSet(-0.0f, -0.5f, 0.5f, -2.0f), VectorReplicate(127.0f));
    EXPECT_EQ(VectorGetX(signedColor), 0.0f);
    EXPECT_EQ(VectorGetY(signedColor), -64.0f);
    EXPECT_EQ(VectorGetZ(signedColor), 64.0f);
    EXPECT_EQ(VectorGetW(signedColor), -127.0f);
    const SIMDVectorSingle quantizedColor = VectorQuantizeUNorm(VectorSet(-1.0f, 0.5f, 1.0f, 2.0f), VectorReplicate(255.0f));
    EXPECT_EQ(VectorGetX(quantizedColor), 0.0f);
    EXPECT_EQ(VectorGetY(quantizedColor), 128.0f);
    EXPECT_EQ(VectorGetZ(quantizedColor), 255.0f);
    EXPECT_EQ(VectorGetW(quantizedColor), 255.0f);
    const SIMDVectorSingle encodedColor = VectorLinearToSRGB(VectorSet(-1.0f, 0.0031308f, 1.0f, 2.0f));
    EXPECT_EQ(VectorGetX(encodedColor), 0.0f);
    EXPECT_NEAR(VectorGetY(encodedColor), 0.0031308f * 12.92f, 0.000001f);
    EXPECT_NEAR(VectorGetZ(encodedColor), 1.0f, 0.000001f);
    EXPECT_NEAR(VectorGetW(encodedColor), 1.0f, 0.000001f);
    const Array<u32, 2u> narrowSum{ Limit<u32>::s_Max, 1u };
    const auto wrappedNarrowSum = TrySumUnsignedWithU64Limit<u32>(narrowSum);
    ASSERT_TRUE(wrappedNarrowSum);
    EXPECT_EQ(*wrappedNarrowSum, 0u);
    const Array<u64, 2u> wideSumOverflow{ Limit<u64>::s_Max, 1u };
    EXPECT_FALSE(TrySumUnsignedWithU64Limit<u64>(wideSumOverflow));
    EXPECT_EQ(TrySumUnsignedWithU64Limit<u64>(Span<const u64>{}).value(), 0u);
    const Array<u64, 2u> exactMaximumSum{ Limit<u64>::s_Max - 1u, 1u };
    EXPECT_EQ(TrySumUnsignedWithU64Limit<u64>(exactMaximumSum).value(), Limit<u64>::s_Max);
    const Array<u64, 2u> maximumThenZero{ Limit<u64>::s_Max, 0u };
    EXPECT_FALSE(TrySumNonzeroU64(maximumThenZero));
    const Array<u64, 1u> maximumCount{ Limit<u64>::s_Max };
    const Array<u64, 2u> overflowingCounts{ Limit<u64>::s_Max, 1u };
    const Array<u64, 1u> zeroCount{ 0u };
    EXPECT_EQ(TrySumNonzeroU64(Span<const u64>{}).value(), 0u);
    EXPECT_EQ(TrySumNonzeroU64(maximumCount).value(), Limit<u64>::s_Max);
    EXPECT_FALSE(TrySumNonzeroU64(overflowingCounts));
    EXPECT_FALSE(TrySumNonzeroU64(zeroCount));
    u64 total = 11u;
    EXPECT_FALSE(AccumulateBoundedCount(0u, 10u, total));
    EXPECT_EQ(total, 11u);
    total = Limit<u64>::s_Max - 1u;
    EXPECT_FALSE(AccumulateBoundedCount(2u, Limit<u64>::s_Max, total));
    EXPECT_EQ(total, Limit<u64>::s_Max - 1u);

    EXPECT_EQ(PackUnorderedU32Pair(0u, Limit<u32>::s_Max), 0xffffffffull);
    EXPECT_EQ(PackUnorderedU32Pair(Limit<u32>::s_Max, 0u), 0xffffffffull);
    EXPECT_EQ(PackUnorderedU32Pair(Limit<u32>::s_Max, Limit<u32>::s_Max), Limit<u64>::s_Max);
    constexpr u64 packedWords = PackU32Words(0xffffffffu, 0x12345678u);
    EXPECT_EQ(packedWords, 0x12345678ffffffffull);
    EXPECT_EQ(LowU32Word(packedWords), 0xffffffffu);
    EXPECT_EQ(HighU32Word(packedWords), 0x12345678u);
    EXPECT_EQ(PackFourCharTag('H', 'a', 'n', 'g'), 0x48616e67u);
    EXPECT_EQ(PackFourCharTag(static_cast<char>(0x80), 'a', 'b', 'c'), (static_cast<u32>(static_cast<char>(0x80)) << 24u) | 0x00616263u);
    EXPECT_EQ(*SignedDifference(0u, static_cast<u64>(Limit<i64>::s_Max) + 1u), Limit<i64>::s_Min);
    EXPECT_FALSE(SignedDifference(0u, static_cast<u64>(Limit<i64>::s_Max) + 2u));
    EXPECT_FALSE(SignedDifference(static_cast<u64>(Limit<i64>::s_Max) + 1u, 0u));
    EXPECT_FALSE(AlignUpCheckedSize(Limit<usize>::s_Max, 4u));
    EXPECT_FALSE(AlignUpChecked(Limit<usize>::s_Max, usize{4u}));
    ASSERT_TRUE(AlignUpChecked(Limit<usize>::s_Max - 3u, usize{4u}));
    EXPECT_EQ(*AlignUpChecked(Limit<usize>::s_Max - 3u, usize{4u}), Limit<usize>::s_Max - 3u);
    EXPECT_TRUE(DifferentF64Bits(0.0, -0.0));
    EXPECT_FALSE(DifferentF64Bits(BitCast<f64>(0x7ff8000000001234ull), BitCast<f64>(0x7ff8000000001234ull)));
    EXPECT_TRUE(DifferentF64Bits(BitCast<f64>(0x7ff8000000001234ull), BitCast<f64>(0x7ff8000000001235ull)));
    EXPECT_EQ(SaturateU64ToI32(static_cast<u64>(Limit<i32>::s_Max) + 1u), Limit<i32>::s_Max);

    EXPECT_EQ(AddSignedOffset(u64(1) << 63u, Limit<i64>::s_Min).value(), 0u);
    EXPECT_FALSE(AddSignedOffset(0u, Limit<i64>::s_Min));
    EXPECT_FALSE(AddSignedOffset(Limit<u64>::s_Max, 1));
    EXPECT_EQ(DivideUpWrapping(Limit<u32>::s_Max, 8u), 0u);
    EXPECT_EQ(DivideUpWrapping(Limit<u64>::s_Max, u64(2)), 0u);
    EXPECT_EQ(DivideUpWrapping(usize(9), 8u), usize(2));
    const BalancedPartitionRange firstRange = ComputeBalancedPartitionRange(10u, 0u, 2u, 1u);
    const BalancedPartitionRange lastRange = ComputeBalancedPartitionRange(10u, 2u, 2u, 1u);
    EXPECT_EQ(firstRange.first, 10u);
    EXPECT_EQ(firstRange.last, 13u);
    EXPECT_EQ(lastRange.first, 15u);
    EXPECT_EQ(lastRange.last, 17u);
    Atomic<u64> wrappingIdentity{ Limit<u64>::s_Max };
    EXPECT_EQ(NextAtomicWrappingNonzero(wrappingIdentity), 1u);
    u64 wrappingRevision = Limit<u64>::s_Max;
    AdvanceWrappingNonzero(wrappingRevision);
    EXPECT_EQ(wrappingRevision, 1u);
    EXPECT_FALSE(HasSingleSetBit(u16(0)));
    EXPECT_TRUE(HasSingleSetBit(u16(0x8000)));
    EXPECT_EQ(LowestSetBit(u8(0x90)), u8(0x10));
    EXPECT_EQ(AlignmentOffsetPowerOfTwo(Limit<usize>::s_Max, 8u), 1u);
    EXPECT_EQ(SubtractSaturating(u64(1), u64(2)), 0u);
    EXPECT_EQ(BitWidthLowU32(0xffffffffu, 0u), 0u);
    EXPECT_EQ(BitWidthLowU32(0x80000100u, 8u), 0u);
    EXPECT_EQ(BitWidthLowU32(0x80000000u, 32u), 32u);
    EXPECT_TRUE(IsNewerSerialU32(0u, Limit<u32>::s_Max));
    EXPECT_FALSE(IsNewerSerialU32(0x80000000u, 0u));
    EXPECT_FALSE(IsNewerSerialU32(7u, 7u));
    EXPECT_TRUE(IsNotOlderSerialU32(7u, 7u));
    EXPECT_TRUE(IsNotOlderSerialU32(0u, Limit<u32>::s_Max));
    EXPECT_TRUE(IsNotOlderSerialU32(0x7fffffffu, 0u));
    EXPECT_FALSE(IsNotOlderSerialU32(0x80000000u, 0u));
    EXPECT_FALSE(IsNotOlderSerialU32(0x80000001u, 0u));
    EXPECT_TRUE(IsSerialU32InWindow(0u, 0xfffffffeu, 1u));
    EXPECT_TRUE(IsSerialU32InWindow(1u, 0xfffffffeu, 1u));
    EXPECT_FALSE(IsSerialU32InWindow(2u, 0xfffffffeu, 1u));
    EXPECT_FALSE(IsSerialU32InWindow(0xfffffffdu, 0xfffffffeu, 1u));
    EXPECT_TRUE(IsSerialU32InWindow(0x7fffffffu, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0x80000000u, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0x80000001u, 0u, 0x7fffffffu));
    EXPECT_FALSE(IsSerialU32InWindow(0u, 0u, 0x80000000u));
    EXPECT_EQ(ComputeSobol2U32(0u), 0u);
    EXPECT_EQ(ComputeSobol2U32(1u), 0x8000000080000000ull);
    EXPECT_EQ(ComputeSobol2U32(2u), 0xc000000040000000ull);
    EXPECT_EQ(ComputeSobol2U32(3u), 0x40000000c0000000ull);
    EXPECT_EQ(DivideFloorI64(Limit<i64>::s_Min, 1), Limit<i64>::s_Min);
    EXPECT_EQ(DivideFloorI64(-3, 2), -2);
    EXPECT_EQ(DivideCeilI64(-3, 2), -1);
    EXPECT_EQ(DivideCeilI64(Limit<i64>::s_Max, 2), Limit<i64>::s_Max / 2 + 1);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(0x1p-150, false)), 0u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(0x1p-150, true)), 1u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0x1p-150, false)), 0x80000001u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0x1p-150, true)), 0x80000000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(-0.0, false)), 0x80000000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(1.0 + 0x1p-24, false)), 0x3f800000u);
    EXPECT_EQ(BitCast<u32>(RoundF64ToF32Bound(1.0 + 0x1p-24, true)), 0x3f800001u);
    EXPECT_EQ(ClassifyIntegerGrammar("-"), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyIntegerGrammar("--1"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyIntegerGrammar(" 1"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar("."), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyFloatGrammar("1e+"), s_NumericGrammarIncomplete);
    EXPECT_EQ(ClassifyFloatGrammar("1e+x"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar(".e"), s_NumericGrammarInvalid);
    EXPECT_EQ(ClassifyFloatGrammar("1."), s_NumericGrammarComplete);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


