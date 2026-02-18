// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "type.h"

#include <functional>

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


namespace NWB{
    namespace Core{
        namespace Alloc{
            template<typename T>
            class GeneralAllocator;

            template<typename T>
            class CacheAlignedAllocator;
        };
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename FUNC>
using Function = std::function<FUNC>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Arg0, typename Arg1>
using Pair = CompressedPair<Arg0, Arg1>;
template<typename Arg0, typename Arg1>
constexpr auto MakePair(Arg0&& arg0, Arg1&& arg1){
    using Arg0Type = RemoveReferenceWrapper_T<Decay_T<Arg0>>;
    using Arg1Type = RemoveReferenceWrapper_T<Decay_T<Arg1>>;
    return Pair<Arg0Type, Arg1Type>(Forward<Arg0>(arg0), Forward<Arg1>(arg1));
}

template<typename Arg0, typename... Args>
using Tuple = std::tuple<Arg0, Args...>;
template<typename... Args>
constexpr auto MakeTuple(Args&&... args){ return std::make_tuple(Forward<Args>(args)...); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using ParallelHashSet = tbb::concurrent_unordered_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<const T, V>>>
using ParallelHashMap = tbb::concurrent_unordered_map<T, V, Hash, Equal, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelVector = tbb::concurrent_vector<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelQueue = tbb::concurrent_queue<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::CacheAlignedAllocator<T>>
using ParallelBlockQueue = tbb::concurrent_bounded_queue<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using HashSet = tsl::robin_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<const T, V>>>
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

template<typename T, typename V, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<const T, V>>>
using Map = std::map<T, V, std::less<T>, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Queue = std::queue<T, std::deque<T, Alloc>>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Deque = std::deque<T, Alloc>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using Hash = std::hash<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

