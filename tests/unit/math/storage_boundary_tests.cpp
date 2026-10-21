// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/array.h>
#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#include <global/limit.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_math_storage_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value>
static void CheckStorageHashMatchesEquality(const Value& lhs, const Value& same, const Value& different){
    EXPECT_EQ(lhs, same);
    EXPECT_NE(lhs, different);
    EXPECT_EQ(Hasher<Value>{}(lhs), Hasher<Value>{}(same));
}

TEST(Math, SignedZeroStorageEqualityProducesEqualHashes){
    EXPECT_EQ(FloatingHashBits(-0.0f), FloatingHashBits(0.0f));
    CheckStorageHashMatchesEquality(Float4(-0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 3.0f), Float4(0.0f, 1.0f, 2.0f, 4.0f));
    CheckStorageHashMatchesEquality(Float3Int(-0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -3), Float3Int(0.0f, 1.0f, 2.0f, -4));
    CheckStorageHashMatchesEquality(Float3UInt(-0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 3u), Float3UInt(0.0f, 1.0f, 2.0f, 4u));
    CheckStorageHashMatchesEquality(Float2U(-0.0f, 1.0f), Float2U(0.0f, 1.0f), Float2U(0.0f, 2.0f));
    CheckStorageHashMatchesEquality(Float3U(-0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 2.0f), Float3U(0.0f, 1.0f, 3.0f));
    CheckStorageHashMatchesEquality(Float4U(-0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 3.0f), Float4U(0.0f, 1.0f, 2.0f, 4.0f));
}

template<typename Storage, usize LogicalCount, typename Source>
static void CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding(const Source& source, const f64 (&expected)[LogicalCount]){
    struct StorageRecord{
        Storage value;
        u64 neighbor[4];
    };
    StorageRecord record{};
    NWB_MEMSET(&record, 0xa5, sizeof(record));
    const u64 poison[]{ 0x7ff0000000000000u, 0x7ff8000000001234u, 0xfff0000000000000u };
    const usize logicalBytes = LogicalCount * sizeof(f64);
    NWB_MEMCPY(reinterpret_cast<u8*>(&record.value) + logicalBytes, sizeof(Storage) - logicalBytes, poison, sizeof(Storage) - logicalBytes);
    Array<u8, sizeof(StorageRecord)> original{};
    NWB_MEMCPY(original.data(), original.size(), &record, sizeof(record));
    for(const bool stream : { false, true }){
        NWB_MEMCPY(&record, sizeof(record), original.data(), original.size());
        if(stream){
            StreamDouble(source, record.value);
            StreamDoubleFence();
        }
        else
            StoreDouble(source, record.value);
        const auto bytes = MakeNotNull(reinterpret_cast<const u8*>(&record));
        for(usize index = logicalBytes; index < sizeof(record); ++index)
            EXPECT_EQ(bytes.get()[index], original[index]) << stream << ':' << index;
        for(usize lane = 0u; lane < LogicalCount; ++lane)
            EXPECT_EQ(BitCast<u64>(record.value.raw[lane]), BitCast<u64>(expected[lane])) << stream << ':' << lane;

        const auto loaded = LoadDouble(record.value);
        if constexpr(LogicalCount < 4u){
            for(usize lane = LogicalCount; lane < 4u; ++lane)
                EXPECT_EQ(BitCast<u64>(loaded[lane]), 0u) << stream << ':' << lane;
            const SIMDVectorDouble transformed = VectorDouble4Transform(loaded, MatrixDoubleIdentity());
            for(usize lane = 0u; lane < LogicalCount; ++lane)
                EXPECT_EQ(transformed[lane], expected[lane]) << stream << ':' << lane;
        }
        else{
            for(usize row = 0u; row < 3u; ++row){
                EXPECT_EQ(BitCast<u64>(loaded.v[row].w), 0u) << stream << ':' << row;
                for(usize lane = 0u; lane < 3u; ++lane)
                    EXPECT_EQ(BitCast<u64>(loaded.v[row][lane]), BitCast<u64>(expected[row * 3u + lane])) << stream << ':' << row << ':' << lane;
            }
            EXPECT_TRUE(VectorDouble4Equal(loaded.v[3], SIMDVectorDouble{ 0.0, 0.0, 0.0, 1.0 }));
        }
    }
}

template<typename Storage, usize LogicalCount>
static void CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero(){
    Storage lhs{};
    Storage rhs{};
    for(usize lane = 0u; lane < LogicalCount; ++lane){
        lhs.raw[lane] = lane == 0u ? -0.0 : 1.0 + 0x1p-52;
        rhs.raw[lane] = lane == 0u ? 0.0 : 1.0 + 0x1p-52;
    }
    const usize logicalBytes = LogicalCount * sizeof(f64);
    NWB_MEMSET(reinterpret_cast<u8*>(&lhs) + logicalBytes, 0x7f, sizeof(Storage) - logicalBytes);
    NWB_MEMSET(reinterpret_cast<u8*>(&rhs) + logicalBytes, 0xff, sizeof(Storage) - logicalBytes);
    EXPECT_EQ(lhs, rhs);
    EXPECT_EQ(Hasher<Storage>{}(lhs), Hasher<Storage>{}(rhs));
    rhs.raw[LogicalCount - 1u] = 1.0;
    EXPECT_NE(lhs, rhs);
}

TEST(Math, DoubleStorageHashesCanonicalizeSignedZeroAndRetainSubFloatResiduals){
    EXPECT_EQ(FloatingHashBits(-0.0), FloatingHashBits(0.0));
    EXPECT_NE(FloatingHashBits(1.0 + 0x1p-52), FloatingHashBits(1.0));
    CheckStorageHashMatchesEquality(Double4(-0.0, 1.0 + 0x1p-52, -0.0, 0x1p+40 + 1.0), Double4(0.0, 1.0 + 0x1p-52, 0.0, 0x1p+40 + 1.0), Double4(0.0, 1.0, 0.0, 0x1p+40 + 1.0));
    CheckStorageHashMatchesEquality(Double4U(-0.0, 1.0 + 0x1p-52, -0.0, 0x1p+40 + 1.0), Double4U(0.0, 1.0 + 0x1p-52, 0.0, 0x1p+40 + 1.0), Double4U(0.0, 1.0, 0.0, 0x1p+40 + 1.0));
}

TEST(Math, AlignedShortDoubleStoresAndStreamsPreservePoisonedPaddingAndNeighborStorage){
    const f64 pairExpected[]{ -0.0, 1.0 + 0x1p-52 };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double2, 2u>(SIMDVectorDouble{ pairExpected[0], pairExpected[1], Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN }, pairExpected);
    const f64 tripleExpected[]{ -0.0, 1.0 + 0x1p-52, 0x1p+40 + 1.0 };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double3, 3u>(SIMDVectorDouble{ tripleExpected[0], tripleExpected[1], tripleExpected[2], Limit<f64>::s_QuietNaN }, tripleExpected);
    const f64 matrixExpected[]{ -0.0, 1.0 + 0x1p-52, 0x1p+40 + 1.0, 2.0, -0.0, -0x1p+40 - 1.0, 0x1p-40, -3.0, 1.0 + 0x1p-52 };
    const SIMDMatrixDouble matrix = {
        SIMDVectorDouble{ matrixExpected[0], matrixExpected[1], matrixExpected[2], Limit<f64>::s_Infinity },
        SIMDVectorDouble{ matrixExpected[3], matrixExpected[4], matrixExpected[5], Limit<f64>::s_QuietNaN },
        SIMDVectorDouble{ matrixExpected[6], matrixExpected[7], matrixExpected[8], -Limit<f64>::s_Infinity },
        SIMDVectorDouble{ Limit<f64>::s_QuietNaN, Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN },
    };
    CheckDoubleStoresIgnoreInactiveLanesAndPreserveObjectPadding<Double33, 9u>(matrix, matrixExpected);
}

TEST(Math, AlignedShortDoubleAndCompactMatrixHashesIgnorePaddingWithSignedZeroAndBinary64Residuals){
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double2, 2u>();
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double3, 3u>();
    CheckDoubleStorageHashIgnoresPaddingAndCanonicalizesSignedZero<Double33, 9u>();
}

TEST(Math, DoubleTransformStreamsPreservePaddedStridesDuringAlignedUnalignedAndInPlaceConversion){
    struct AlignedRecord{
        Double3 value;
        u64 gap[4];
    };
    struct alignas(32) UnalignedRecord{
        u64 prefix;
        Double3U value;
        u64 gap[4];
    };
    AlignedRecord aligned[3]{};
    UnalignedRecord unaligned[3]{};
    NWB_MEMSET(aligned, 0xa5, sizeof(aligned));
    NWB_MEMSET(unaligned, 0x5a, sizeof(unaligned));
    const u64 poison = 0x7ff8000000001234u;
    for(usize index = 0u; index < LengthOf(aligned); ++index){
        const SIMDVectorDouble position = VectorDoubleReplicate(static_cast<f64>(index));
        const SIMDVectorDouble x = VectorDoubleMultiplyAddExpression(VectorDoubleReplicate(0.25), position, VectorDoubleReplicate(0x1p+40 + 0.25));
        const SIMDVectorDouble previousPosition = VectorDoubleSubtract(position, VectorDoubleSplatOne());
        const SIMDVectorDouble y = VectorDoubleMultiply(previousPosition, VectorDoubleReplicate(0x1p-40));
        StoreDouble(VectorDoubleMergeX(x, y, VectorDoubleReplicate(1.0 + 0x1p-52), VectorDoubleReplicate(Limit<f64>::s_Infinity)), aligned[index].value);
        NWB_MEMCPY(reinterpret_cast<u8*>(&aligned[index].value) + 3u * sizeof(f64), sizeof(aligned[index].value) - 3u * sizeof(f64), &poison, sizeof(poison));
    }
    Array<u8, sizeof(aligned)> alignedOriginal{};
    Array<u8, sizeof(unaligned)> unalignedOriginal{};
    NWB_MEMCPY(alignedOriginal.data(), alignedOriginal.size(), aligned, sizeof(aligned));
    NWB_MEMCPY(unalignedOriginal.data(), unalignedOriginal.size(), unaligned, sizeof(unaligned));
    const SIMDMatrixDouble matrix = MatrixDoubleTranslation(0.25, 0.0, 0.0);
    EXPECT_EQ(VectorDouble3TransformCoordStream(MakeNotNull(&unaligned[0].value), sizeof(UnalignedRecord), MakeNotNull<const Double3*>(&aligned[0].value), sizeof(AlignedRecord), LengthOf(aligned), matrix).get(), &unaligned[0].value);
    EXPECT_EQ(NWB_MEMCMP(aligned, alignedOriginal.data(), sizeof(aligned)), 0);
    for(usize index = 0u; index < LengthOf(aligned); ++index)
        EXPECT_EQ(unaligned[index].value.x, 0x1p+40 + 0.5 + 0.25 * static_cast<f64>(index)) << index;

    EXPECT_EQ(VectorDouble3TransformCoordStream(MakeNotNull(&unaligned[0].value), sizeof(UnalignedRecord), MakeNotNull<const Double3U*>(&unaligned[0].value), sizeof(UnalignedRecord), LengthOf(unaligned), matrix).get(), &unaligned[0].value);
    EXPECT_EQ(VectorDouble3TransformCoordStream(MakeNotNull(&aligned[0].value), sizeof(AlignedRecord), MakeNotNull<const Double3U*>(&unaligned[0].value), sizeof(UnalignedRecord), LengthOf(aligned), matrix).get(), &aligned[0].value);
    EXPECT_EQ(VectorDouble3TransformCoordStream(MakeNotNull(&aligned[0].value), sizeof(AlignedRecord), MakeNotNull<const Double3*>(&aligned[0].value), sizeof(AlignedRecord), LengthOf(aligned), matrix).get(), &aligned[0].value);
    for(usize index = 0u; index < LengthOf(aligned); ++index){
        EXPECT_EQ(aligned[index].value.x, 0x1p+40 + 1.25 + 0.25 * static_cast<f64>(index)) << index;
        EXPECT_EQ(unaligned[index].value.x, 0x1p+40 + 0.75 + 0.25 * static_cast<f64>(index)) << index;
        EXPECT_EQ(aligned[index].value.y, (static_cast<f64>(index) - 1.0) * 0x1p-40) << index;
        EXPECT_EQ(aligned[index].value.z, 1.0 + 0x1p-52) << index;
        const auto alignedBytes = MakeNotNull(reinterpret_cast<const u8*>(&aligned[index]));
        const auto unalignedBytes = MakeNotNull(reinterpret_cast<const u8*>(&unaligned[index]));
        for(usize offset = 3u * sizeof(f64); offset < sizeof(AlignedRecord); ++offset)
            EXPECT_EQ(alignedBytes.get()[offset], alignedOriginal[index * sizeof(AlignedRecord) + offset]) << index << ':' << offset;
        for(usize offset = 0u; offset < sizeof(UnalignedRecord); ++offset){
            if(offset >= offsetof(UnalignedRecord, value) && offset < offsetof(UnalignedRecord, value) + sizeof(Double3U))
                continue;
            EXPECT_EQ(unalignedBytes.get()[offset], unalignedOriginal[index * sizeof(UnalignedRecord) + offset]) << index << ':' << offset;
        }
    }

    BoundingDoubleBox bounds;
    BoundingDoubleBox::CreateFromPoints(bounds, LengthOf(aligned), aligned[0].value, sizeof(AlignedRecord));
    EXPECT_EQ(bounds.center.x, 0x1p+40 + 1.5);
    EXPECT_EQ(bounds.center.y, 0.0);
    EXPECT_EQ(bounds.center.z, 1.0 + 0x1p-52);
    EXPECT_EQ(bounds.extents.x, 0.25);
    EXPECT_EQ(bounds.extents.y, 0x1p-40);
    EXPECT_EQ(bounds.extents.z, 0.0);
    EXPECT_EQ(bounds.contains(SIMDVectorDouble{ 0x1p+40 + 1.75 + 0x1p-10, 0.0, 1.0 + 0x1p-52, 0.0 }), ContainmentType::Disjoint);

    struct PairRecord{
        Double2 value;
        u64 gap[4];
    };
    PairRecord pairs[2]{};
    NWB_MEMSET(pairs, 0xa5, sizeof(pairs));
    for(usize index = 0u; index < LengthOf(pairs); ++index){
        StoreDouble(SIMDVectorDouble{ 0x1p+40 + 0.25, 1.0 + 0x1p-52, Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN }, pairs[index].value);
        NWB_MEMCPY(reinterpret_cast<u8*>(&pairs[index].value) + 2u * sizeof(f64), sizeof(pairs[index].value) - 2u * sizeof(f64), &poison, sizeof(poison));
    }
    Array<u8, sizeof(pairs)> pairOriginal{};
    NWB_MEMCPY(pairOriginal.data(), pairOriginal.size(), pairs, sizeof(pairs));
    EXPECT_EQ(VectorDouble2TransformCoordStream(MakeNotNull(&pairs[0].value), sizeof(PairRecord), MakeNotNull<const Double2*>(&pairs[0].value), sizeof(PairRecord), LengthOf(pairs), matrix).get(), &pairs[0].value);
    for(usize index = 0u; index < LengthOf(pairs); ++index){
        EXPECT_EQ(pairs[index].value.x, 0x1p+40 + 0.5) << index;
        EXPECT_EQ(pairs[index].value.y, 1.0 + 0x1p-52) << index;
        const auto bytes = MakeNotNull(reinterpret_cast<const u8*>(&pairs[index]));
        for(usize offset = 2u * sizeof(f64); offset < sizeof(PairRecord); ++offset)
            EXPECT_EQ(bytes.get()[offset], pairOriginal[index * sizeof(PairRecord) + offset]) << index << ':' << offset;
    }
}

TEST(Math, DoubleMinMaxRetainRightOperandNaNPayloadAndZeroSign){
    const u64 firstBits[]{ 0xfff8000012345678u, 0x8000000000000000u, 0u, 0x3ff8000000000000u };
    const u64 secondBits[]{ 0x7ff800009abcdef0u, 0u, 0x8000000000000000u, 0x7ff8000013579bdfu };
    const SIMDVectorDouble first = VectorDoubleSetInt(firstBits[0], firstBits[1], firstBits[2], firstBits[3]);
    const SIMDVectorDouble second = VectorDoubleSetInt(secondBits[0], secondBits[1], secondBits[2], secondBits[3]);
    const SIMDVectorDouble forward[]{ VectorDoubleMin(first, second), VectorDoubleMax(first, second) };
    const SIMDVectorDouble reverse[]{ VectorDoubleMin(second, first), VectorDoubleMax(second, first) };
    for(usize operation = 0u; operation < LengthOf(forward); ++operation){
        for(usize lane = 0u; lane < 4u; ++lane){
            EXPECT_EQ(VectorDoubleGetIntByIndex(forward[operation], lane), secondBits[lane]) << operation << ':' << lane;
            EXPECT_EQ(VectorDoubleGetIntByIndex(reverse[operation], lane), firstBits[lane]) << operation << ':' << lane;
        }
    }
}

TEST(Math, DoubleRawIntegerEqualityRequiresBoth32BitHalvesOfEveryLane){
    const u64 firstBits[]{ 0x0123456789abcdefu, 0x8000000000000000u, 0x7ff800009abcdef0u, 0xfedcba9876543210u };
    const u64 differences[]{ 0x0000000000000001u, 0x0000000100000000u };
    const SIMDVectorDouble first = VectorDoubleSetInt(firstBits[0], firstBits[1], firstBits[2], firstBits[3]);
    for(usize changedLane = 0u; changedLane < 4u; ++changedLane){
        for(const u64 difference : differences){
            const SIMDVectorDouble second = VectorDoubleSetIntByIndex(first, firstBits[changedLane] ^ difference, changedLane);
            const SIMDVectorDouble equality[]{ VectorDoubleEqualInt(first, second), VectorDoubleEqualInt(second, first) };
            for(const SIMDVectorDouble mask : equality){
                for(usize lane = 0u; lane < 4u; ++lane)
                    EXPECT_EQ(VectorDoubleGetIntByIndex(mask, lane), lane == changedLane ? 0u : Limit<u64>::s_Max) << changedLane << ':' << difference << ':' << lane;
            }
        }
    }
}

TEST(Math, DoubleSingleConversionsRetainSubnormalsAndSignsAndRoundHalfUlpTiesEven){
    volatile f64 runtimeInputs[][4]{
        { Limit<f64>::s_Max, -Limit<f64>::s_Max, -0.0, 0.0 },
        { 1.0 + 0x1p-24, 1.0 + 0x1.8p-23, -1.0 - 0x1p-24, -1.0 - 0x1.8p-23 },
        { 0x1p-150, 0x1.8p-149, -0x1p-150, -0x1.8p-149 },
    };
    const SIMDVectorSingle overflow = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[0][0], runtimeInputs[0][1], runtimeInputs[0][2], runtimeInputs[0][3]));
    const u32 overflowExpected[]{ 0x7f800000u, 0xff800000u, 0x80000000u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(overflow, lane), overflowExpected[lane]) << lane;

    const u32 subnormalBits[]{ 1u, 0x80000001u, 0x007fffffu, 0x807fffffu };
    volatile u32 runtimeSubnormalBits[]{ 1u, 0x80000001u, 0x007fffffu, 0x807fffffu };
    const SIMDVectorSingle subnormals = VectorSetInt(runtimeSubnormalBits[0], runtimeSubnormalBits[1], runtimeSubnormalBits[2], runtimeSubnormalBits[3]);
    const SIMDVectorDouble widened = ConvertVectorSingleToDouble(subnormals);
    const f64 widenedExpected[]{ 0x1p-149, -0x1p-149, 0x1.fffffcp-127, -0x1.fffffcp-127 };
    const SIMDVectorSingle narrowed = ConvertVectorDoubleToSingle(widened);
    for(usize lane = 0u; lane < 4u; ++lane){
        EXPECT_EQ(VectorDoubleGetIntByIndex(widened, lane), BitCast<u64>(widenedExpected[lane])) << lane;
        EXPECT_EQ(VectorGetIntByIndex(narrowed, lane), subnormalBits[lane]) << lane;
    }

    volatile u32 runtimeZeroBits[]{ 0x80000000u, 0u, 0x80000000u, 0u };
    const SIMDVectorDouble signedZeros = ConvertVectorSingleToDouble(VectorSetInt(runtimeZeroBits[0], runtimeZeroBits[1], runtimeZeroBits[2], runtimeZeroBits[3]));
    const u64 zeroExpected[]{ 0x8000000000000000u, 0u, 0x8000000000000000u, 0u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorDoubleGetIntByIndex(signedZeros, lane), zeroExpected[lane]) << lane;

    const SIMDVectorSingle rounded = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[1][0], runtimeInputs[1][1], runtimeInputs[1][2], runtimeInputs[1][3]));
    const u32 roundedExpected[]{ 0x3f800000u, 0x3f800002u, 0xbf800000u, 0xbf800002u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(rounded, lane), roundedExpected[lane]) << lane;

    const SIMDVectorSingle underflow = ConvertVectorDoubleToSingle(VectorDoubleSet(runtimeInputs[2][0], runtimeInputs[2][1], runtimeInputs[2][2], runtimeInputs[2][3]));
    const u32 underflowExpected[]{ 0u, 2u, 0x80000000u, 0x80000002u };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(underflow, lane), underflowExpected[lane]) << lane;
}

TEST(Math, SingleRuntimePermutationsPreserveNaNPayloadsSignedZeroAndSubnormalBits){
    const u32 bits[]{ 0xffc01234u, 0x80000000u, 1u, 0x7f800001u, 0u, 0x7fc05678u, 0x807fffffu, Limit<u32>::s_Max };
    const SIMDVectorSingle first = VectorSetInt(bits[0], bits[1], bits[2], bits[3]);
    const SIMDVectorSingle second = VectorSetInt(bits[4], bits[5], bits[6], bits[7]);
    for(u32 code = 0u; code < 256u; ++code){
        volatile u32 controls[]{ code & 3u, (code >> 2u) & 3u, (code >> 4u) & 3u, (code >> 6u) & 3u };
        const SIMDVectorSingle result = VectorSwizzle(first, controls[0], controls[1], controls[2], controls[3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorGetIntByIndex(result, lane), bits[controls[lane]]) << code << ':' << lane;
    }
    for(u32 code = 0u; code < 4096u; ++code){
        volatile u32 controls[]{ code & 7u, (code >> 3u) & 7u, (code >> 6u) & 7u, (code >> 9u) & 7u };
        const SIMDVectorSingle result = VectorPermute(first, second, controls[0], controls[1], controls[2], controls[3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorGetIntByIndex(result, lane), bits[controls[lane]]) << code << ':' << lane;
    }
}

TEST(Math, UnalignedFloat33LoadRetainsNegativeZeroAndSignalingNaNPayloadWithoutInvalidExceptions){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    }
    );

    struct alignas(16) MatrixRecord{
        u32 prefix{};
        Float33U value{};
    } record;
    volatile u32 runtimeBits[]{ 0x80000000u, 1u, 0x7fc01234u, 0x80000000u, 0x7f800001u, 0x807fffffu, 0xff800000u, 0u, 0xffc05678u };
    for(usize lane = 0u; lane < 9u; ++lane)
        record.value.raw[lane] = BitCast<f32>(runtimeBits[lane]);
    ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
    const SIMDMatrix matrix = LoadFloat(record.value);
    const i32 invalid = TestFloatingPointExceptions(FE_INVALID);
    EXPECT_EQ(invalid, 0);
    for(usize row = 0u; row < 3u; ++row){
        for(usize lane = 0u; lane < 3u; ++lane)
            EXPECT_EQ(VectorGetIntByIndex(matrix.v[row], lane), runtimeBits[row * 3u + lane]) << row << ':' << lane;
        EXPECT_EQ(VectorGetIntW(matrix.v[row]), 0u) << row;
    }
    const u32 lastRow[]{ 0u, 0u, 0u, BitCast<u32>(1.0f) };
    for(usize lane = 0u; lane < 4u; ++lane)
        EXPECT_EQ(VectorGetIntByIndex(matrix.v[3], lane), lastRow[lane]) << lane;
}

TEST(Math, SelectionControlTreatsHighBitUnsignedValuesAsTrueAndPreservesSelectedRawBits){
    const u32 choices[]{ 0u, 1u, 0x80000000u, Limit<u32>::s_Max };
    const u32 left[]{ 0x80000000u, 1u, 0x7f800001u, 0xffc01234u };
    const u32 right[]{ 0u, 0x807fffffu, 0x7fc05678u, Limit<u32>::s_Max };
    const SIMDVectorSingle first = VectorSetInt(left[0], left[1], left[2], left[3]);
    const SIMDVectorSingle second = VectorSetInt(right[0], right[1], right[2], right[3]);
    for(u32 code = 0u; code < 256u; ++code){
        volatile u32 controls[]{ choices[code & 3u], choices[(code >> 2u) & 3u], choices[(code >> 4u) & 3u], choices[(code >> 6u) & 3u] };
        const SIMDVectorSingle control = VectorSelectControl(controls[0], controls[1], controls[2], controls[3]);
        const SIMDVectorSingle selected = VectorSelect(first, second, control);
        const SIMDVectorDouble doubleControl = VectorDoubleSelectControl(controls[0], controls[1], controls[2], controls[3]);
        for(usize lane = 0u; lane < 4u; ++lane){
            const bool enabled = controls[lane] != 0u;
            EXPECT_EQ(VectorGetIntByIndex(control, lane), enabled ? Limit<u32>::s_Max : 0u) << code << ':' << lane;
            EXPECT_EQ(VectorGetIntByIndex(selected, lane), enabled ? right[lane] : left[lane]) << code << ':' << lane;
            EXPECT_EQ(VectorDoubleGetIntByIndex(doubleControl, lane), enabled ? Limit<u64>::s_Max : 0u) << code << ':' << lane;
        }
    }
}

TEST(Math, DoubleRuntimePermutationsRetainNonfiniteAndRawBitsAcrossPairs){
    const SIMDVectorDouble first = VectorDoubleSetInt(0xfff8000012345678u, 0x8000000000000000u, 0x0123456789abcdefu, 0x7ff0000000000001u);
    const SIMDVectorDouble second = VectorDoubleSetInt(0u, 0x7ff800009abcdef0u, 0xfedcba9876543210u, Limit<u64>::s_Max);
    volatile u32 swizzleControls[][4]{ { 3u, 1u, 0u, 2u }, { 2u, 0u, 3u, 1u }, { 1u, 3u, 1u, 3u } };
    const u64 swizzleExpected[][4]{
        { 0x7ff0000000000001u, 0x8000000000000000u, 0xfff8000012345678u, 0x0123456789abcdefu },
        { 0x0123456789abcdefu, 0xfff8000012345678u, 0x7ff0000000000001u, 0x8000000000000000u },
        { 0x8000000000000000u, 0x7ff0000000000001u, 0x8000000000000000u, 0x7ff0000000000001u }
    };
    for(usize index = 0u; index < LengthOf(swizzleExpected); ++index){
        const SIMDVectorDouble result = VectorDoubleSwizzle(first, swizzleControls[index][0], swizzleControls[index][1], swizzleControls[index][2], swizzleControls[index][3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorDoubleGetIntByIndex(result, lane), swizzleExpected[index][lane]) << index << ':' << lane;
    }

    volatile u32 permuteControls[][4]{ { 7u, 0u, 6u, 1u }, { 3u, 4u, 2u, 5u }, { 5u, 3u, 4u, 0u } };
    const u64 permuteExpected[][4]{
        { Limit<u64>::s_Max, 0xfff8000012345678u, 0xfedcba9876543210u, 0x8000000000000000u },
        { 0x7ff0000000000001u, 0u, 0x0123456789abcdefu, 0x7ff800009abcdef0u },
        { 0x7ff800009abcdef0u, 0x7ff0000000000001u, 0u, 0xfff8000012345678u }
    };
    for(usize index = 0u; index < LengthOf(permuteExpected); ++index){
        const SIMDVectorDouble result = VectorDoublePermute(first, second, permuteControls[index][0], permuteControls[index][1], permuteControls[index][2], permuteControls[index][3]);
        for(usize lane = 0u; lane < 4u; ++lane)
            EXPECT_EQ(VectorDoubleGetIntByIndex(result, lane), permuteExpected[index][lane]) << index << ':' << lane;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

