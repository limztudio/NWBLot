// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "limit.h"
#include "expected.h"
#include "overflow.h"
#include "span.h"
#include "type_properties.h"

#include <algorithm>
#include <numeric>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename InputIt, typename Predicate>
constexpr InputIt FindIf(InputIt first, InputIt last, Predicate&& pred){
    return std::find_if(first, last, Forward<Predicate>(pred));
}

template<typename TContainer, typename TPredicate>
[[nodiscard]] inline auto FindIfPointer(TContainer& sources, TPredicate&& predicate)noexcept(noexcept(FindIf(sources.begin(), sources.end(), Forward<TPredicate>(predicate))) && noexcept(sources.end()) && noexcept(&*sources.begin())) -> decltype(&*sources.begin()){
    const auto found = FindIf(sources.begin(), sources.end(), Forward<TPredicate>(predicate));
    return found == sources.end() ? nullptr : &*found;
}


namespace AlgorithmDetail{


template<typename T, typename Predicate, typename State, typename Output>
void VisitPointerDependency(const usize index, const Span<const T* const> items, const Predicate& dependsOn, State& state, Output& outSorted){
    if(state[index] != 0u)
        return;
    state[index] = 1u;
    const T* current = items[index];
    if(current){
        for(usize j = 0u; j < items.size(); ++j){
            if(j != index && state[j] == 0u && items[j] && dependsOn(*current, *items[j]))
                VisitPointerDependency(j, items, dependsOn, state, outSorted);
        }
    }
    state[index] = 2u;
    outSorted.push_back(current);
}


};


// State must contain at least one zero for each item.
template<typename T, typename Predicate, typename State, typename Output>
void TopologicallySortPointers(const Span<const T* const> items, const Predicate& dependsOn, State& state, Output& outSorted){
    outSorted.clear();
    if(items.empty())
        return;
    outSorted.reserve(items.size());
    for(usize i = 0u; i < items.size(); ++i)
        AlgorithmDetail::VisitPointerDependency(i, items, dependsOn, state, outSorted);
}


// The mask must contain one entry for each element.
template<typename Container>
void CompactByMask(const Span<const u8> selected, Container& values)noexcept(
    noexcept(values.size()) && noexcept(values[usize{}] = Move(values[usize{}])) && noexcept(values.erase(values.begin() + usize{}, values.end()))
){
    usize next = 0u;
    for(usize i = 0u; i < values.size(); ++i){
        if(!selected[i])
            continue;
        if(next != i)
            values[next] = Move(values[i]);
        ++next;
    }
    values.erase(values.begin() + next, values.end());
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

template<typename ForwardIt, typename T, typename Compare>
constexpr ForwardIt UpperBound(ForwardIt first, ForwardIt last, const T& value, Compare&& compare){
    return std::upper_bound(first, last, value, Forward<Compare>(compare));
}

[[nodiscard]] inline constexpr usize LowerBoundIndex(const Span<const usize> values, const usize value)noexcept{
    usize first = 0u;
    usize count = values.size();
    while(count > 0u){
        const usize step = count / 2u;
        const usize mid = first + step;
        if(values[mid] < value){
            first = mid + 1u;
            count -= step + 1u;
        }
        else
            count = step;
    }
    return first;
}

template<typename Sequence, typename Predicate>
void RetainIf(Sequence& values, const Predicate& selected)noexcept(
    noexcept(static_cast<b8>(DeclVal<usize&>() < values.size()))
    && noexcept(static_cast<b8>(!selected(values[DeclVal<usize&>()])))
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
    requires(IsIntegral_V<T> && !IsSigned_V<T> && !IsSame_V<T, b8>)
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
[[nodiscard]] constexpr b8 IsSupportedAlignmentValue(const T align)noexcept(IsArithmetic_V<T>){
    return align <= static_cast<T>(1) || (align & (align - static_cast<T>(1))) == static_cast<T>(0);
}

template<typename T>
[[nodiscard]] constexpr b8 IsAligned(const T value, const T alignment)noexcept(IsArithmetic_V<T>){
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

// Uses wrapping numerator arithmetic and requires a nonzero divisor.
template<typename ValueT, typename DivisorT>
    requires(IsIntegral_V<ValueT> && IsIntegral_V<DivisorT>)
[[nodiscard]] constexpr auto DivideUpWrapping(const ValueT value, const DivisorT divisor)noexcept -> decltype((value + divisor - 1u) / divisor){
    return (value + divisor - 1u) / divisor;
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


[[nodiscard]] inline constexpr Expected<u32> AlignU32PowerOfTwoChecked(const u32 value, const u32 alignment)noexcept{
    if(alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        return MakeUnexpected(Failure{});
    return AlignUpChecked(value, alignment);
}

[[nodiscard]] inline constexpr Expected<usize> AlignUpCheckedSize(const usize value, const usize alignment)noexcept{
    return AlignUpChecked(value, alignment);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

