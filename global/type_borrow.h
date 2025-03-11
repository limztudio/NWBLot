// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PartialOrdering = std::partial_ordering;
using WeakOrdering = std::weak_ordering;
using StrongOrdering = std::strong_ordering;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class T>
using IsConst = std::is_const<T>;
template <class T>
inline constexpr bool IsConst_V = std::is_const_v<T>;

template <class T>
using IsVolatile = std::is_volatile<T>;
template <class T>
inline constexpr bool IsVolatile_V = std::is_volatile_v<T>;

template <class T>
using IsFunction = std::is_function<T>;
template <class T>
inline constexpr bool IsFunction_V = std::is_function_v<T>;

template <class T>
using IsObject = std::is_object<T>;
template <class T>
inline constexpr bool IsObject_V = std::is_object_v<T>;

template <class T>
using IsClass = std::is_class<T>;
template <class T>
inline constexpr bool IsClass_V = std::is_class_v<T>;

template <class T>
using IsAbstract = std::is_abstract<T>;
template <class T>
inline constexpr bool IsAbstract_V = std::is_abstract_v<T>;

template <class T>
using IsPolymorphic = std::is_polymorphic<T>;
template <class T>
inline constexpr bool IsPolymorphic_V = std::is_polymorphic_v<T>;

template <class T>
using IsFinal = std::is_final<T>;
template <class T>
inline constexpr bool IsFinal_V = std::is_final_v<T>;

template <class T>
using IsUnion = std::is_union<T>;
template <class T>
inline constexpr bool IsUnion_V = std::is_union_v<T>;

template <class Base, class Derived>
using IsBaseOf = std::is_base_of<Base, Derived>;
template <class Base, class Derived>
inline constexpr bool IsBaseOf_V = std::is_base_of_v<Base, Derived>;

template <class T>
using IsArray = std::is_array<T>;
template <class T>
inline constexpr bool IsArray_V = std::is_array_v<T>;

template <class T>
using IsBoundedArray = std::is_bounded_array<T>;
template <class T>
inline constexpr bool IsBoundedArray_V = std::is_bounded_array_v<T>;

template <class T>
using IsUnboundedArray = std::is_unbounded_array<T>;
template <class T>
inline constexpr bool IsUnboundedArray_V = std::is_unbounded_array_v<T>;

template <class T>
using IsLValueReference = std::is_lvalue_reference<T>;
template <class T>
inline constexpr bool IsLValueReference_V = std::is_lvalue_reference_v<T>;

template <class T>
using IsRValueReference = std::is_rvalue_reference<T>;
template <class T>
inline constexpr bool IsRValueReference_V = std::is_rvalue_reference_v<T>;

template <class T>
using IsReference = std::is_reference<T>;
template <class T>
inline constexpr bool IsReference_V = std::is_reference_v<T>;

template <class T>
using IsPointer = std::is_pointer<T>;
template <class T>
inline constexpr bool IsPointer_V = std::is_pointer_v<T>;

template <class T>
using IsMemberObjectPointer = std::is_member_object_pointer<T>;
template <class T>
inline constexpr bool IsMemberObjectPointer_V = std::is_member_object_pointer_v<T>;

template <class T>
using IsMemberPointer = std::is_member_pointer<T>;
template <class T>
inline constexpr bool IsMemberPointer_V = std::is_member_pointer_v<T>;

template <class T>
using IsNullPointer = std::is_null_pointer<T>;
template <class T>
inline constexpr bool IsNullPointer_V = std::is_null_pointer_v<T>;

template <class T>
using IsScalar = std::is_scalar<T>;
template <class T>
inline constexpr bool IsScalar_V = std::is_scalar_v<T>;

template <class T>
using IsArithmetic = std::is_arithmetic<T>;
template <class T>
inline constexpr bool IsArithmetic_V = std::is_arithmetic_v<T>;

template <class T>
using IsEmpty = std::is_empty<T>;
template <class T>
inline constexpr bool IsEmpty_V = std::is_empty_v<T>;

template <class T, class F>
using IsSame = std::is_same<T, F>;
template <class T, class F>
inline constexpr bool IsSame_V = std::is_same_v<T, F>;

template <class From, class To>
using IsConvertible = std::is_convertible<From, To>;
template <class From, class To>
inline constexpr bool IsConvertible_V = std::is_convertible_v<From, To>;

template <class To, class From>
using IsAssignable = std::is_assignable<To, From>;
template <class To, class From>
inline constexpr bool IsAssignable_V = std::is_assignable_v<To, From>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class T>
using AddConst = std::add_const<T>;
template <class T>
using AddConst_V = typename AddConst<T>::type;

template <class T>
using AddVolatile = std::add_volatile<T>;
template <class T>
using AddVolatile_V = typename AddVolatile<T>::type;

template <class T>
using AddCV = std::add_cv<T>;
template <class T>
using AddCV_V = typename AddCV<T>::type;

template <class T>
using AddLValueReference = std::add_lvalue_reference<T>;
template <class T>
using AddLValueReference_V = typename AddLValueReference<T>::type;

template <class T>
using AddRValueReference = std::add_rvalue_reference<T>;
template <class T>
using AddRValueReference_V = typename AddRValueReference<T>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class T>
using RemoveConst = std::remove_const<T>;
template <class T>
using RemoveConst_T = typename RemoveConst<T>::type;

template <class T>
using RemoveVolatile = std::remove_volatile<T>;
template <class T>
using RemoveVolatile_T = typename RemoveVolatile<T>::type;

template <class T>
using RemoveCV = std::remove_cv<T>;
template <class T>
using RemoveCV_T = typename RemoveCV<T>::type;

template <class T>
using RemoveReference = std::remove_reference<T>;
template <class T>
using RemoveReference_T = typename RemoveReference<T>::type;

template <class T>
using RemoveCVRef = std::remove_cvref<T>;
template <class T>
using RemoveCVRef_T = typename RemoveCVRef<T>::type;

template <class T>
using RemoveExtent = std::remove_extent<T>;
template <class T>
using RemoveExtent_T = typename RemoveExtent<T>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class T, T Val>
using IntegralConstant = std::integral_constant<T, Val>;

template <class T>
using PointerTraits = std::pointer_traits<T>;

template <class T>
using AllocatorTraits = std::allocator_traits<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <bool Test, class T = void>
using EnableIf = std::enable_if<Test, T>;
template <bool Test, class T = void>
using EnableIf_T = typename EnableIf<Test, T>::type;

template <bool Test, class T, class F>
using Conditional = std::conditional<Test, T, F>;
template <bool Test, class T, class F>
using Conditional_T = typename Conditional<Test, T, F>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class T, class Cat = PartialOrdering>
concept ThreeWayComparable = std::three_way_comparable<T, Cat>;
template <class T, class F, class Cat = PartialOrdering>
concept ThreeWayComparableWith = std::three_way_comparable_with<T, F, Cat>;
template <class T, class F = T>
using CompareThreeWayResult_T = std::compare_three_way_result_t<T, F>;
template <class T, class F = T>
using CompareThreeWayResult = typename std::compare_three_way_result<T, F>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <class... T>
using CommonType = std::common_type<T...>;
template <class... T>
using CommonType_T = typename CommonType<T...>::type;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr inline bool IsConstantEvaluated()noexcept{ return std::is_constant_evaluated(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if _HAS_CXX23
template <class Pointer, class SizeType = std::size_t>
using AllocationResult = std::allocation_result<Pointer, SizeType>;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

