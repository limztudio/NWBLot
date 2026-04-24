// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

