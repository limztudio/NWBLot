// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <functional>
#include <compare>
#include <memory>
#include <iterator>
#include <type_traits>
#include <utility>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PartialOrdering = std::partial_ordering;
using WeakOrdering = std::weak_ordering;
using StrongOrdering = std::strong_ordering;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Key>
struct Hasher : std::hash<Key>{};

template<typename Key>
    requires requires(const Key& key){
        static_cast<size_t>(key.hash());
    } && (!requires(const Key& key){
        std::hash<Key>{}(key);
    })
struct Hasher<Key>{
    constexpr size_t operator()(const Key& key)const noexcept{
        return static_cast<size_t>(key.hash());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class T>
using IsConst = std::is_const<T>;
template<class T>
inline constexpr b8 IsConst_V = std::is_const_v<T>;

template<class T>
using IsVolatile = std::is_volatile<T>;
template<class T>
inline constexpr b8 IsVolatile_V = std::is_volatile_v<T>;

template<class T>
using IsFunction = std::is_function<T>;
template<class T>
inline constexpr b8 IsFunction_V = std::is_function_v<T>;

template<class T>
using IsObject = std::is_object<T>;
template<class T>
inline constexpr b8 IsObject_V = std::is_object_v<T>;

template<class T>
using IsClass = std::is_class<T>;
template<class T>
inline constexpr b8 IsClass_V = std::is_class_v<T>;

template<class T>
using IsAbstract = std::is_abstract<T>;
template<class T>
inline constexpr b8 IsAbstract_V = std::is_abstract_v<T>;

template<class T>
using IsPolymorphic = std::is_polymorphic<T>;
template<class T>
inline constexpr b8 IsPolymorphic_V = std::is_polymorphic_v<T>;

template<class T>
using IsFinal = std::is_final<T>;
template<class T>
inline constexpr b8 IsFinal_V = std::is_final_v<T>;

template<class T>
using IsUnion = std::is_union<T>;
template<class T>
inline constexpr b8 IsUnion_V = std::is_union_v<T>;

template<class Base, class Derived>
using IsBaseOf = std::is_base_of<Base, Derived>;
template<class Base, class Derived>
inline constexpr b8 IsBaseOf_V = std::is_base_of_v<Base, Derived>;

template<class T>
using IsArray = std::is_array<T>;
template<class T>
inline constexpr b8 IsArray_V = std::is_array_v<T>;

template<class T>
using IsBoundedArray = std::is_bounded_array<T>;
template<class T>
inline constexpr b8 IsBoundedArray_V = std::is_bounded_array_v<T>;

template<class T>
using IsUnboundedArray = std::is_unbounded_array<T>;
template<class T>
inline constexpr b8 IsUnboundedArray_V = std::is_unbounded_array_v<T>;

template<class T>
using IsLValueReference = std::is_lvalue_reference<T>;
template<class T>
inline constexpr b8 IsLValueReference_V = std::is_lvalue_reference_v<T>;

template<class T>
using IsRValueReference = std::is_rvalue_reference<T>;
template<class T>
inline constexpr b8 IsRValueReference_V = std::is_rvalue_reference_v<T>;

template<class T>
using IsReference = std::is_reference<T>;
template<class T>
inline constexpr b8 IsReference_V = std::is_reference_v<T>;

template<class T>
using IsPointer = std::is_pointer<T>;
template<class T>
inline constexpr b8 IsPointer_V = std::is_pointer_v<T>;

template<class T>
using IsMemberObjectPointer = std::is_member_object_pointer<T>;
template<class T>
inline constexpr b8 IsMemberObjectPointer_V = std::is_member_object_pointer_v<T>;

template<class T>
using IsMemberPointer = std::is_member_pointer<T>;
template<class T>
inline constexpr b8 IsMemberPointer_V = std::is_member_pointer_v<T>;

template<class T>
using IsNullPointer = std::is_null_pointer<T>;
template<class T>
inline constexpr b8 IsNullPointer_V = std::is_null_pointer_v<T>;

template<class T>
using IsScalar = std::is_scalar<T>;
template<class T>
inline constexpr b8 IsScalar_V = std::is_scalar_v<T>;

template<class T>
using IsStandardLayout = std::is_standard_layout<T>;
template<class T>
inline constexpr b8 IsStandardLayout_V = std::is_standard_layout_v<T>;

template<class T>
using IsTriviallyCopyable = std::is_trivially_copyable<T>;
template<class T>
inline constexpr b8 IsTriviallyCopyable_V = std::is_trivially_copyable_v<T>;

template<class T>
using IsArithmetic = std::is_arithmetic<T>;
template<class T>
inline constexpr b8 IsArithmetic_V = std::is_arithmetic_v<T>;

template<class T>
using IsIntegral = std::is_integral<T>;
template<class T>
inline constexpr b8 IsIntegral_V = std::is_integral_v<T>;

template<class T>
using IsSigned = std::is_signed<T>;
template<class T>
inline constexpr b8 IsSigned_V = std::is_signed_v<T>;

template<class T>
using TypeIdentity = std::type_identity_t<T>;

template<class T>
using IsEmpty = std::is_empty<T>;
template<class T>
inline constexpr b8 IsEmpty_V = std::is_empty_v<T>;

template<class T, class F>
using IsSame = std::is_same<T, F>;
template<class T, class F>
inline constexpr b8 IsSame_V = std::is_same_v<T, F>;

template<class From, class To>
using IsConvertible = std::is_convertible<From, To>;
template<class From, class To>
inline constexpr b8 IsConvertible_V = std::is_convertible_v<From, To>;

template<class From, class To>
using IsNothrowConvertible = std::is_nothrow_convertible<From, To>;
template<class From, class To>
inline constexpr b8 IsNothrowConvertible_V = std::is_nothrow_convertible_v<From, To>;

template<class To, class From>
using IsAssignable = std::is_assignable<To, From>;
template<class To, class From>
inline constexpr b8 IsAssignable_V = std::is_assignable_v<To, From>;

template<class T>
using IsDefaultConstructible = std::is_default_constructible<T>;
template<class T>
inline constexpr b8 IsDefaultConstructible_V = std::is_default_constructible_v<T>;

template<class T, class... Args>
using IsConstructible = std::is_constructible<T, Args...>;
template<class T, class... Args>
inline constexpr b8 IsConstructible_V = std::is_constructible_v<T, Args...>;

template<class T>
using IsCopyConstructible = std::is_copy_constructible<T>;
template<class T>
inline constexpr b8 IsCopyConstructible_V = std::is_copy_constructible_v<T>;

template<class T, class... Args>
using IsNothrowConstructible = std::is_nothrow_constructible<T, Args...>;
template<class T, class... Args>
inline constexpr b8 IsNothrowConstructible_V = std::is_nothrow_constructible_v<T, Args...>;

template<class T>
using IsNothrowMoveConstructible = std::is_nothrow_move_constructible<T>;
template<class T>
inline constexpr b8 IsNothrowMoveConstructible_V = std::is_nothrow_move_constructible_v<T>;

template<class T>
using IsNothrowDestructible = std::is_nothrow_destructible<T>;
template<class T>
inline constexpr b8 IsNothrowDestructible_V = std::is_nothrow_destructible_v<T>;

template<class Func, class... Args>
using IsInvocable = std::is_invocable<Func, Args...>;
template<class Func, class... Args>
inline constexpr b8 IsInvocable_V = std::is_invocable_v<Func, Args...>;

template<typename R, typename Func, typename... Args>
using IsInvocableR = std::is_invocable_r<R, Func, Args...>;
template<typename R, typename Func, typename... Args>
inline constexpr b8 IsInvocableR_V = std::is_invocable_r_v<R, Func, Args...>;

template<typename Func, typename... Args>
inline constexpr b8 IsNothrowInvocable_V = std::is_nothrow_invocable_v<Func, Args...>;

template<typename R, typename Func, typename... Args>
using IsNothrowInvocableR = std::is_nothrow_invocable_r<R, Func, Args...>;
template<typename R, typename Func, typename... Args>
inline constexpr b8 IsNothrowInvocableR_V = std::is_nothrow_invocable_r_v<R, Func, Args...>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class T>
using Decay = std::decay<T>;
template<class T>
using Decay_T = typename Decay<T>::type;

template<class T>
using AddConst = std::add_const<T>;
template<class T>
using AddConst_T = typename AddConst<T>::type;

template<class T>
using AddVolatile = std::add_volatile<T>;
template<class T>
using AddVolatile_T = typename AddVolatile<T>::type;

template<class T>
using AddCV = std::add_cv<T>;
template<class T>
using AddCV_T = typename AddCV<T>::type;

template<class T>
using AddLValueReference = std::add_lvalue_reference<T>;
template<class T>
using AddLValueReference_T = typename AddLValueReference<T>::type;

template<class T>
using AddRValueReference = std::add_rvalue_reference<T>;
template<class T>
using AddRValueReference_T = typename AddRValueReference<T>::type;

template<class T>
using RemoveConst = std::remove_const<T>;
template<class T>
using RemoveConst_T = typename RemoveConst<T>::type;

template<class T>
using RemoveVolatile = std::remove_volatile<T>;
template<class T>
using RemoveVolatile_T = typename RemoveVolatile<T>::type;

template<class T>
using RemoveCV = std::remove_cv<T>;
template<class T>
using RemoveCV_T = typename RemoveCV<T>::type;

template<class T>
using RemoveReference = std::remove_reference<T>;
template<class T>
using RemoveReference_T = typename RemoveReference<T>::type;

template<class T>
using RemoveCVRef = std::remove_cvref<T>;
template<class T>
using RemoveCVRef_T = typename RemoveCVRef<T>::type;

template<class T>
using RemoveExtent = std::remove_extent<T>;
template<class T>
using RemoveExtent_T = typename RemoveExtent<T>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class Container>
using BackInsertIterator = std::back_insert_iterator<Container>;
template<class Container>
using FrontInsertIterator = std::front_insert_iterator<Container>;

template<typename T>
using ReferenceWrapper = std::reference_wrapper<T>;

template<class T>
using PointerTraits = std::pointer_traits<T>;

template<class T>
using CharTraits = std::char_traits<T>;

template<class T>
using AllocatorTraits = std::allocator_traits<T>;

template<class T, T Val>
using IntegralConstant = std::integral_constant<T, Val>;

template<size_t... Is>
using IndexSequence = std::index_sequence<Is...>;
template<typename... Ts>
using IndexSequenceFor = std::index_sequence_for<Ts...>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<b8 Test, class T = void> using EnableIf = std::enable_if<Test, T>;
template<b8 Test, class T = void> using EnableIf_T = typename EnableIf<Test, T>::type;

template<b8 Test, class T, class F>
using Conditional = std::conditional<Test, T, F>;
template<b8 Test, class T, class F>
using Conditional_T = typename Conditional<Test, T, F>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class T = void> using EqualTo = std::equal_to<T>;
template<class T = void> using NotEqualTo = std::not_equal_to<T>;

template<class T = void> using GreaterThan = std::greater<T>;
template<class T = void> using LessThan = std::less<T>;

template<class T = void> using GreaterThanEqualTo = std::greater_equal<T>;
template<class T = void> using LessThanEqualTo = std::less_equal<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class T, class Cat = PartialOrdering> concept ThreeWayComparable = std::three_way_comparable<T, Cat>;
template<class T, class F, class Cat = PartialOrdering> concept ThreeWayComparableWith = std::three_way_comparable_with<T, F, Cat>;
template<class T, class F = T> using CompareThreeWayResult_T = std::compare_three_way_result_t<T, F>;
template<class T, class F = T> using CompareThreeWayResult = typename std::compare_three_way_result<T, F>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class... T>
using CommonType = std::common_type<T...>;
template<class... T>
using CommonType_T = typename CommonType<T...>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class T, class F>
concept SameAs = std::same_as<T, F>;

template<class Derived, class Base>
concept DerivedFrom = std::derived_from<Derived, Base>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr auto IsConstantEvaluated()noexcept{ return std::is_constant_evaluated(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<class Pointer, class SizeType = std::size_t> using AllocationResult = std::allocation_result<Pointer, SizeType>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

