// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/perf/timing.h>
#include <global/named_registry.h>
#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::TelemetryCases{

template<typename T>
class BorrowedScopePointer{
public:
    explicit BorrowedScopePointer(T& object)noexcept : m_pointer(&object){}
    [[nodiscard]] T* const& get()const noexcept{ return m_pointer; }

private:
    T* m_pointer;
};

template<typename T>
using ScopeNonNull = NotNull<T*>;

template<typename T>
[[nodiscard]] ScopeNonNull<T> MakeScopeNonNull(T& record)noexcept{ return MakeNotNull(&record); }

template<typename Record, typename Id>
requires IsSame_V<decltype(FindNamedScope<Id>(DeclVal<Span<const BorrowedScopePointer<Record>>>(), DeclVal<Id>())), Record*>
[[nodiscard]] usize FindScopeAddress(Span<const BorrowedScopePointer<Record>> records, Id id)noexcept(noexcept(FindNamedScope<Id>(records, id))){
    return reinterpret_cast<usize>(FindNamedScope<Id>(records, id));
}

struct ThrowingScopeName{
    explicit operator Name()& noexcept{ return s_NameNone; }
    operator const Name&()const&{ throw failure; }

    u32 failure = 0;
};
inline constexpr b8 s_ExplicitScopeConversionIsNothrow = noexcept(Name(DeclVal<ThrowingScopeName&>()));
inline constexpr b8 s_ImplicitScopeConversionIsNothrow = noexcept(DeclVal<void(*)(Name)noexcept>()(DeclVal<ThrowingScopeName&>()));

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

