// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "array.h"
#include "expected.h"
#include "optional.h"
#include "generic.h"
#include "algorithm.h"
#include "limit.h"
#include "type.h"
#include "simd/memory.h"

#include <functional>
#include <tuple>
#include <forward_list>
#include <list>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include <queue>
#include <memory>

#include <tbb/concurrent_unordered_set.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_queue.h>

#include <robin_set.h>
#include <robin_map.h>

#include "compressed_pair.h"
#include "fixed_vector.h"
#include "basic_string.h"
#include "container/adaptor.h"
#include "filesystem.h"
#include "name.h"
#include "sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FUNC>
using Function = std::function<FUNC>;

using InPlaceType = std::in_place_t;
inline constexpr InPlaceType s_InPlace{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arg0, typename Arg1>
using Pair = CompressedPair<Arg0, Arg1>;
template<typename Arg0, typename Arg1>
constexpr auto MakePair(Arg0&& arg0, Arg1&& arg1)noexcept(noexcept(Pair<RemoveReferenceWrapper_T<Decay_T<Arg0>>, RemoveReferenceWrapper_T<Decay_T<Arg1>>>(Forward<Arg0>(arg0), Forward<Arg1>(arg1)))){
    using Arg0Type = RemoveReferenceWrapper_T<Decay_T<Arg0>>;
    using Arg1Type = RemoveReferenceWrapper_T<Decay_T<Arg1>>;
    return Pair<Arg0Type, Arg1Type>(Forward<Arg0>(arg0), Forward<Arg1>(arg1));
}

template<typename... Args>
using Tuple = std::tuple<Args...>;
template<typename... Args>
constexpr auto MakeTuple(Args&&... args)noexcept(noexcept(std::make_tuple(Forward<Args>(args)...))){ return std::make_tuple(Forward<Args>(args)...); }

template<typename... Args>
constexpr auto ForwardAsTuple(Args&&... args)noexcept{ return Tuple<Args&&...>(Forward<Args>(args)...); }

template<size_t I, typename... Args>
constexpr auto& Get(Tuple<Args...>& t)noexcept{ return std::get<I>(t); }

template<size_t I, typename... Args>
constexpr const auto& Get(const Tuple<Args...>& t)noexcept{ return std::get<I>(t); }

template<typename... Args>
constexpr auto Tie(Args&... args)noexcept{ return std::tie(args...); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT, typename Hash = Hasher<T>, typename Equal = EqualTo<T>> using ParallelHashSet = tbb::concurrent_unordered_set<T, Hash, Equal, ContainerDetail::ArenaCacheAlignedAllocatorFor_T<T, ArenaT>>;

template<typename T, typename V, typename ArenaT, typename Hash = Hasher<T>, typename Equal = EqualTo<T>>
using ParallelHashMap = tbb::concurrent_unordered_map< T, V, Hash, Equal, ContainerDetail::ArenaCacheAlignedAllocatorFor_T<typename tbb::concurrent_unordered_map<T, V, Hash, Equal>::value_type, ArenaT> >;

template<typename T, typename ArenaT>
using ParallelVector = tbb::concurrent_vector<T, ContainerDetail::ArenaCacheAlignedAllocatorFor_T<T, ArenaT>>;

template<typename T, typename ArenaT>
using ParallelQueue = tbb::concurrent_queue<T, ContainerDetail::ArenaCacheAlignedAllocatorFor_T<T, ArenaT>>;

template<typename T, typename ArenaT>
using ParallelBlockQueue = tbb::concurrent_bounded_queue<T, ContainerDetail::ArenaCacheAlignedAllocatorFor_T<T, ArenaT>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT, typename Hash = Hasher<T>, typename Equal = EqualTo<T>> using HashSet = tsl::robin_set<T, Hash, Equal, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;

template<typename T, typename V, typename ArenaT, typename Hash = Hasher<T>, typename Equal = EqualTo<T>> using HashMap = tsl::robin_map<T, V, Hash, Equal, ContainerDetail::ArenaAllocatorFor_T<Pair<T, V>, ArenaT>>;

template<
    typename K,
    typename V,
    typename ArenaT,
    ArenaT& (*DefaultArena)(),
    typename Hash = Hasher<K>, typename Equal = EqualTo<K> > using DefaultHashMap = tsl::robin_map< K, V, Hash, Equal, ContainerDetail::DefaultArenaAllocatorFor_T<Pair<K, V>, ArenaT, DefaultArena> >;

template<typename T, typename ArenaT>
using ForwardList = std::forward_list<T, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;

template<typename T, typename ArenaT>
using List = std::list<T, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;

template<typename T, typename ArenaT>
using Vector = std::vector<T, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;

template<typename T>
using InteropVector = std::vector<T>;

template<typename T, typename ArenaT, ArenaT& (*DefaultArena)()>
using DefaultVector = std::vector<T, ContainerDetail::DefaultArenaAllocatorFor_T<T, ArenaT, DefaultArena>>;

template<typename T, typename ArenaT>
using Set = std::set<T, std::less<T>, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;

template<typename T, typename V, typename ArenaT>
using Map = std::map<T, V, std::less<T>, ContainerDetail::ArenaAllocatorFor_T<typename std::map<T, V>::value_type, ArenaT>>;

template<typename T, typename ArenaT>
using Queue = std::queue<T, std::deque<T, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>>;

template<typename T, typename ArenaT>
using Deque = std::deque<T, ContainerDetail::ArenaAllocatorFor_T<T, ArenaT>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename DestinationVector, typename SourceValue>
[[nodiscard]] inline Expected<usize> SourceAliasesDestination(const DestinationVector& destination, const SourceValue* sourceData, const usize sourceSize)noexcept(noexcept(static_cast<bool>(destination.empty() || sourceSize == 0u)) && noexcept(destination.data()) && noexcept(static_cast<bool>(DeclVal<const usize&>() >= destination.size()))){
    if(destination.empty() || sourceSize == 0u)
        return MakeUnexpected(Failure{});

    const auto* const destinationData = destination.data();
    if(destinationData == nullptr || sourceData == nullptr)
        return MakeUnexpected(Failure{});

    const usize destinationBegin = reinterpret_cast<usize>(destinationData);
    const usize sourceBegin = reinterpret_cast<usize>(sourceData);
    if(sourceBegin < destinationBegin)
        return MakeUnexpected(Failure{});

    using DestinationValue = typename DestinationVector::value_type;
    const usize byteOffset = sourceBegin - destinationBegin;
    if((byteOffset % sizeof(DestinationValue)) != 0u)
        return MakeUnexpected(Failure{});

    const usize sourceOffset = byteOffset / sizeof(DestinationValue);
    if(sourceOffset >= destination.size())
        return MakeUnexpected(Failure{});

    return sourceOffset;
}

template<typename DestinationVector, typename SourceVector>
[[nodiscard]] inline Expected<usize> SourceAliasesDestination(const DestinationVector& destination, const SourceVector& source)noexcept(noexcept(SourceAliasesDestination(destination, source.data(), source.size()))){
    return SourceAliasesDestination(destination, source.data(), source.size());
}

template<typename Container>
inline void ReserveGrowingCapacity(Container& container, const usize requiredCapacity)noexcept([]()consteval noexcept{
    if constexpr(requires(const Container& c){ c.capacity(); }){
        constexpr bool s_ReserveIsNothrow =
            noexcept(static_cast<bool>(DeclVal<const usize&>() <= DeclVal<Container&>().capacity()))
            && noexcept(static_cast<usize>(DeclVal<Container&>().capacity()))
            && noexcept(DeclVal<Container&>().reserve(DeclVal<usize&>()));
        if constexpr(requires(const Container& c){ c.max_size(); })
            return s_ReserveIsNothrow && noexcept(static_cast<bool>(DeclVal<usize&>() > DeclVal<Container&>().max_size()));
        else
            return s_ReserveIsNothrow;
    }
    else
        return noexcept(DeclVal<Container&>().reserve(DeclVal<const usize&>()));
}()){
    if constexpr(requires(const Container& c){ c.capacity(); }){
        if(requiredCapacity <= container.capacity())
            return;

        usize nextCapacity = NextGrowingCapacity(static_cast<usize>(container.capacity()), requiredCapacity);

        if constexpr(requires(const Container& c){ c.max_size(); }){
            if(nextCapacity > container.max_size())
                nextCapacity = requiredCapacity;
        }

        container.reserve(nextCapacity);
    }
    else
        container.reserve(requiredCapacity);
}

template<typename Container>
inline void ReserveAdditionalCapacity(Container& container, const usize additionalCount)noexcept(
    noexcept(DeclVal<void(*)(usize)noexcept>()(container.size()))
    && noexcept(ReserveGrowingCapacity(container, usize{}))
){
    if(additionalCount <= 1u)
        return;

    const usize currentSize = container.size();
    if(additionalCount > Limit<usize>::s_Max - currentSize)
        return;

    ReserveGrowingCapacity(container, currentSize + additionalCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename DestinationVector, typename SourceVector>
inline void AssignTriviallyCopyableVector(DestinationVector& destination, const SourceVector& source){
    using DestinationValue = typename DestinationVector::value_type;
    using SourceValue = typename SourceVector::value_type;
    static_assert(IsSame_V<DestinationValue, SourceValue>, "vector value types must match");
    static_assert(IsTriviallyCopyable_V<DestinationValue>, "vector value type must be trivially copyable");

    const usize sourceSize = source.size();
    if(source.empty()){
        destination.clear();
        return;
    }
    const auto aliasOffset = ContainerDetail::SourceAliasesDestination(destination, source);
    if(aliasOffset){
        const usize sourceOffset = *aliasOffset;
        NWB_ASSERT(sourceSize <= destination.size() - sourceOffset);
        if constexpr(requires(DestinationVector& d, usize n){ d.data(); d.resize(n); }){
            const usize byteCount = sourceSize * sizeof(DestinationValue);
            if(sourceOffset > 0u)
                std::memmove(destination.data(), destination.data() + sourceOffset, byteCount);
            destination.resize(sourceSize);
        }
        else{
            for(usize i = 0u; i < sourceSize; ++i)
                destination[i] = destination[sourceOffset + i];
            while(destination.size() > sourceSize)
                destination.pop_back();
        }
        return;
    }

    if constexpr(requires(DestinationVector& d, usize n){ d.data(); d.resize(n); } && requires(const SourceVector& s){ s.data(); }){
        destination.resize(sourceSize);
        if constexpr(alignof(DestinationValue) >= s_SIMDMemoryCopyAlignment)
            NWB_SIMD_MEMCPY(destination.data(), sourceSize * sizeof(DestinationValue), source.data(), sourceSize * sizeof(SourceValue));
        else
            NWB_MEMCPY(destination.data(), sourceSize * sizeof(DestinationValue), source.data(), sourceSize * sizeof(SourceValue));
    }
    else{
        ContainerDetail::ReserveGrowingCapacity(destination, sourceSize);
        destination.assign(source.begin(), source.end());
    }
}

template<typename DestinationVector, typename SourceVector>
inline void AppendTriviallyCopyableVector(DestinationVector& destination, const SourceVector& source){
    using DestinationValue = typename DestinationVector::value_type;
    using SourceValue = typename SourceVector::value_type;
    static_assert(IsSame_V<DestinationValue, SourceValue>, "vector value types must match");
    static_assert(IsTriviallyCopyable_V<DestinationValue>, "vector value type must be trivially copyable");

    if(source.empty())
        return;

    const usize destinationSize = destination.size();
    const usize sourceSize = source.size();
    NWB_ASSERT(sourceSize <= Limit<usize>::s_Max - destinationSize);
    const auto aliasOffset = ContainerDetail::SourceAliasesDestination(destination, source);
    if(aliasOffset){
        const usize sourceOffset = *aliasOffset;
        NWB_ASSERT(sourceSize <= destinationSize - sourceOffset);
        const usize requiredSize = destinationSize + sourceSize;
        ContainerDetail::ReserveGrowingCapacity(destination, requiredSize);
        if constexpr(requires(DestinationVector& d, usize n){ d.data(); d.resize(n); }){
            destination.resize(requiredSize);
            if constexpr(alignof(DestinationValue) >= s_SIMDMemoryCopyAlignment)
                NWB_SIMD_MEMCPY(destination.data() + destinationSize, sourceSize * sizeof(DestinationValue), destination.data() + sourceOffset, sourceSize * sizeof(DestinationValue));
            else
                NWB_MEMCPY(destination.data() + destinationSize, sourceSize * sizeof(DestinationValue), destination.data() + sourceOffset, sourceSize * sizeof(DestinationValue));
        }
        else{
            for(usize i = 0u; i < sourceSize; ++i)
                destination.push_back(destination[sourceOffset + i]);
        }
        return;
    }

    ContainerDetail::ReserveGrowingCapacity(destination, destinationSize + sourceSize);
    if constexpr(requires(DestinationVector& d, usize n){ d.data(); d.resize(n); } && requires(const SourceVector& s){ s.data(); }){
        destination.resize(destinationSize + sourceSize);
        if constexpr(alignof(DestinationValue) >= s_SIMDMemoryCopyAlignment)
            NWB_SIMD_MEMCPY(destination.data() + destinationSize, sourceSize * sizeof(DestinationValue), source.data(), sourceSize * sizeof(SourceValue));
        else
            NWB_MEMCPY(destination.data() + destinationSize, sourceSize * sizeof(DestinationValue), source.data(), sourceSize * sizeof(SourceValue));
    }
    else if constexpr(requires(DestinationVector& d, const SourceVector& s){ d.insert(d.end(), s.begin(), s.end()); }){
        destination.insert(destination.end(), source.begin(), source.end());
    }
    else{
        for(usize i = 0u; i < sourceSize; ++i)
            destination.push_back(source[i]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

