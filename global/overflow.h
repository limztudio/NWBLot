#pragma once


#include "type.h"
#include "limit.h"
#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr bool NegateOverflows(const TypeIdentity<T> value){
    if constexpr(IsSigned_V<T>)
        return value == Limit<T>::s_Min;
    else
        return value != T(0);
}

template<typename T>
[[nodiscard]] constexpr bool AddOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs){
    if constexpr(IsSigned_V<T>){
        if(rhs > T(0))
            return lhs > Limit<T>::s_Max - rhs;
        if(rhs < T(0))
            return lhs < Limit<T>::s_Min - rhs;
        return false;
    }
    else
        return lhs > Limit<T>::s_Max - rhs;
}

template<typename T>
[[nodiscard]] constexpr bool SubtractOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs){
    if constexpr(IsSigned_V<T>){
        if(rhs > T(0))
            return lhs < Limit<T>::s_Min + rhs;
        if(rhs < T(0))
            return lhs > Limit<T>::s_Max + rhs;
        return false;
    }
    else
        return lhs < rhs;
}

template<typename T>
[[nodiscard]] constexpr bool MultiplyOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs){
    if(lhs == T(0) || rhs == T(0))
        return false;

    if constexpr(IsSigned_V<T>){
        if(lhs > T(0)){
            if(rhs > T(0))
                return lhs > Limit<T>::s_Max / rhs;
            return rhs < Limit<T>::s_Min / lhs;
        }

        if(rhs > T(0))
            return lhs < Limit<T>::s_Min / rhs;

        return rhs < Limit<T>::s_Max / lhs;
    }
    else
        return lhs > Limit<T>::s_Max / rhs;
}

template<typename T>
[[nodiscard]] constexpr bool DivideOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs){
    if constexpr(IsSigned_V<T>)
        return lhs == Limit<T>::s_Min && rhs == T(-1);
    else
        return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr T AddSaturating(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs){
    return AddOverflows<T>(lhs, rhs) ? Limit<T>::s_Max : static_cast<T>(lhs + rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

