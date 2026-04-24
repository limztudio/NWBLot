// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "limit.h"
#include "type.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>


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
[[nodiscard]] NWB_INLINE T ModF(const T value, T* outInteger){
    using std::modf;
    return static_cast<T>(modf(value, outInteger));
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
[[nodiscard]] NWB_INLINE T ASin(const T value){
    using std::asin;
    return static_cast<T>(asin(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T ATan2(const T y, const T x){
    using std::atan2;
    return static_cast<T>(atan2(y, x));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Exp(const T value){
    using std::exp;
    return static_cast<T>(exp(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Exp2(const T value){
    using std::exp2;
    return static_cast<T>(exp2(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Log(const T value){
    using std::log;
    return static_cast<T>(log(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Log2(const T value){
    using std::log2;
    return static_cast<T>(log2(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Log10(const T value){
    using std::log10;
    return static_cast<T>(log10(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Pow(const T base, const T exponent){
    using std::pow;
    return static_cast<T>(pow(base, exponent));
}

template<typename T>
[[nodiscard]] NWB_INLINE T SinH(const T value){
    using std::sinh;
    return static_cast<T>(sinh(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T CosH(const T value){
    using std::cosh;
    return static_cast<T>(cosh(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T TanH(const T value){
    using std::tanh;
    return static_cast<T>(tanh(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE bool IsFinite(const T value){
    using std::isfinite;
    return isfinite(value);
}

template<typename T>
[[nodiscard]] NWB_INLINE bool IsNaN(const T value){
    using std::isnan;
    return isnan(value);
}

template<typename T>
[[nodiscard]] NWB_INLINE bool SignBit(const T value){
    using std::signbit;
    return signbit(value);
}

template<typename T>
[[nodiscard]] constexpr bool AddNoOverflow(const T lhs, const T rhs, T& outResult){
    if(lhs > (Limit<T>::s_Max - rhs))
        return false;

    outResult = lhs + rhs;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

