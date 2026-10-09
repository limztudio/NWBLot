// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "limit.h"
#include "expected.h"
#include "type_properties.h"

#include <algorithm>
#include <numeric>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename InputIt, typename Predicate>
constexpr InputIt FindIf(InputIt first, InputIt last, Predicate&& pred){
    return std::find_if(first, last, Forward<Predicate>(pred));
}

template<typename RandomIt>
constexpr void Sort(RandomIt first, RandomIt last){
    std::sort(first, last);
}

template<typename RandomIt, typename Compare>
constexpr void Sort(RandomIt first, RandomIt last, Compare&& compare){
    std::sort(first, last, Forward<Compare>(compare));
}

template<typename ForwardIt>
[[nodiscard]] constexpr ForwardIt Unique(ForwardIt first, ForwardIt last){
    return std::unique(first, last);
}

template<typename ForwardIt>
constexpr ForwardIt Rotate(ForwardIt first, ForwardIt middle, ForwardIt last){
    return std::rotate(first, middle, last);
}

template<typename InputIt, typename OutputIt, typename UnaryOp>
constexpr OutputIt Transform(InputIt first, InputIt last, OutputIt dFirst, UnaryOp&& op){
    return std::transform(first, last, dFirst, Forward<UnaryOp>(op));
}

template<typename ForwardIt, typename T>
constexpr void Replace(ForwardIt first, ForwardIt last, const T& oldValue, const T& newValue){
    std::replace(first, last, oldValue, newValue);
}

template<typename ForwardIt, typename T>
constexpr void Iota(ForwardIt first, ForwardIt last, T value){
    std::iota(first, last, value);
}

template<typename ForwardIt, typename T>
constexpr ForwardIt LowerBound(ForwardIt first, ForwardIt last, const T& value){
    return std::lower_bound(first, last, value);
}

template<typename ForwardIt, typename T, typename Compare>
constexpr ForwardIt LowerBound(ForwardIt first, ForwardIt last, const T& value, Compare&& compare){
    return std::lower_bound(first, last, value, Forward<Compare>(compare));
}

[[nodiscard]] constexpr usize NextGrowingCapacity(
    const usize currentCapacity,
    const usize requiredCapacity,
    const usize initialCapacity = 1u
)noexcept{
    usize capacity = currentCapacity > initialCapacity ? currentCapacity : initialCapacity;
    if(capacity == 0u)
        capacity = 1u;

    while(capacity < requiredCapacity){
        if(capacity > Limit<usize>::s_Max / 2u)
            return requiredCapacity;
        capacity *= 2u;
    }
    return capacity;
}

template<typename T>
constexpr T AlignUp(const T value, const T alignment)noexcept(IsArithmetic_V<T>){
    if(alignment == 0)
        return value;
    return value + (alignment - (value % alignment)) % alignment;
}

template<typename T>
constexpr T DivideUp(const T value, const T divisor)noexcept(IsArithmetic_V<T>){
    if(value == 0 || divisor == 0)
        return 0;
    return static_cast<T>(1) + ((value - static_cast<T>(1)) / divisor);
}

template<typename T>
[[nodiscard]] constexpr Expected<T> DivideUpChecked(const T value, const T divisor)noexcept(IsArithmetic_V<T>){
    if(divisor == 0)
        return MakeUnexpected(Failure{});

    return DivideUp(value, divisor);
}

template<typename T>
[[nodiscard]] constexpr Expected<T> AlignUpChecked(const T value, const T alignment)noexcept(IsArithmetic_V<T>){
    if(alignment == 0)
        return value;

    const T remainder = value % alignment;
    if(remainder == 0)
        return value;

    const T addend = alignment - remainder;
    if(value > Limit<T>::s_Max - addend)
        return MakeUnexpected(Failure{});

    return value + addend;
}

[[nodiscard]] constexpr Expected<u32> AlignUpU32Checked(const u32 value, const u32 alignment)noexcept{
    return AlignUpChecked(value, alignment);
}

[[nodiscard]] constexpr Expected<u64> AlignUpU64Checked(const u64 value, const u64 alignment)noexcept{
    return AlignUpChecked(value, alignment);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

