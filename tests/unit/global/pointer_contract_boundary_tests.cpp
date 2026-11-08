// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/global/contract_name_cases.adhd.h>

#include <global/not_null.h>
#include <global/refcount_ptr.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_global_pointer_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ConversionFailure = NWB::Tests::GlobalContractCases::s_GlobalContractConversionFailure;
constexpr u32 s_DeleterTag = NWB::Tests::GlobalContractCases::s_GlobalContractDeleterTag;


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


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalContractBoundaries, ThrowingNotNullConversionPropagatesAndPreservesThePriorPointer){
    using namespace __hidden_global_pointer_contract_tests;
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
    using namespace __hidden_global_pointer_contract_tests;
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

