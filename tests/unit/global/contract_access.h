// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/fixed_index_map.h>
#include <global/not_null.h>
#include <global/process_capture.h>
#include <global/refcount_ptr.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>
#include <tests/unit/global/contract_name_cases.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalContractCases{

struct ThrowingPointerConversion{
public:
    int* value;


public:
    [[nodiscard]] operator int*()const{ throw s_GlobalContractConversionFailure; }
};

struct NonthrowingPointerConversion{
public:
    int* value;


public:
    [[nodiscard]] operator int*()const noexcept{ return value; }
};

struct PotentiallyThrowingPointerDestruction{
public:
    int* value;


public:
    ~PotentiallyThrowingPointerDestruction()noexcept(false){}


public:
    [[nodiscard]] operator int*()const noexcept{ return value; }
};

template<typename T>
struct StatefulValueDeleter{
public:
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
using ContractNotNull = ::NotNull<i32*>;
using ThrowingDestructorAssignment = ContractNotNull&(ContractNotNull::*)(PotentiallyThrowingPointerDestruction);
using NonthrowingAssignment = ContractNotNull&(ContractNotNull::*)(NonthrowingPointerConversion)noexcept;
static_assert(!IsConstructible_V<ContractNotNull, ::NotNull<const i32*>>);
static_assert(IsSame_V<decltype(&ContractNotNull::template operator=<PotentiallyThrowingPointerDestruction>), ThrowingDestructorAssignment>);
static_assert(IsSame_V<decltype(&ContractNotNull::template operator=<NonthrowingPointerConversion>), NonthrowingAssignment>);
static_assert(!noexcept(ContractNotNull(DeclVal<PotentiallyThrowingPointerDestruction>())));

[[nodiscard]] inline ThrowingPointerConversion ThrowingPointer(i32& value)noexcept{ return { &value }; }
[[nodiscard]] inline ContractNotNull NonNullAddress(i32& value)noexcept{ return ContractNotNull(&value); }
inline void AssignPointer(ContractNotNull& target, ThrowingPointerConversion source)noexcept(noexcept(target = source)){ target = source; }
[[nodiscard]] inline i32& PointedValue(const ContractNotNull& pointer)noexcept{ return *pointer; }

template<typename T>
using ContractOwner = RefCountPtr<T, StatefulValueDeleter<T>>;

template<typename T>
[[nodiscard]] ContractOwner<T> AdoptProbe(u32& destroyed, u32 tag, u32& deleted){
    return ContractOwner<T>(new T(destroyed), StatefulValueDeleter<T>(tag, deleted), s_AdoptRef);
}

template<typename T>
[[nodiscard]] T& Target(const ContractOwner<T>& owner)noexcept{ return *owner.get(); }

template<typename Base, typename Derived>
[[nodiscard]] bool SameProbe(const ContractOwner<Base>& lhs, const ContractOwner<Derived>& rhs)noexcept{
    static_assert(!IsConstructible_V<NotNull<Derived*>, NotNull<Base*>>);
    return lhs.get() == rhs.get();
}

using HashCallback = usize(*)(u32)noexcept;
using CollisionMap = ::FixedIndexMap<u32, Core::Alloc::ScratchArena, HashCallback>;
using OrdinaryMap = ::FixedIndexMap<u32, Core::Alloc::ScratchArena>;
template<typename Map>
[[nodiscard]] Optional<usize> FindIndex(const Map& map, u32 key){
    const usize* value = map.find(key);
    if(value)
        return *value;
    return {};
}

inline constexpr WStringView s_WideDecimal = L"18446744073709551617tail";

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
#define NWB_CONTRACT_LINUX_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_CONTRACT_LINUX_CASE(Suite, Case, Fn)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

