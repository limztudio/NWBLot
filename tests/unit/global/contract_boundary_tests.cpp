// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/not_null.h>
#include <global/overflow.h>
#include <global/process_execution.h>
#include <global/refcount_ptr.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_contract_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ConversionFailure = 0xE301u;
constexpr u32 s_DeleterTag = 47u;

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

    [[nodiscard]] operator int*()const{ throw s_ConversionFailure; }
};

struct NonthrowingPointerConversion{
    int* value;

    [[nodiscard]] operator int*()const noexcept{ return value; }
};

struct PotentiallyThrowingPointerDestruction{
    int* value;

    ~PotentiallyThrowingPointerDestruction()noexcept(false){}
    [[nodiscard]] operator int*()const noexcept{ return value; }
};

struct ReferenceProbeBase{
    u32 referenceCount = 1u;
    u32& destroyedCount;

    explicit ReferenceProbeBase(u32& count)noexcept
        : destroyedCount(count)
    {}
    virtual ~ReferenceProbeBase()noexcept{ ++destroyedCount; }

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

    StatefulValueDeleter(const u32 value, u32& count)noexcept
        : tag(value)
        , deleteCount(count)
    {}
    template<typename U>
    StatefulValueDeleter(const StatefulValueDeleter<U>& other)noexcept
        : tag(other.tag)
        , deleteCount(other.deleteCount)
    {}

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
    constexpr AStringView s_SuccessArguments[]{ "/bin/sh", "-c", "printf 'captured\\n'; printf 'discarded\\n' >&2; exit 0" };
    if(!CaptureProcessOutput(testArena.arena, output, s_SuccessArguments, 256u, 64u, 2000u) || AStringView(output) != "captured\n")
        ::_exit(21);
    constexpr AStringView s_FailureArguments[]{ "/bin/sh", "-c", "printf 'retained\\n'; printf 'discarded\\n' >&2; exit 23" };
    if(CaptureProcessOutput(testArena.arena, output, s_FailureArguments, 256u, 64u, 2000u) || AStringView(output) != "retained\n")
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

TEST(GlobalContractBoundaries, ThrowingNotNullConversionPropagatesAndPreservesThePriorPointer){
    using namespace __hidden_contract_boundary_tests;
    int original = 17;
    int replacement = 29;
    ThrowingPointerConversion conversion{ &replacement };
    static_assert(!noexcept(NotNull<int*>(conversion)));
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

