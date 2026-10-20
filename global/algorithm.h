// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "limit.h"
#include "expected.h"
#include "overflow.h"
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

template<typename Sequence, typename Predicate>
void RetainIf(Sequence& values, const Predicate& selected)noexcept(
    noexcept(static_cast<bool>(DeclVal<usize&>() < values.size()))
    && noexcept(static_cast<bool>(!selected(values[DeclVal<usize&>()])))
    && noexcept(values[DeclVal<usize&>()] = values[DeclVal<usize&>()])
    && noexcept(values.resize(DeclVal<usize&>()))
){
    usize retainedCount = 0u;
    for(usize index = 0u; index < values.size(); ++index){
        if(!selected(values[index]))
            continue;
        if(retainedCount != index)
            values[retainedCount] = values[index];
        ++retainedCount;
    }
    values.resize(retainedCount);
}

template<typename DestinationT, typename SourceT>
inline void AssignVector(DestinationT& destination, const SourceT& source)noexcept(noexcept(destination.reserve(source.size())) && noexcept(destination.assign(source.begin(), source.end()))){
    destination.reserve(source.size());
    destination.assign(source.begin(), source.end());
}

[[nodiscard]] constexpr usize NextGrowingCapacity(const usize currentCapacity, const usize requiredCapacity, const usize initialCapacity = 1u)noexcept{
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
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, bool>)
[[nodiscard]] constexpr Expected<T> CommonAlignmentChecked(const T firstAlignment, const T secondAlignment)noexcept{
    if(firstAlignment == 0u || secondAlignment == 0u)
        return MakeUnexpected(Failure{});
    T first = firstAlignment;
    T second = secondAlignment;
    while(second != 0u){
        const T remainder = static_cast<T>(first % second);
        first = second;
        second = remainder;
    }
    return TryMultiply<T>(static_cast<T>(firstAlignment / first), secondAlignment);
}

template<typename T>
[[nodiscard]] constexpr bool IsSupportedAlignmentValue(const T align)noexcept(IsArithmetic_V<T>){
    return align <= static_cast<T>(1) || (align & (align - static_cast<T>(1))) == static_cast<T>(0);
}

template<typename T>
[[nodiscard]] constexpr bool IsAligned(const T value, const T alignment)noexcept(IsArithmetic_V<T>){
    return alignment == 0 || value % alignment == 0;
}

template<typename T>
[[nodiscard]] constexpr Expected<T> AlignUpPowerOfTwoChecked(const T value, const T alignment)noexcept(IsArithmetic_V<T>){
    if(alignment == 0)
        return value;

    const T mask = alignment - static_cast<T>(1);
    if(value > Limit<T>::s_Max - mask)
        return MakeUnexpected(Failure{});

    return (value + mask) & ~mask;
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

// Least common multiple of two nonzero alignments; zero inputs and overflow fail.
template<typename T>
[[nodiscard]] constexpr Expected<T> CommonAlignmentChecked(const T firstAlignment, const T secondAlignment)noexcept(IsArithmetic_V<T>){
    if(firstAlignment == 0 || secondAlignment == 0)
        return MakeUnexpected(Failure{});

    T first = firstAlignment;
    T second = secondAlignment;
    while(second != 0){
        const T remainder = first % second;
        first = second;
        second = remainder;
    }
    const T scale = firstAlignment / first;
    if(scale > Limit<T>::s_Max / secondAlignment)
        return MakeUnexpected(Failure{});
    return scale * secondAlignment;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

