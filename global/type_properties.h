// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class... T>
using Void_T = void;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <bool Val>
using BoolConstant = IntegralConstant<bool, Val>;

using TrueType = BoolConstant<true>;
using FalseType = BoolConstant<false>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_type_properties{
    template <typename T>
    struct IsReferenceWrapper : public FalseType{};
    template <typename T>
    struct IsReferenceWrapper<ReferenceWrapper<T>> : public TrueType{};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename = void>
struct IsCompleteType : public FalseType{};
template<typename T>
struct IsCompleteType<T, Void_T<decltype(sizeof(T) != 0)>> : public TrueType{};
template<>
struct IsCompleteType<const volatile void> : public FalseType{};
template<>
struct IsCompleteType<const void> : public FalseType{};
template<>
struct IsCompleteType<volatile void> : public FalseType{};
template<>
struct IsCompleteType<void> : public FalseType{};
template<typename T>
struct IsCompleteType<T, EnableIf_T<IsFunction_V<T>>> : public TrueType{};

template <typename T>
inline constexpr bool IsCompleteType_V = IsCompleteType<T, void>::value;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
struct IsReferenceWrapper : public __hidden_type_properties::IsReferenceWrapper<typename RemoveCV<T>::type>{};

template <typename T>
struct RemoveReferenceWrapper{ typedef T type; };
template <typename T>
struct RemoveReferenceWrapper<ReferenceWrapper<T>>{ typedef T& type; };
template <typename T>
struct RemoveReferenceWrapper<const ReferenceWrapper<T>>{ typedef T& type; };
template <typename T>
using RemoveReferenceWrapper_T = typename RemoveReferenceWrapper<T>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename CONTAINER>
constexpr BackInsertIterator<CONTAINER> BackInserter(CONTAINER& container)noexcept{ return BackInsertIterator<CONTAINER>(container); }
template <typename CONTAINER>
constexpr FrontInsertIterator<CONTAINER> FrontInserter(CONTAINER& container)noexcept{ return FrontInsertIterator<CONTAINER>(container); }

template <typename CONTAINER>
constexpr auto LengthOf(const CONTAINER& container)noexcept(noexcept(container.size()))->decltype(container.size()){ return container.size(); }
template <typename T, size_t Size>
constexpr auto LengthOf(const T(&)[Size])noexcept{ return Size; }

template <typename T>
constexpr auto Forward(RemoveReference_T<T>& v)noexcept{ return static_cast<T&&>(v); }
template <typename T>
constexpr auto Forward(RemoveReference_T<T>&& v)noexcept{
    static_assert(!IsLValueReference_V<T>, "bad forward call");
    return static_cast<T&&>(v);
}

template <typename T>
constexpr auto Move(T&& v)noexcept{ return static_cast<RemoveReference_T<T>&&>(v); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

