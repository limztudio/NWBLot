// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "limit.h"
#include "span.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Admission uses a u64 bound; accumulation keeps the unsigned input type's arithmetic.
template<typename T>
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8> && sizeof(T) <= sizeof(u64))
[[nodiscard]] inline Expected<T> TrySumUnsignedWithU64Limit(const Span<const T> values)noexcept{
    T total = 0u;
    for(const T value : values){
        if(static_cast<u64>(value) > Limit<u64>::s_Max - static_cast<u64>(total))
            return MakeUnexpected(Failure{});
        total += value;
    }
    return total;
}

[[nodiscard]] inline Expected<u64> TrySumNonzeroU64(const Span<const u64> values)noexcept{
    u64 total = 0u;
    for(const u64 value : values){
        if(value == 0u || value > Limit<u64>::s_Max - total)
            return MakeUnexpected(Failure{});
        total += value;
    }
    return total;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

