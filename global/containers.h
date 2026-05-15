// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "limit.h"
#include "type.h"

#include <functional>
#include <optional>

#include <tuple>
#include <array>
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
#include "filesystem.h"
#include "name.h"
#include "sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Core::Alloc{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
class GeneralAllocator;

template<typename T>
class CacheAlignedAllocator;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FUNC>
using Function = std::function<FUNC>;

template<typename T>
using Optional = std::optional<T>;

using InPlaceType = std::in_place_t;
inline constexpr InPlaceType s_InPlace{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arg0, typename Arg1>
using Pair = CompressedPair<Arg0, Arg1>;
template<typename Arg0, typename Arg1>
constexpr auto MakePair(Arg0&& arg0, Arg1&& arg1){
    using Arg0Type = RemoveReferenceWrapper_T<Decay_T<Arg0>>;
    using Arg1Type = RemoveReferenceWrapper_T<Decay_T<Arg1>>;
    return Pair<Arg0Type, Arg1Type>(Forward<Arg0>(arg0), Forward<Arg1>(arg1));
}

template<typename... Args>
using Tuple = std::tuple<Args...>;
template<typename... Args>
constexpr auto MakeTuple(Args&&... args){ return std::make_tuple(Forward<Args>(args)...); }
template<size_t I, typename... Args>
constexpr auto& Get(Tuple<Args...>& t){ return std::get<I>(t); }
template<size_t I, typename... Args>
constexpr const auto& Get(const Tuple<Args...>& t){ return std::get<I>(t); }
template<typename... Args>
constexpr auto Tie(Args&... args){ return std::tie(args...); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using ParallelHashSet = tbb::concurrent_unordered_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<T, V>>>
using ParallelHashMap = tbb::concurrent_unordered_map<T, V, Hash, Equal, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelVector = tbb::concurrent_vector<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelQueue = tbb::concurrent_queue<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelBlockQueue = tbb::concurrent_bounded_queue<T, Alloc>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using HashSet = tsl::robin_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<T, V>>>
using HashMap = tsl::robin_map<T, V, Hash, Equal, Alloc>;

template<typename T, usize N>
using Array = std::array<T, N>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using ForwardList = std::forward_list<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using List = std::list<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Vector = std::vector<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Set = std::set<T, std::less<T>, Alloc>;

template<typename T, typename V, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<T, V>>>
using Map = std::map<T, V, std::less<T>, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Queue = std::queue<T, std::deque<T, Alloc>>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Deque = std::deque<T, Alloc>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ContainerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename DestinationVector, typename SourceVector>
[[nodiscard]] inline bool SourceAliasesDestination(const DestinationVector& destination, const SourceVector& source, usize& outSourceOffset){
    outSourceOffset = 0u;
    if(destination.empty() || source.empty())
        return false;

    const auto* const destinationData = destination.data();
    const auto* const sourceData = source.data();
    if(destinationData == nullptr || sourceData == nullptr)
        return false;

    const usize destinationBegin = reinterpret_cast<usize>(destinationData);
    const usize sourceBegin = reinterpret_cast<usize>(sourceData);
    if(sourceBegin < destinationBegin)
        return false;

    using DestinationValue = typename DestinationVector::value_type;
    const usize byteOffset = sourceBegin - destinationBegin;
    if((byteOffset % sizeof(DestinationValue)) != 0u)
        return false;

    const usize sourceOffset = byteOffset / sizeof(DestinationValue);
    if(sourceOffset >= destination.size())
        return false;

    outSourceOffset = sourceOffset;
    return true;
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
    usize sourceOffset = 0u;
    if(ContainerDetail::SourceAliasesDestination(destination, source, sourceOffset)){
        NWB_ASSERT(sourceSize <= destination.size() - sourceOffset);
        if(sourceSize > destination.size() - sourceOffset)
            return;
        for(usize i = 0u; i < sourceSize; ++i)
            destination[i] = destination[sourceOffset + i];
        while(destination.size() > sourceSize)
            destination.pop_back();
        return;
    }

    destination.assign(source.begin(), source.end());
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
    usize sourceOffset = 0u;
    if(ContainerDetail::SourceAliasesDestination(destination, source, sourceOffset)){
        NWB_ASSERT(sourceSize <= destinationSize - sourceOffset);
        if(sourceSize > destinationSize - sourceOffset)
            return;
        destination.reserve(destinationSize + sourceSize);
        for(usize i = 0u; i < sourceSize; ++i)
            destination.push_back(destination[sourceOffset + i]);
        return;
    }

    destination.reserve(destinationSize + sourceSize);
    if constexpr(requires(DestinationVector& d, const SourceVector& s){ d.insert(d.end(), s.begin(), s.end()); }){
        destination.insert(destination.end(), source.begin(), source.end());
    }
    else{
        for(usize i = 0u; i < sourceSize; ++i)
            destination.push_back(source[i]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

