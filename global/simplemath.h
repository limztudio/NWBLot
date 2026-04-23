// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr const T& Min(const T& a, const T& b){ return (a < b) ? a : b; }
template<typename T, typename Compare>
[[nodiscard]] constexpr const T& Min(const T& a, const T& b, Compare comp){ return comp(a, b) ? a : b; }
template<typename T>
[[nodiscard]] constexpr T Min(std::initializer_list<T> list){ return *std::min_element(list.begin(), list.end()); }
template<typename T, typename Compare>
[[nodiscard]] constexpr T Min(std::initializer_list<T> list, Compare comp){
    return *std::min_element(list.begin(), list.end(), comp);
}

template<typename T>
[[nodiscard]] constexpr const T& Max(const T& a, const T& b){ return (a > b) ? a : b; }
template<typename T, typename Compare>
[[nodiscard]] constexpr const T& Max(const T& a, const T& b, Compare comp){ return comp(a, b) ? b : a; }
template<typename T>
[[nodiscard]] constexpr T Max(std::initializer_list<T> list){ return *std::max_element(list.begin(), list.end()); }
template<typename T, typename Compare>
[[nodiscard]] constexpr T Max(std::initializer_list<T> list, Compare comp){
    return *std::max_element(list.begin(), list.end(), comp);
}

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T Abs(const T value){ return value < static_cast<T>(0) ? -value : value; }

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T Saturate(const T value){
    if(value < static_cast<T>(0))
        return static_cast<T>(0);
    if(value > static_cast<T>(1))
        return static_cast<T>(1);
    return value;
}

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T FloorLog2(const T x){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }
template<typename T>
[[nodiscard]] constexpr NWB_INLINE T CeilLog2(const T x){ return (x == 1) ? 0 : (FloorLog2<T>(x - 1) + 1); }

template<typename T>
[[nodiscard]] NWB_INLINE T Floor(const T value){
    using std::floor;
    return static_cast<T>(floor(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Ceil(const T value){
    using std::ceil;
    return static_cast<T>(ceil(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Sqrt(const T value){
    using std::sqrt;
    return static_cast<T>(sqrt(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Sin(const T value){
    using std::sin;
    return static_cast<T>(sin(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Cos(const T value){
    using std::cos;
    return static_cast<T>(cos(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE bool IsFinite(const T value){
    using std::isfinite;
    return isfinite(value);
}

template<typename T>
[[nodiscard]] constexpr bool AddNoOverflow(const T lhs, const T rhs, T& outResult){
    if(lhs > ((std::numeric_limits<T>::max)() - rhs))
        return false;

    outResult = lhs + rhs;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

