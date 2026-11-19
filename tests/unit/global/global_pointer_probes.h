// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/refcount_ptr.h>
#include <global/unique_ptr.h>
#include <tests/unit/global/name_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pointer_failure_tests{

inline constexpr u32 s_PointerFailure = NWB::Tests::GlobalCases::s_GlobalPointerFailure;
inline constexpr u32 s_ComparisonFailure = NWB::Tests::GlobalCases::s_GlobalComparisonFailure;

struct RefCountTarget{};

struct ThrowingFancyPointer{
public:
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

[[nodiscard]] FOUNDATION_INLINE b8 operator==(const ThrowingFancyPointer& lhs, const ThrowingFancyPointer& rhs)noexcept{
    return lhs.target == rhs.target;
}

FOUNDATION_INLINE u32 RefCountAddReference(ThrowingFancyPointer)noexcept{ return 1u; }

FOUNDATION_INLINE u32 RefCountRelease(ThrowingFancyPointer)noexcept{ return 1u; }

struct FancyPointerDeleter{
public:
    using pointer = ThrowingFancyPointer;


public:
    void operator()(pointer)const noexcept{}
};

struct ThrowingComparisonResult{
public:
    operator b8()const{ throw s_ComparisonFailure; }

    [[nodiscard]] ThrowingComparisonResult operator!()const noexcept{ return {}; }
};

template<typename T>
struct ComparisonProxyDeleter{
public:
    void operator()(T*)const noexcept{}
};

struct ComparisonReferenceTarget{
public:
    u32 references = 1u;


public:
    [[nodiscard]] u32 addReference()noexcept{ return ++references; }

    [[nodiscard]] u32 release()noexcept{ return --references; }
};

using UniqueComparisonOwner = UniquePtr<int, ComparisonProxyDeleter<int>>;
using ReferenceComparisonOwner = RefCountPtr<ComparisonReferenceTarget, ComparisonProxyDeleter<ComparisonReferenceTarget>>;

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(const UniqueComparisonOwner&, const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(const UniqueComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(decltype(nullptr), const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(const ReferenceComparisonOwner&, decltype(nullptr))noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator<(decltype(nullptr), const ReferenceComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator!(const UniqueComparisonOwner&)noexcept{ return {}; }

[[nodiscard]] FOUNDATION_INLINE ThrowingComparisonResult operator!(const ReferenceComparisonOwner&)noexcept{ return {}; }

struct ObservationPointer{
public:
    inline static b8 s_FailCopy = false;
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

[[nodiscard]] FOUNDATION_INLINE b8 operator==(const ObservationPointer& first, const ObservationPointer& second)noexcept{ return first.target == second.target; }

FOUNDATION_INLINE u32 RefCountAddReference(ObservationPointer)noexcept{ return 1u; }

FOUNDATION_INLINE u32 RefCountRelease(ObservationPointer)noexcept{ return 1u; }

struct ObservationDeleter{
public:
    using pointer = ObservationPointer;


public:
    void operator()(pointer)const noexcept{}
};
using FancyOwner = RefCountPtr<RefCountTarget, FancyPointerDeleter>;
using ObservedUnique = UniquePtr<RefCountTarget, ObservationDeleter>;
using ObservedShared = RefCountPtr<RefCountTarget, ObservationDeleter>;
using PlainUnique = UniquePtr<int>;
using PlainReference = RefCountPtr<ComparisonReferenceTarget>;
using NullOwner = decltype(nullptr);
inline constexpr NullOwner s_NullOwner = nullptr;

static_assert(noexcept(ObservationPointer(DeclVal<const ObservationPointer&>())));
static_assert(!noexcept(DeclVal<void(*)(ObservationPointer)noexcept>()(DeclVal<const ObservationPointer&>())));

[[nodiscard]] inline FancyOwner BorrowFancy(RefCountTarget& target){ return FancyOwner(ThrowingFancyPointer::Borrow(&target), s_AdoptRef); }
[[nodiscard]] inline ObservedUnique BorrowObservedUnique(RefCountTarget& target){ return ObservedUnique(ObservationPointer{ &target }); }
[[nodiscard]] inline ObservedShared BorrowObservedShared(RefCountTarget& target){ return ObservedShared(ObservationPointer{ &target }, s_AdoptRef); }
template<typename Owner>
[[nodiscard]] bool PointsAt(const Owner& owner, RefCountTarget& target)noexcept(noexcept(owner.get())){ return owner.get().target == &target; }
template<typename Owner>
[[nodiscard]] bool ArrowPointsAt(const Owner& owner, RefCountTarget& target)noexcept(noexcept(owner.operator->())){ return owner.operator->().target == &target; }
template<typename Owner>
[[nodiscard]] auto& Dereference(const Owner& owner)noexcept(noexcept(*owner)){ return *owner; }
inline void AssignRaw(FancyOwner& owner, RefCountTarget& target)noexcept(noexcept(owner = &target)){ owner = &target; }
inline void FailObserverCopy(bool value)noexcept{ ObservationPointer::s_FailCopy = value; }
inline bool FailsObserverCopy()noexcept{ return ObservationPointer::s_FailCopy; }
template<typename Owner>
[[nodiscard]] bool HasOwner(const Owner& owner)noexcept(noexcept(static_cast<bool>(owner))){ return static_cast<bool>(owner); }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_TEST_LESS(lhs, rhs) ((lhs) < (rhs))
#define NWB_TEST_NEGATED_LESS(lhs, rhs) (!((lhs) < (rhs)))
#define NWB_TEST_NEGATED_OWNER(owner) (!(owner))
#define NWB_TEST_OWNER_EQUAL(lhs, rhs) ::operator==((lhs), (rhs))
#define NWB_TEST_OWNER_GREATER(lhs, rhs) ::operator>((lhs), (rhs))
#define NWB_TEST_OWNER_LESS_EQUAL(lhs, rhs) ::operator<=((lhs), (rhs))
#define NWB_TEST_OWNER_GREATER_EQUAL(lhs, rhs) ::operator>=((lhs), (rhs))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

