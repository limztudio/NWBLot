// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/binary.h>
#include <global/compact_string.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations_file_io.h>
#include <global/hash_utils.h>
#include <global/inplace_function.h>
#include <global/numeric_text.h>
#include <global/refcount_ptr.h>
#include <global/unique_ptr.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pointer_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_PointerFailure = 0xE204u;
inline constexpr u32 s_ComparisonFailure = 0xE209u;


struct RefCountTarget{};

struct ThrowingFancyPointer{
    RefCountTarget* target = nullptr;


public:
    [[nodiscard]] static ThrowingFancyPointer Borrow(RefCountTarget* value)noexcept{
        ThrowingFancyPointer result;
        result.target = value;
        return result;
    }


public:
    ThrowingFancyPointer()noexcept = default;
    ThrowingFancyPointer(RefCountTarget* value){
        if(value)
            throw s_PointerFailure;
    }


public:
    [[nodiscard]] RefCountTarget& operator*()const{
        throw s_PointerFailure;
    }

    explicit operator b8()const noexcept{ return target != nullptr; }
};

[[nodiscard]] b8 operator==(const ThrowingFancyPointer& lhs, const ThrowingFancyPointer& rhs)noexcept{
    return lhs.target == rhs.target;
}

u32 RefCountAddReference(ThrowingFancyPointer)noexcept{ return 1u; }

u32 RefCountRelease(ThrowingFancyPointer)noexcept{ return 1u; }

struct FancyPointerDeleter{
    using pointer = ThrowingFancyPointer;


public:
    void operator()(pointer)const noexcept{}
};

struct ThrowingComparisonResult{
    operator b8()const{ throw s_ComparisonFailure; }

    [[nodiscard]] ThrowingComparisonResult operator!()const noexcept{ return {}; }
};

template<typename T>
struct ComparisonProxyDeleter{
    void operator()(T*)const noexcept{}
};

struct ComparisonReferenceTarget{
    u32 references = 1u;


public:
    [[nodiscard]] u32 addReference()noexcept{ return ++references; }

    [[nodiscard]] u32 release()noexcept{ return --references; }
};

using UniqueComparisonOwner = UniquePtr<int, ComparisonProxyDeleter<int>>;
using ReferenceComparisonOwner = RefCountPtr<ComparisonReferenceTarget, ComparisonProxyDeleter<ComparisonReferenceTarget>>;

[[nodiscard]] ThrowingComparisonResult operator<(const UniqueComparisonOwner&, const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const UniqueComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(decltype(nullptr), const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator<(decltype(nullptr), const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator!(const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] ThrowingComparisonResult operator!(const ReferenceComparisonOwner&)noexcept{ return {}; }

template<typename OwnerT>
void VerifyComparisonResultConversions(){
    OwnerT first;
    OwnerT second;
    static_assert(noexcept(first < second));
    static_assert(noexcept(!(first < second)));
    static_assert(noexcept(!first));
    static_assert(!noexcept(::operator==(first, nullptr)));
    static_assert(!noexcept(::operator>(first, second)));
    static_assert(!noexcept(::operator<=(first, second)));
    static_assert(!noexcept(::operator>=(first, second)));
    static_assert(!noexcept(::operator>(first, nullptr)));
    static_assert(!noexcept(::operator>(nullptr, first)));
    static_assert(!noexcept(::operator<=(first, nullptr)));
    static_assert(!noexcept(::operator<=(nullptr, first)));
    static_assert(!noexcept(::operator>=(first, nullptr)));
    static_assert(!noexcept(::operator>=(nullptr, first)));
    EXPECT_THROW(::operator==(first, nullptr), u32);
    EXPECT_THROW(::operator>(first, second), u32);
    EXPECT_THROW(::operator<=(first, second), u32);
    EXPECT_THROW(::operator>=(first, second), u32);
    EXPECT_THROW(::operator>(first, nullptr), u32);
    EXPECT_THROW(::operator>(nullptr, first), u32);
    EXPECT_THROW(::operator<=(first, nullptr), u32);
    EXPECT_THROW(::operator<=(nullptr, first), u32);
    EXPECT_THROW(::operator>=(first, nullptr), u32);
    EXPECT_THROW(::operator>=(nullptr, first), u32);
    EXPECT_FALSE(first);
    EXPECT_FALSE(second);
}

struct ObservationPointer{
    static inline b8 s_FailCopy = false;
    RefCountTarget* target = nullptr;


public:
    ObservationPointer()noexcept = default;
    ObservationPointer(decltype(nullptr))noexcept{}
    explicit ObservationPointer(RefCountTarget* value)noexcept
        : target(value)
    {}
    ObservationPointer(const volatile ObservationPointer& value)
        : target(value.target)
    {
        if(s_FailCopy)
            throw s_PointerFailure;
    }
    template<typename Other> requires IsSame_V<RemoveCVRef_T<Other>, ObservationPointer>
    explicit ObservationPointer(Other&& value)noexcept
        : target(value.target)
    {}
    ObservationPointer(ObservationPointer&&)noexcept = default;


public:
    ObservationPointer& operator=(const ObservationPointer&)noexcept = default;

    ObservationPointer& operator=(ObservationPointer&&)noexcept = default;

    explicit operator b8()const noexcept{ return target != nullptr; }
};

[[nodiscard]] b8 operator==(const ObservationPointer& first, const ObservationPointer& second)noexcept{ return first.target == second.target; }

u32 RefCountAddReference(ObservationPointer)noexcept{ return 1u; }

u32 RefCountRelease(ObservationPointer)noexcept{ return 1u; }

struct ObservationDeleter{
    using pointer = ObservationPointer;


public:
    void operator()(pointer)const noexcept{}
};




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NoexceptFailureTests, FancyPointerDereferenceAndRawConversionFailuresPreserveBorrowedTarget){
    using namespace __hidden_pointer_failure_tests;
    using Storage = RefCountPtr<RefCountTarget, FancyPointerDeleter>;
    RefCountTarget target;
    Storage stored(ThrowingFancyPointer::Borrow(&target), s_AdoptRef);
    static_assert(!noexcept(*stored));
    static_assert(!noexcept(stored = &target));

    EXPECT_THROW(EXPECT_EQ(&*stored, &target), u32);
    EXPECT_THROW(stored = &target, u32);
    EXPECT_EQ(stored.get().target, &target);
}

TEST(NoexceptFailureTests, UniqueOwnerComparisonResultConversionUnwindsThroughEveryBooleanWrapper){
    using namespace __hidden_pointer_failure_tests;
    UniquePtr<int> first;
    UniquePtr<int> second;
    static_assert(noexcept(::operator==(first, nullptr)));
    static_assert(noexcept(::operator>(first, second)));
    static_assert(noexcept(::operator<=(first, second)));
    static_assert(noexcept(::operator>=(first, second)));
    VerifyComparisonResultConversions<UniqueComparisonOwner>();
}

TEST(NoexceptFailureTests, ReferenceOwnerComparisonResultConversionUnwindsThroughEveryBooleanWrapper){
    using namespace __hidden_pointer_failure_tests;
    RefCountPtr<ComparisonReferenceTarget> first;
    RefCountPtr<ComparisonReferenceTarget> second;
    static_assert(noexcept(::operator==(first, nullptr)));
    static_assert(noexcept(::operator>(first, second)));
    static_assert(noexcept(::operator<=(first, second)));
    static_assert(noexcept(::operator>=(first, second)));
    VerifyComparisonResultConversions<ReferenceComparisonOwner>();
}

TEST(NoexceptFailureTests, OwnerObservationUsesImplicitCopyAndPreservesBorrowedTargetsOnFailure){
    using namespace __hidden_pointer_failure_tests;
    static_assert(noexcept(ObservationPointer(DeclVal<const ObservationPointer&>())));
    static_assert(!noexcept(DeclVal<void(*)(ObservationPointer)noexcept>()(DeclVal<const ObservationPointer&>())));
    RefCountTarget target;
    UniquePtr<RefCountTarget, ObservationDeleter> unique(ObservationPointer{ &target });
    RefCountPtr<RefCountTarget, ObservationDeleter> shared(ObservationPointer{ &target }, s_AdoptRef);
    ScopeExit disarm([]()noexcept{ ObservationPointer::s_FailCopy = false; });
    static_assert(!noexcept(unique.get()));
    static_assert(!noexcept(unique.operator->()));
    static_assert(!noexcept(shared.get()));
    static_assert(!noexcept(shared.operator->()));
    ObservationPointer::s_FailCopy = true;
    EXPECT_THROW(EXPECT_EQ(unique.get().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(unique.operator->().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(shared.get().target, &target), u32);
    EXPECT_THROW(EXPECT_EQ(shared.operator->().target, &target), u32);
    ObservationPointer::s_FailCopy = false;
    EXPECT_EQ(unique.get().target, &target);
    EXPECT_EQ(shared.get().target, &target);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


