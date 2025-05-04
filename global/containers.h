// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "type.h"

#include <tuple>
#include <forward_list>
#include <list>
#include <vector>
#include <deque>

#include <readerwriterqueue.h>
#include <readerwritercircularbuffer.h>
#include <concurrentqueue.h>
#include <blockingconcurrentqueue.h>

#include <robin_set.h>
#include <robin_map.h>

#include "compressed_pair.h"
#include "basic_string.h"
#include "sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
    namespace Core{
        namespace Alloc{
            template<typename T>
            class GeneralAllocator;
        };
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename Arg0, typename Arg1>
using Pair = CompressedPair<Arg0, Arg1>;
template <typename Arg0, typename Arg1>
constexpr auto MakePair(Arg0&& arg0, Arg1&& arg1){
    using Arg0Type = RemoveReferenceWrapper_T<Decay_T<Arg0>>;
    using Arg1Type = RemoveReferenceWrapper_T<Decay_T<Arg1>>;
    return Pair<Arg0Type, Arg1Type>(Forward<Arg0>(arg0), Forward<Arg1>(arg1));
}

template <typename Arg0, typename... Args>
using Tuple = std::tuple<Arg0, Args...>;
template <typename... Args>
constexpr auto MakeTuple(Args&&... args){ return std::make_tuple(Forward<Args>(args)...); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, usize MAX_BLOCK_SIZE = 512>
using SerialQueue = moodycamel::ReaderWriterQueue<T, static_cast<size_t>(MAX_BLOCK_SIZE)>;

template<typename T>
using SerialCircularBuffer = moodycamel::BlockingReaderWriterCircularBuffer<T>;

template<typename T>
using ParallelQueue = moodycamel::ConcurrentQueue<T>;

template<typename T>
using ParallelBlockQueue = moodycamel::BlockingConcurrentQueue<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using HashSet = tsl::robin_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = Hasher<T>, typename Equal = EqualTo<T>, typename Alloc = NWB::Core::Alloc::GeneralAllocator<Pair<T, V>>>
using HashMap = tsl::robin_map<T, V, Hash, Equal, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using ForwardList = std::forward_list<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using List = std::list<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Vector = std::vector<T, Alloc>;

template<typename T, typename Alloc = NWB::Core::Alloc::GeneralAllocator<T>>
using Deque = std::deque<T, Alloc>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

