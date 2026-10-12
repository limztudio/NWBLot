// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simd/type.h>
#include <global/limit.h>
#include <gtest/gtest.h>

#include <cfenv>
#include <new>

#if defined(__SSE2__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#pragma STDC FENV_ACCESS ON


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_storage_equality_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Element, usize Count>
struct RawEqualityStorage{
    Element raw[Count]{};

    friend bool operator==(const RawEqualityStorage& lhs, const RawEqualityStorage& rhs)noexcept{
        return MathTypeDetail::EqualArray<RawEqualityStorage, Count>(lhs, rhs);
    }
    friend bool operator!=(const RawEqualityStorage& lhs, const RawEqualityStorage& rhs)noexcept{ return !(lhs == rhs); }
};

template<typename Value, usize Count>
void CheckArrayEquality(){
    using Element = RemoveCVRef_T<decltype(Value{}.raw[0])>;
    Value lhs{}, rhs{};
    for(usize lane = 0u; lane < Count; ++lane){
        lhs.raw[lane] = static_cast<Element>(lane + 1u);
        rhs.raw[lane] = lhs.raw[lane];
    }
    EXPECT_TRUE(lhs == rhs);
    EXPECT_FALSE(lhs != rhs);
    for(usize lane = 0u; lane < Count; ++lane){
        rhs.raw[lane] = static_cast<Element>(lhs.raw[lane] + 1);
        EXPECT_FALSE(lhs == rhs) << lane;
        EXPECT_TRUE(lhs != rhs) << lane;
        rhs.raw[lane] = lhs.raw[lane];
    }
}

template<typename Value, usize Count>
NWB_NOINLINE bool LegacyEqual(const Value& lhs, const Value& rhs)noexcept{
    for(usize lane = 0u; lane < Count; ++lane){
        if(lhs.raw[lane] != rhs.raw[lane])
            return false;
    }
    return true;
}

// Compare the scalar reference under its original default floating-point environment.
#pragma STDC FENV_ACCESS OFF
template<typename Value, usize Count>
NWB_NOINLINE bool DefaultContextLegacyEqual(const Value& lhs, const Value& rhs)noexcept{
    for(usize i = 0u; i < Count; ++i){
        if(lhs.raw[i] != rhs.raw[i])
            return false;
    }
    return true;
}
#pragma STDC FENV_ACCESS ON

template<typename Value>
NWB_NOINLINE bool StorageEqual(const Value& lhs, const Value& rhs)noexcept{ return lhs == rhs; }

template<typename Value, usize Count>
void CheckNaNComparisonFlags(){
    std::fenv_t original;
    ASSERT_EQ(std::fegetenv(&original), 0);
    for(const u32 nanBits : { 0x7FC12345u, 0x7F812345u }){
        for(usize nanLane = 0u; nanLane < Count; ++nanLane){
            for(usize mismatch = 0u; mismatch <= Count; ++mismatch){
                Value lhs{}, rhs{};
                for(usize lane = 0u; lane < Count; ++lane)
                    lhs.raw[lane] = rhs.raw[lane] = static_cast<f32>(lane + 1u);
                lhs.raw[nanLane] = rhs.raw[nanLane] = BitCast<f32>(nanBits);
                if(mismatch < Count && mismatch != nanLane)
                    rhs.raw[mismatch] += 1.0f;
                std::feclearexcept(FE_ALL_EXCEPT);
                const bool expected = LegacyEqual<Value, Count>(lhs, rhs);
                const int expectedFlags = std::fetestexcept(FE_ALL_EXCEPT);
                std::feclearexcept(FE_ALL_EXCEPT);
                const bool actual = StorageEqual(lhs, rhs);
                const int actualFlags = std::fetestexcept(FE_ALL_EXCEPT);
                EXPECT_EQ(actual, expected) << nanLane << ", " << mismatch;
                EXPECT_EQ(actualFlags, expectedFlags) << nanLane << ", " << mismatch;
            }
        }
    }
    EXPECT_EQ(std::fesetenv(&original), 0);
}

#if defined(__SSE2__) || defined(_M_X64)
struct EqualityEnvironmentGuard{
    std::fenv_t original{};
    unsigned originalMxcsr = _mm_getcsr();
    bool captured = std::fegetenv(&original) == 0;

    ~EqualityEnvironmentGuard(){
        if(captured){
            std::fesetenv(&original);
            _mm_setcsr(originalMxcsr);
        }
    }
};

struct EqualityObservation{
    bool equal;
    int flags;
    unsigned rawFlags;
};

template<typename Value, typename Compare>
EqualityObservation ObserveEquality(const Value& lhs, const Value& rhs, Compare compare){
    std::feclearexcept(FE_ALL_EXCEPT);
    _mm_setcsr(_mm_getcsr() & ~0x3Fu);
    const bool equal = compare(lhs, rhs);
    return { equal, std::fetestexcept(FE_ALL_EXCEPT), _mm_getcsr() & 0x3Fu };
}

template<typename Value, usize Count>
void CheckRawEqualityComparisonFlags(){
    EqualityEnvironmentGuard restore;
    ASSERT_TRUE(restore.captured);
    std::fenv_t held;
    ASSERT_EQ(std::feholdexcept(&held), 0);
    const unsigned heldMxcsr = _mm_getcsr();
    for(const unsigned mode : { 0u, 0x40u, 0x8000u, 0x8040u }){
        _mm_setcsr((heldMxcsr & ~(0x8040u | 0x3Fu)) | mode);
        for(const u32 bits : { 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu, 0x7FC12345u, 0x7F812345u }){
            for(usize exceptionalLane = 0u; exceptionalLane < Count; ++exceptionalLane){
                for(usize mismatch = 0u; mismatch <= Count; ++mismatch){
                    for(const bool compareWithZero : { false, true }){
                        Value lhs{}, rhs{};
                        for(usize lane = 0u; lane < Count; ++lane)
                            lhs.raw[lane] = rhs.raw[lane] = static_cast<f32>(lane + 1u);
                        lhs.raw[exceptionalLane] = BitCast<f32>(bits);
                        rhs.raw[exceptionalLane] = compareWithZero ? 0.0f : BitCast<f32>(bits);
                        if(mismatch < Count && mismatch != exceptionalLane)
                            rhs.raw[mismatch] += 1.0f;
                        const auto strict = ObserveEquality(lhs, rhs, LegacyEqual<Value, Count>);
                        const auto actual = ObserveEquality(lhs, rhs, StorageEqual<Value>);
                        EXPECT_EQ(actual.equal, strict.equal) << mode << ", " << exceptionalLane << ", " << mismatch;
                        EXPECT_EQ(actual.flags, strict.flags) << mode << ", " << exceptionalLane << ", " << mismatch;
                        EXPECT_EQ(actual.rawFlags, strict.rawFlags) << mode << ", " << exceptionalLane << ", " << mismatch;
                        const auto defaultContext = ObserveEquality(lhs, rhs, DefaultContextLegacyEqual<Value, Count>);
                        const auto actualDefault = ObserveEquality(lhs, rhs, StorageEqual<Value>);
                        EXPECT_EQ(actualDefault.equal, defaultContext.equal) << mode << ", " << exceptionalLane << ", " << mismatch;
                        EXPECT_EQ(actualDefault.flags, defaultContext.flags) << mode << ", " << exceptionalLane << ", " << mismatch;
                        EXPECT_EQ(actualDefault.rawFlags, defaultContext.rawFlags) << mode << ", " << exceptionalLane << ", " << mismatch;
                    }
                }
            }
        }
    }
}
#endif

#if defined(__linux__)
template<typename Value, usize Count>
void CheckPageBoundary(){
    const long pageSize = sysconf(_SC_PAGESIZE);
    ASSERT_GT(pageSize, 0);
    void* const mapping = mmap(nullptr, static_cast<usize>(pageSize) * 2u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(mapping, MAP_FAILED);
    const int protection = mprotect(static_cast<char*>(mapping) + pageSize, static_cast<usize>(pageSize), PROT_NONE);
    if(protection != 0){
        munmap(mapping, static_cast<usize>(pageSize) * 2u);
        FAIL() << "Unable to protect equality boundary page";
    }
    Value* const lhs = ::new(static_cast<char*>(mapping) + pageSize - sizeof(Value)) Value{};
    Value rhs{};
    for(usize lane = 0u; lane < Count; ++lane)
        lhs->raw[lane] = rhs.raw[lane] = static_cast<RemoveCVRef_T<decltype(rhs.raw[0])>>(lane + 1u);
    EXPECT_TRUE(StorageEqual(*lhs, rhs));
    if constexpr(IsSame_V<RemoveCVRef_T<decltype(rhs.raw[0])>, bool>)
        rhs.raw[Count - 1u] = !rhs.raw[Count - 1u];
    else
        rhs.raw[Count - 1u] += 1;
    EXPECT_FALSE(StorageEqual(*lhs, rhs));
    lhs->~Value();
    EXPECT_EQ(munmap(mapping, static_cast<usize>(pageSize) * 2u), 0);
}
#endif



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Math, StorageEqualityChecksEveryFloatIntegerAndHalfLane){
    using namespace __hidden_storage_equality_tests;
    CheckArrayEquality<Half2U, 2u>();
    CheckArrayEquality<Half4U, 4u>();
    CheckArrayEquality<Float4, 4u>();
    CheckArrayEquality<Float34, 12u>();
    CheckArrayEquality<Float44, 16u>();
    CheckArrayEquality<Float2U, 2u>();
    CheckArrayEquality<Float3U, 3u>();
    CheckArrayEquality<Float4U, 4u>();
    CheckArrayEquality<Float33U, 9u>();
    CheckArrayEquality<Float34U, 12u>();
    CheckArrayEquality<Float44U, 16u>();
    CheckArrayEquality<Int4, 4u>();
    CheckArrayEquality<Int2U, 2u>();
    CheckArrayEquality<Int3U, 3u>();
    CheckArrayEquality<Int4U, 4u>();
    CheckArrayEquality<UInt4, 4u>();
    CheckArrayEquality<UInt2U, 2u>();
    CheckArrayEquality<UInt3U, 3u>();
    CheckArrayEquality<UInt4U, 4u>();

    const Half4U halfBits(0x7C01u, 0xFC01u, 0x8000u, 0xFFFFu);
    EXPECT_TRUE(halfBits == halfBits);
    const Int4 signedBits{ .raw = { Limit<i32>::s_Min, Limit<i32>::s_Max, -1, 0 } };
    const UInt4 unsignedBits{ .raw = { Limit<u32>::s_Max, 0x80000000u, 0x7FFFFFFFu, 0u } };
    EXPECT_TRUE(signedBits == signedBits);
    EXPECT_TRUE(unsignedBits == unsignedBits);
}

TEST(Math, StorageEqualityUsesNumericFloatsAndIntegerMixedPayloads){
    const Float4U first(0.0f, -0.0f, Limit<f32>::s_Infinity, BitCast<f32>(u32{ 1u }));
    const Float4U second(-0.0f, 0.0f, Limit<f32>::s_Infinity, BitCast<f32>(u32{ 1u }));
    EXPECT_TRUE(first == second);
    EXPECT_FALSE(first != second);
    const Float4U nan(Limit<f32>::s_QuietNaN, 1.0f, 2.0f, 3.0f);
    EXPECT_FALSE(nan == nan);
    EXPECT_TRUE(nan != nan);

    const Float3Int mixedSigned(0.0f, -0.0f, 2.0f, Limit<i32>::s_Min);
    EXPECT_TRUE(mixedSigned == Float3Int(-0.0f, 0.0f, 2.0f, Limit<i32>::s_Min));
    EXPECT_FALSE(mixedSigned == Float3Int(0.0f, -0.0f, 2.0f, Limit<i32>::s_Min + 1));
    const Float3UInt mixedUnsigned(0.0f, -0.0f, 2.0f, 0x7F800001u);
    EXPECT_TRUE(mixedUnsigned == Float3UInt(-0.0f, 0.0f, 2.0f, 0x7F800001u));
    EXPECT_FALSE(mixedUnsigned == Float3UInt(0.0f, -0.0f, 2.0f, 0x7F800002u));
    EXPECT_FALSE(Float3UInt(1.0f, 2.0f, Limit<f32>::s_QuietNaN, 0u) == Float3UInt(1.0f, 2.0f, Limit<f32>::s_QuietNaN, 0u));
}

TEST(Math, StorageEqualityPreservesNaNShortCircuitExceptionFlags){
    using namespace __hidden_storage_equality_tests;
    CheckNaNComparisonFlags<Float4U, 4u>();
    CheckNaNComparisonFlags<Float33U, 9u>();
}

TEST(Math, StorageEqualityDoesNotReadBeyondUnalignedShortValues){
    using namespace __hidden_storage_equality_tests;
#if defined(__linux__)
    CheckPageBoundary<RawEqualityStorage<bool, 1u>, 1u>();
    CheckPageBoundary<RawEqualityStorage<bool, 16u>, 16u>();
    CheckPageBoundary<RawEqualityStorage<bool, 17u>, 17u>();
    CheckPageBoundary<RawEqualityStorage<bool, 33u>, 33u>();
    CheckPageBoundary<Half2U, 2u>();
    CheckPageBoundary<Half4U, 4u>();
    CheckPageBoundary<Float2U, 2u>();
    CheckPageBoundary<Float3U, 3u>();
    CheckPageBoundary<Float33U, 9u>();
    CheckPageBoundary<Int2U, 2u>();
    CheckPageBoundary<Int3U, 3u>();
    CheckPageBoundary<UInt2U, 2u>();
    CheckPageBoundary<UInt3U, 3u>();
#else
    GTEST_SKIP() << "Protected-page boundaries are verified on Linux";
#endif
}


TEST(Math, StorageEqualityFallsBackForBooleanArrays){
    using namespace __hidden_storage_equality_tests;
    const auto verify = []<usize Count>(){
        RawEqualityStorage<bool, Count> lhs{}, rhs{};
        for(usize lane = 0u; lane < Count; ++lane)
            lhs.raw[lane] = rhs.raw[lane] = (lane & 1u) != 0u;
        EXPECT_TRUE(StorageEqual(lhs, rhs));
        for(usize lane = 0u; lane < Count; ++lane){
            rhs.raw[lane] = !rhs.raw[lane];
            EXPECT_FALSE(StorageEqual(lhs, rhs)) << Count << ", " << lane;
            EXPECT_EQ(StorageEqual(lhs, rhs), (LegacyEqual<RawEqualityStorage<bool, Count>, Count>(lhs, rhs)));
            rhs.raw[lane] = lhs.raw[lane];
        }
        EXPECT_TRUE(StorageEqual(lhs, rhs));
    };
    verify.operator()<1u>();
    verify.operator()<2u>();
    verify.operator()<7u>();
    verify.operator()<16u>();
    verify.operator()<17u>();
    verify.operator()<33u>();
}

TEST(Math, StorageEqualityFallsBackForWideIntegralArrays){
    using namespace __hidden_storage_equality_tests;
#if defined(__SIZEOF_INT128__)
    using WideInteger = __int128;
    const WideInteger high = static_cast<WideInteger>(1) << 100u;
    const RawEqualityStorage<WideInteger, 3u> lhs{ { high, -high, high + 7 } };
    auto rhs = lhs;
    EXPECT_TRUE(StorageEqual(lhs, rhs));
    rhs.raw[1] += static_cast<WideInteger>(1) << 110u;
    EXPECT_FALSE(StorageEqual(lhs, rhs));
    EXPECT_EQ(StorageEqual(lhs, rhs), (LegacyEqual<RawEqualityStorage<WideInteger, 3u>, 3u>(lhs, rhs)));
    rhs = lhs;
    rhs.raw[2] += 1;
    EXPECT_FALSE(StorageEqual(lhs, rhs));
#else
    GTEST_SKIP() << "This target does not expose a wider integral type";
#endif
}


TEST(Math, StorageEqualityPreservesSubnormalNaNAndSkippedLaneMxcsrFlags){
    using namespace __hidden_storage_equality_tests;
#if defined(__SSE2__) || defined(_M_X64)
    CheckRawEqualityComparisonFlags<Float4U, 4u>();
    CheckRawEqualityComparisonFlags<Float33U, 9u>();
#else
    GTEST_SKIP() << "Raw MXCSR equality flags are specific to x86 SSE targets";
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

