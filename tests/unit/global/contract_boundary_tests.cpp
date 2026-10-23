// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/allocation_size.h>
#include <global/fixed_index_map.h>
#include <global/hash_utils.h>
#include <global/not_null.h>
#include <global/numeric_text.h>
#include <global/overflow.h>
#include <global/process_execution.h>
#include <global/refcount_ptr.h>
#include <global/utf8_text.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_contract_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ConversionFailure = 0xE301u;
constexpr u32 s_DeleterTag = 47u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
void VerifySignedLowerSaturation(){
    constexpr T s_Minimum = Limit<T>::s_Min;
    constexpr T s_Maximum = Limit<T>::s_Max;
    EXPECT_EQ(AddSaturating<T>(s_Minimum, T(-1)), s_Minimum);
    EXPECT_EQ(AddSaturating<T>(T(-1), s_Minimum), s_Minimum);
    EXPECT_EQ(AddSaturating<T>(s_Minimum, s_Minimum), s_Minimum);
    EXPECT_EQ(AddSaturating<T>(T(s_Minimum + T(1)), T(-1)), s_Minimum);
    EXPECT_EQ(AddSaturating<T>(s_Maximum, T(1)), s_Maximum);
}

struct ThrowingPointerConversion{
    int* value;


public:
    [[nodiscard]] operator int*()const{ throw s_ConversionFailure; }
};

struct NonthrowingPointerConversion{
    int* value;


public:
    [[nodiscard]] operator int*()const noexcept{ return value; }
};

struct PotentiallyThrowingPointerDestruction{
    int* value;


public:
    ~PotentiallyThrowingPointerDestruction()noexcept(false){}


public:
    [[nodiscard]] operator int*()const noexcept{ return value; }
};

struct ReferenceProbeBase{
    u32 referenceCount = 1u;
    u32& destroyedCount;


public:
    explicit ReferenceProbeBase(u32& count)noexcept
        : destroyedCount(count)
    {}
    virtual ~ReferenceProbeBase()noexcept{ ++destroyedCount; }


public:
    [[nodiscard]] u32 addReference()noexcept{ return ++referenceCount; }

    [[nodiscard]] u32 release()noexcept{ return --referenceCount; }
};

struct ReferenceProbeDerived final : ReferenceProbeBase{
    explicit ReferenceProbeDerived(u32& count)noexcept
        : ReferenceProbeBase(count)
    {}
    virtual ~ReferenceProbeDerived()noexcept override = default;
};

template<typename T>
struct StatefulValueDeleter{
    u32 tag;
    u32& deleteCount;


public:
    StatefulValueDeleter(const u32 value, u32& count)noexcept
        : tag(value)
        , deleteCount(count)
    {}
    template<typename U>
    StatefulValueDeleter(const StatefulValueDeleter<U>& other)noexcept
        : tag(other.tag)
        , deleteCount(other.deleteCount)
    {}


public:
    void operator()(T* value)const noexcept{
        ++deleteCount;
        delete value;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
[[noreturn]] void VerifyRedirectedFileWithClosedDescriptors(const u32 mask, const AStringView outputPath){
    NWB::Tests::TestArena<> testArena;
    for(int descriptor = 0; descriptor < 3; ++descriptor){
        if((mask & (1u << descriptor)) != 0u && ::close(descriptor) != 0)
            ::_exit(10);
    }

    constexpr AStringView s_Arguments[]{ "/bin/sh", "-c", "printf 'stdout\\n'; printf 'stderr\\n' >&2; exit 23" };
    const auto exitCode = RunProcessRedirectedToFile(testArena.arena, s_Arguments, outputPath);
    if(!exitCode || *exitCode != 23)
        ::_exit(11);
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, outputPath);
    AString<NWB::Core::Alloc::GlobalArena> output(testArena.arena);
    const auto read = ReadTextFile(path, output);
    if(!read || AStringView(output) != "stdout\nstderr\n")
        ::_exit(12);
    ::_exit(0);
}

[[noreturn]] void VerifyCapturedOutputWithClosedDescriptors(const u32 mask){
    NWB::Tests::TestArena<> testArena;
    for(int descriptor = 0; descriptor < 3; ++descriptor){
        if((mask & (1u << descriptor)) != 0u && ::close(descriptor) != 0)
            ::_exit(20);
    }

    AString<NWB::Core::Alloc::GlobalArena> output(testArena.arena);
    constexpr u32 s_CaptureTimeoutMS = 2000u;
    constexpr AStringView s_SuccessArguments[]{ "/bin/sh", "-c", "printf 'captured\\n'; printf 'discarded\\n' >&2; exit 0" };
    if(!CaptureProcessOutput(output, testArena.arena, s_SuccessArguments, 256u, 64u, s_CaptureTimeoutMS) || AStringView(output) != "captured\n")
        ::_exit(21);
    constexpr AStringView s_FailureArguments[]{ "/bin/sh", "-c", "printf 'retained\\n'; printf 'discarded\\n' >&2; exit 23" };
    if(CaptureProcessOutput(output, testArena.arena, s_FailureArguments, 256u, 64u, s_CaptureTimeoutMS) || AStringView(output) != "retained\n")
        ::_exit(22);
    ::_exit(0);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalContractBoundaries, SignedAdditionClampsNegativeOverflowToTheLowerBound){
    using namespace __hidden_contract_boundary_tests;
    VerifySignedLowerSaturation<i8>();
    VerifySignedLowerSaturation<i16>();
    VerifySignedLowerSaturation<i32>();
    VerifySignedLowerSaturation<i64>();
}

TEST(GlobalContractBoundaries, AlignmentOverflowKeepsTheLastRepresentableAlignedSize){
    constexpr usize s_Maximum = Limit<usize>::s_Max;
    constexpr usize s_LastAligned = s_Maximum & ~usize{ 7u };
    EXPECT_EQ(Alignment(8u, s_LastAligned), s_LastAligned);
    EXPECT_EQ(AlignmentPowerOfTwo(8u, s_LastAligned), s_LastAligned);
    EXPECT_THROW(Alignment(8u, s_LastAligned + 1u), AllocationSizeException);
    EXPECT_THROW(AlignmentPowerOfTwo(8u, s_LastAligned + 1u), AllocationSizeException);
    EXPECT_THROW(Alignment(10u, s_Maximum), AllocationSizeException);
    EXPECT_EQ(Alignment(0u, s_Maximum), s_Maximum);
    EXPECT_EQ(AlignmentPowerOfTwo(0u, s_Maximum), s_Maximum);
    EXPECT_FALSE(IsAligned(s_Maximum, usize{ 8u }));
    EXPECT_TRUE(IsAligned(s_LastAligned, usize{ 8u }));
}

TEST(GlobalContractBoundaries, CommonAlignmentRejectsZeroAndChecksOverflowAfterReducingSharedFactors){
    constexpr u32 s_HighAlignment = u32{ 1u } << 31u;
    const auto sharedFactor = CommonAlignmentChecked(s_HighAlignment, s_HighAlignment);
    ASSERT_TRUE(sharedFactor);
    EXPECT_EQ(*sharedFactor, s_HighAlignment);
    EXPECT_FALSE(CommonAlignmentChecked(u32{ 0u }, u32{ 8u }));
    EXPECT_FALSE(CommonAlignmentChecked(u32{ 8u }, u32{ 0u }));
    EXPECT_FALSE(CommonAlignmentChecked(Limit<u32>::s_Max, u32{ 2u }));
    const auto wideSharedFactor = CommonAlignmentChecked(Limit<u64>::s_Max - u64{ 1u }, u64{ 2u });
    ASSERT_TRUE(wideSharedFactor);
    EXPECT_EQ(*wideSharedFactor, Limit<u64>::s_Max - u64{ 1u });
    EXPECT_FALSE(CommonAlignmentChecked(Limit<u64>::s_Max, u64{ 2u }));
}

TEST(GlobalContractBoundaries, DecimalPrefixStopsAtMalformedSuffixAndPreservesUnsignedWrap){
    EXPECT_EQ(ParseDecimalPrefixU64(AStringView("18446744073709551616tail")), 0u);
    EXPECT_EQ(ParseDecimalPrefixU64(WStringView(L"18446744073709551617tail")), 1u);
    EXPECT_EQ(ParseDecimalPrefixU64(AStringView("17-9")), 17u);
    EXPECT_EQ(ParseDecimalPrefixU64(AStringView("-17")), 0u);
    EXPECT_EQ(ParseDecimalPrefixU64(AStringView{}), 0u);
}

TEST(GlobalContractBoundaries, WideHashCombinationRetainsHighBitsAndWraparound){
    u64 seed = 0xfedcba9876543210ull;
    HashCombineHash(seed, u64{ 0x8877665544332211ull }, s_HashCombineGoldenRatio64);
    EXPECT_EQ(seed, 0xe3480e4a004b1cbaull);
}

TEST(GlobalContractBoundaries, Utf8OffsetRejectsMalformedPrefixesWithoutReadingTheSuffix){
    const char bytes[]{ 'a', static_cast<char>(0xc3u), static_cast<char>(0xa9u), '\0', static_cast<char>(0xffu) };
    const AStringView text(bytes, sizeof(bytes));
    const auto boundary = Utf8ByteOffset(text, 2u);
    ASSERT_TRUE(boundary);
    EXPECT_EQ(*boundary, 3u);
    EXPECT_FALSE(IsValidUtf8TextWithoutNul(text));
    EXPECT_FALSE(Utf8ByteOffset(text, 3u));
    EXPECT_FALSE(Utf8ByteOffset(AStringView(bytes + 1u, 1u), 1u));
    EXPECT_FALSE(Utf8ByteOffset(AStringView{}, 1u));
    const auto emptyBoundary = Utf8ByteOffset(AStringView{}, 0u);
    ASSERT_TRUE(emptyBoundary);
    EXPECT_EQ(*emptyBoundary, 0u);
}

TEST(GlobalContractBoundaries, FixedIndexMapCollisionWrapAndCapacityFailurePreserveExistingEntries){
    NWB::Core::Alloc::ScratchArena arena(Name("tests/global/fixed_index_collision"));
    const auto collide = [](const u32)noexcept{ return usize{ 1u }; };
    FixedIndexMap<u32, NWB::Core::Alloc::ScratchArena, decltype(collide)> map(arena, 2u, collide);
    ASSERT_TRUE(map.valid());
    for(u32 key = 0u; key < 5u; ++key)
        ASSERT_TRUE(map.insertOrAssign(key, key + 10u));
    EXPECT_FALSE(map.insertOrAssign(5u, 99u));
    EXPECT_EQ(map.find(5u), nullptr);
    ASSERT_TRUE(map.insertOrAssign(4u, 77u));
    for(u32 key = 0u; key < 5u; ++key){
        const usize* const value = map.find(key);
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(*value, key == 4u ? 77u : key + 10u);
    }
    FixedIndexMap<u32, NWB::Core::Alloc::ScratchArena> overflow(arena, Limit<usize>::s_Max);
    EXPECT_FALSE(overflow.valid());
    EXPECT_FALSE(overflow.insertOrAssign(0u, 0u));
    EXPECT_EQ(overflow.find(0u), nullptr);
}

TEST(GlobalContractBoundaries, ThrowingNotNullConversionPropagatesAndPreservesThePriorPointer){
    using namespace __hidden_contract_boundary_tests;
    int original = 17;
    int replacement = 29;
    ThrowingPointerConversion conversion{ &replacement };
    static_assert(!noexcept(NotNull<int*>(conversion)));
    static_assert(!IsConstructible_V<NotNull<int*>, NotNull<const int*>>);
    static_assert(!IsConstructible_V<NotNull<ReferenceProbeDerived*>, NotNull<ReferenceProbeBase*>>);
    NotNull<int*> stored(&original);
    static_assert(!noexcept(stored = conversion));
    EXPECT_THROW((NotNull<int*>(conversion)), u32);
    EXPECT_THROW(stored = conversion, u32);
    EXPECT_EQ(stored.get(), &original);

    using ThrowingDestructorAssignment = NotNull<int*>&(NotNull<int*>::*)(PotentiallyThrowingPointerDestruction);
    using NonthrowingAssignment = NotNull<int*>&(NotNull<int*>::*)(NonthrowingPointerConversion)noexcept;
    static_assert(IsSame_V<decltype(&NotNull<int*>::template operator=<PotentiallyThrowingPointerDestruction>), ThrowingDestructorAssignment>);
    static_assert(IsSame_V<decltype(&NotNull<int*>::template operator=<NonthrowingPointerConversion>), NonthrowingAssignment>);
    static_assert(!noexcept(NotNull<int*>(DeclVal<PotentiallyThrowingPointerDestruction>())));
}

TEST(GlobalContractBoundaries, ConvertingCopyFromAConstReferenceOwnerRetainsItsValueDeleterAndLifetime){
    using namespace __hidden_contract_boundary_tests;
    u32 destroyedCount = 0u;
    u32 deleteCount = 0u;
    {
        const StatefulValueDeleter<ReferenceProbeDerived> deleter(s_DeleterTag, deleteCount);
        const RefCountPtr<ReferenceProbeDerived, StatefulValueDeleter<ReferenceProbeDerived>> source(new ReferenceProbeDerived(destroyedCount), deleter, s_AdoptRef);
        {
            const RefCountPtr<ReferenceProbeBase, StatefulValueDeleter<ReferenceProbeBase>> copied(source);
            EXPECT_EQ(copied.get(), source.get());
            EXPECT_EQ(source->referenceCount, 2u);
            EXPECT_EQ(copied.getDeleter().tag, s_DeleterTag);
            EXPECT_EQ(&copied.getDeleter().deleteCount, &deleteCount);
            EXPECT_EQ(destroyedCount, 0u);
        }
        EXPECT_EQ(source->referenceCount, 1u);
        EXPECT_EQ(deleteCount, 0u);
        EXPECT_EQ(destroyedCount, 0u);
    }
    EXPECT_EQ(deleteCount, 1u);
    EXPECT_EQ(destroyedCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(GlobalContractBoundaries, RedirectedProcessPreservesCombinedOutputAndExitStatusWithClosedStandardDescriptors){
    using namespace __hidden_contract_boundary_tests;
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/contract_redirected_closed_stdio");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    const auto outputPath = root / "combined.txt";
    const auto outputText = PathToString<char>(testArena.arena, outputPath);
    for(u32 mask = 1u; mask < 8u; ++mask){
        SCOPED_TRACE(mask);
        ASSERT_EXIT(VerifyRedirectedFileWithClosedDescriptors(mask, AStringView(outputText)), testing::ExitedWithCode(0), "");
    }
    ASSERT_TRUE(RemoveAllIfExists(root));
}

TEST(GlobalContractBoundaries, CapturedProcessPreservesStdoutAndFailureStatusWithClosedStandardDescriptors){
    using namespace __hidden_contract_boundary_tests;
    for(u32 mask = 1u; mask < 8u; ++mask){
        SCOPED_TRACE(mask);
        ASSERT_EXIT(VerifyCapturedOutputWithClosedDescriptors(mask), testing::ExitedWithCode(0), "");
    }
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

