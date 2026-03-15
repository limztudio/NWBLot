// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <algorithm>
#include <cmath>
#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
constexpr const T& Min(const T& a, const T& b){ return (a < b) ? a : b; }
template<typename T, typename Compare>
constexpr const T& Min(const T& a, const T& b, Compare comp){ return comp(a, b) ? a : b; }
template<typename T>
constexpr T Min(std::initializer_list<T> list){ return *std::min_element(list.begin(), list.end()); }
template<typename T, typename Compare>
constexpr T Min(std::initializer_list<T> list, Compare comp){ return *std::min_element(list.begin(), list.end(), comp); }

template<typename T>
constexpr const T& Max(const T& a, const T& b){ return (a > b) ? a : b; }
template<typename T, typename Compare>
constexpr const T& Max(const T& a, const T& b, Compare comp){ return comp(a, b) ? b : a; }
template<typename T>
constexpr T Max(std::initializer_list<T> list){ return *std::max_element(list.begin(), list.end()); }
template<typename T, typename Compare>
constexpr T Max(std::initializer_list<T> list, Compare comp){ return *std::max_element(list.begin(), list.end(), comp); }

template<typename T>
constexpr inline T FloorLog2(T x){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }
template<typename T>
constexpr inline T CeilLog2(T x){ return (x == 1) ? 0 : (FloorLog2<T>(x - 1) + 1); }

template<typename T>
[[nodiscard]] inline T Floor(const T value){
    using std::floor;
    return static_cast<T>(floor(value));
}

template<typename T>
[[nodiscard]] inline T Ceil(const T value){
    using std::ceil;
    return static_cast<T>(ceil(value));
}

template<typename T>
[[nodiscard]] inline bool IsFinite(const T value){
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

