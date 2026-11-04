// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include "expected.h"
#include "limit.h"
#include "type_borrow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr b8 NegateOverflows(const TypeIdentity<T> value)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSigned_V<T>)
        return value == Limit<T>::s_Min;
    else
        return value != T(0);
}

template<typename T>
[[nodiscard]] constexpr b8 AddOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
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
[[nodiscard]] constexpr Expected<T> AddNoOverflow(const T lhs, const T rhs)noexcept(IsArithmetic_V<T>){
    if(AddOverflows<T>(lhs, rhs))
        return MakeUnexpected(Failure{});

    return static_cast<T>(lhs + rhs);
}

[[nodiscard]] inline constexpr Expected<u64> AddSignedOffset(const u64 base, const i64 offset)noexcept{
    if(offset < 0){
        const u64 distance = static_cast<u64>(-(offset + 1)) + 1u;
        if(distance > base)
            return MakeUnexpected(Failure{});
        return base - distance;
    }
    const u64 distance = static_cast<u64>(offset);
    if(distance > Limit<u64>::s_Max - base)
        return MakeUnexpected(Failure{});
    return base + distance;
}

[[nodiscard]] inline constexpr Expected<i64> SignedDifference(const u64 value, const u64 prior)noexcept{
    constexpr u64 s_MaxPositive = static_cast<u64>(Limit<i64>::s_Max);
    if(value >= prior){
        const u64 magnitude = value - prior;
        if(magnitude > s_MaxPositive)
            return MakeUnexpected(Failure{});
        return static_cast<i64>(magnitude);
    }
    const u64 magnitude = prior - value;
    constexpr u64 s_MaxNegative = s_MaxPositive + 1u;
    if(magnitude > s_MaxNegative)
        return MakeUnexpected(Failure{});
    return magnitude == s_MaxNegative ? Limit<i64>::s_Min : -static_cast<i64>(magnitude);
}

[[nodiscard]] inline b8 AccumulateBoundedCount(const u64 value, const u64 maximum, u64& total)noexcept{
    if(total > maximum || value > maximum - total)
        return false;
    total += value;
    return true;
}

[[nodiscard]] inline b8 UnsignedRangesOverlap(u64 firstOffsetBytes, u64 firstSizeBytes, u64 secondOffsetBytes, u64 secondSizeBytes)noexcept{
    if(firstSizeBytes == 0 || secondSizeBytes == 0)
        return false;
    if(firstSizeBytes > Limit<u64>::s_Max - firstOffsetBytes || secondSizeBytes > Limit<u64>::s_Max - secondOffsetBytes)
        return true;

    const u64 firstEnd = firstOffsetBytes + firstSizeBytes;
    const u64 secondEnd = secondOffsetBytes + secondSizeBytes;
    return firstOffsetBytes < secondEnd && secondOffsetBytes < firstEnd;
}


template<typename T>
[[nodiscard]] constexpr b8 SubtractOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
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
[[nodiscard]] constexpr b8 MultiplyOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
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
[[nodiscard]] constexpr Expected<T> TryMultiply(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
    if constexpr(IsIntegral_V<T> && !IsSame_V<T, b8>){
        T value;
        if(__builtin_mul_overflow(lhs, rhs, &value))
            return MakeUnexpected(Failure{});
        return value;
    }
    else{
        if(MultiplyOverflows<T>(lhs, rhs))
            return MakeUnexpected(Failure{});

        return static_cast<T>(lhs * rhs);
    }
}

template<typename T>
[[nodiscard]] constexpr b8 DivideOverflows(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
    if constexpr(IsSigned_V<T>)
        return lhs == Limit<T>::s_Min && rhs == T(-1);
    else
        return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr Expected<u64> ComputeStridedRangeByteSize(const u32 elementCount, const u64 stride, const u64 elementSize)noexcept{
    if(elementCount == 0u)
        return u64{0u};
    const u64 spanCount = static_cast<u64>(elementCount - 1u);
    if(stride != 0u && spanCount > (Limit<u64>::s_Max - elementSize) / stride)
        return MakeUnexpected(Failure{});
    return spanCount * stride + elementSize;
}

[[nodiscard]] inline constexpr u64 MultiplySaturatingU64(const u64 lhs, const u64 rhs)noexcept{
    return MultiplyOverflows<u64>(lhs, rhs) ? Limit<u64>::s_Max : lhs * rhs;
}

template<typename T>
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8>)
[[nodiscard]] inline constexpr T SubtractSaturating(const T value, const T amount)noexcept{
    return value >= amount ? value - amount : T(0);
}

template<typename T>
[[nodiscard]] constexpr T AddSaturating(const TypeIdentity<T> lhs, const TypeIdentity<T> rhs)noexcept(IsArithmetic_V<T>){
    if(AddOverflows<T>(lhs, rhs)){
        if constexpr(IsSigned_V<T>)
            return rhs < T(0) ? Limit<T>::s_Min : Limit<T>::s_Max;
        else
            return Limit<T>::s_Max;
    }
    return static_cast<T>(lhs + rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

