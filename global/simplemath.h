// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <algorithm>


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

