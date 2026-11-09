// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "smart_ptr_deleter.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SmartPtrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source, typename Target>
struct IsArenaDeleterConversion : FalseType{};
template<typename U, typename T, typename POOL>
struct IsArenaDeleterConversion<ArenaDeleter<U, POOL>, ArenaDeleter<T, POOL>> : IntegralConstant<b8, !IsArray_V<U> && !IsArray_V<T> && !IsSame_V<RemoveCV_T<U>, RemoveCV_T<T>> && IsConvertible_V<U*, T*>>{};

template<typename Source, typename Target>
inline constexpr b8 IsArenaDeleterConversion_V = IsArenaDeleterConversion<RemoveCVRef_T<Source>, RemoveCVRef_T<Target>>::value;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Deleter>
class UniquePointerType{
private:
    struct PointerProbe{
        template<typename U>
        static typename U::pointer Test(typename U::pointer*);
        template<typename U>
        static T* Test(...);
    };


public:
    using type = decltype(PointerProbe::template Test<typename RemoveReference<Deleter>::type>(0));
};

template<typename Base, typename Derived>
struct IsDerived : public IntegralConstant<b8, IsBaseOf<Base, Derived>::value && !IsSame<typename RemoveCV<Base>::type, typename RemoveCV<Derived>::type>::value>{};

template<typename T, typename TPointer, typename U, typename UPointer>
struct IsSafeArrayConversion : public IntegralConstant<b8, IsConvertible<UPointer, TPointer>::value && IsArray<U>::value && (!IsPointer<UPointer>::value || !IsPointer<TPointer>::value || !IsDerived<T, typename RemoveExtent<U>::type>::value)>{};

template<typename OwnerA, typename OwnerB>
using OwnerPointerCompareResult_T = CompareThreeWayResult_T<typename OwnerA::pointer, typename OwnerB::pointer>;

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline b8 OwnerPointerEqual(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
){
    return a.get() == b.get();
}

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline OwnerPointerCompareResult_T<OwnerA, OwnerB> OwnerPointerCompare(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
){
    return a.get() <=> b.get();
}

template<typename OwnerA, typename OwnerB>
[[nodiscard]] inline b8 OwnerPointerLess(const OwnerA& a, const OwnerB& b)noexcept(
    IsPointer_V<typename OwnerA::pointer>
    && IsPointer_V<typename OwnerB::pointer>
    && IsSame_V<decltype(a.get()), typename OwnerA::pointer>
    && IsSame_V<decltype(b.get()), typename OwnerB::pointer>
    && noexcept(a.get())
    && noexcept(b.get())
    && IsPointer_V<typename CommonType<typename OwnerA::pointer, typename OwnerB::pointer>::type>
    && noexcept(static_cast<b8>(LessThan<typename CommonType<typename OwnerA::pointer, typename OwnerB::pointer>::type>{}(a.get(), b.get())))
){
    using P1 = typename OwnerA::pointer;
    using P2 = typename OwnerB::pointer;
    using PCommon = typename CommonType<P1, P2>::type;
    PCommon lhsPointer = a.get();
    PCommon rhsPointer = b.get();
    return LessThan<PCommon>()(lhsPointer, rhsPointer);
}

template<typename Owner>
[[nodiscard]] inline b8 OwnerPointerLessNull(const Owner& a)noexcept(
    IsPointer_V<typename Owner::pointer>
    && IsSame_V<decltype(a.get()), typename Owner::pointer>
    && noexcept(a.get())
    && noexcept(static_cast<b8>(LessThan<typename Owner::pointer>{}(a.get(), nullptr)))
){
    using pointer = typename Owner::pointer;
    return LessThan<pointer>()(a.get(), nullptr);
}

template<typename Owner>
[[nodiscard]] inline b8 NullLessOwnerPointer(const Owner& b)noexcept(
    IsPointer_V<typename Owner::pointer>
    && IsSame_V<decltype(b.get()), typename Owner::pointer>
    && noexcept(b.get())
    && noexcept(static_cast<b8>(LessThan<typename Owner::pointer>{}(nullptr, b.get())))
){
    using pointer = typename Owner::pointer;
    pointer rhsPointer = b.get();
    return LessThan<pointer>()(nullptr, rhsPointer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define FOUNDATION_SMART_PTR_COMPARISON_OPERATORS(OwnerType) \
template<typename T1, typename D1, typename T2, typename D2> \
inline b8 operator==(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerEqual(a, b))){ return SmartPtrDetail::OwnerPointerEqual(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
requires ThreeWayComparableWith<typename OwnerType<T1, D1>::pointer, typename OwnerType<T2, D2>::pointer> \
inline CompareThreeWayResult_T<typename OwnerType<T1, D1>::pointer, typename OwnerType<T2, D2>::pointer> \
operator<=>(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerCompare(a, b))){ return SmartPtrDetail::OwnerPointerCompare(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline b8 operator<(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(SmartPtrDetail::OwnerPointerLess(a, b))){ return SmartPtrDetail::OwnerPointerLess(a, b); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline b8 operator>(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()((b < a)))){ return (b < a); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline b8 operator<=(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(b < a)))){ return !(b < a); } \
template<typename T1, typename D1, typename T2, typename D2> \
inline b8 operator>=(const OwnerType<T1, D1>& a, const OwnerType<T2, D2>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(a < b)))){ return !(a < b); } \
template<typename T, typename D> \
inline b8 operator==(const OwnerType<T, D>& a, Nullptr)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!a))){ return !a; } \
template<typename T, typename D> \
requires ThreeWayComparableWith<typename OwnerType<T, D>::pointer, Nullptr> \
inline CompareThreeWayResult_T<typename OwnerType<T, D>::pointer, Nullptr> operator<=>(const OwnerType<T, D>& a, Nullptr)noexcept(IsPointer_V<typename OwnerType<T, D>::pointer> && noexcept(a.get())){ return a.get() <=> nullptr; } \
template<typename T, typename D> \
inline b8 operator<(const OwnerType<T, D>& a, Nullptr)noexcept(noexcept(SmartPtrDetail::OwnerPointerLessNull(a))){ return SmartPtrDetail::OwnerPointerLessNull(a); } \
template<typename T, typename D> \
inline b8 operator<(Nullptr, const OwnerType<T, D>& b)noexcept(noexcept(SmartPtrDetail::NullLessOwnerPointer(b))){ return SmartPtrDetail::NullLessOwnerPointer(b); } \
template<typename T, typename D> \
inline b8 operator>(const OwnerType<T, D>& a, Nullptr)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()((nullptr < a)))){ return (nullptr < a); } \
template<typename T, typename D> \
inline b8 operator>(Nullptr, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()((b < nullptr)))){ return (b < nullptr); } \
template<typename T, typename D> \
inline b8 operator<=(const OwnerType<T, D>& a, Nullptr)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(nullptr < a)))){ return !(nullptr < a); } \
template<typename T, typename D> \
inline b8 operator<=(Nullptr, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(b < nullptr)))){ return !(b < nullptr); } \
template<typename T, typename D> \
inline b8 operator>=(const OwnerType<T, D>& a, Nullptr)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(a < nullptr)))){ return !(a < nullptr); } \
template<typename T, typename D> \
inline b8 operator>=(Nullptr, const OwnerType<T, D>& b)noexcept(noexcept(DeclVal<void(*)(b8)noexcept>()(!(nullptr < b)))){ return !(nullptr < b); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

