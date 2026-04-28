// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "limit.h"
#include "type_properties.h"

#include <algorithm>


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
constexpr ForwardIt Rotate(ForwardIt first, ForwardIt middle, ForwardIt last){
    return std::rotate(first, middle, last);
}

template<typename ForwardIt, typename T>
constexpr ForwardIt LowerBound(ForwardIt first, ForwardIt last, const T& value){
    return std::lower_bound(first, last, value);
}

template<typename ForwardIt, typename T, typename Compare>
constexpr ForwardIt LowerBound(ForwardIt first, ForwardIt last, const T& value, Compare&& compare){
    return std::lower_bound(first, last, value, Forward<Compare>(compare));
}

template<typename T>
constexpr T AlignUp(const T value, const T alignment){
    if(alignment == 0)
        return value;
    return value + (alignment - (value % alignment)) % alignment;
}

template<typename T>
constexpr bool AlignUpChecked(const T value, const T alignment, T& outValue){
    if(alignment == 0){
        outValue = value;
        return true;
    }

    const T remainder = value % alignment;
    if(remainder == 0){
        outValue = value;
        return true;
    }

    const T addend = alignment - remainder;
    if(value > Limit<T>::s_Max - addend)
        return false;

    outValue = value + addend;
    return true;
}

constexpr u32 AlignUpU32(const u32 value, const u32 alignment){
    return AlignUp(value, alignment);
}

constexpr bool AlignUpU32Checked(const u32 value, const u32 alignment, u32& outValue){
    return AlignUpChecked(value, alignment, outValue);
}

constexpr bool AlignUpU64Checked(const u64 value, const u64 alignment, u64& outValue){
    return AlignUpChecked(value, alignment, outValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

