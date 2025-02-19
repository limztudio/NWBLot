// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "type.h"

#include <readerwriterqueue.h>
#include <readerwritercircularbuffer.h>
#include <concurrentqueue.h>
#include <blockingconcurrentqueue.h>

#include <robin_set.h>
#include <robin_map.h>

#include "atomic.h"
#include "sync.h"


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


template<typename T, typename Hash = std::hash<T>, typename Equal = std::equal_to<T>, typename Alloc = std::allocator<T>>
using HashSet = tsl::robin_set<T, Hash, Equal, Alloc>;

template<typename T, typename V, typename Hash = std::hash<T>, typename Equal = std::equal_to<T>, typename Alloc = std::allocator<std::pair<T, V>>>
using HashMap = tsl::robin_map<T, V, Hash, Equal, Alloc>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

