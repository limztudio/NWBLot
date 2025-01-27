// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"

#include <readerwriterqueue.h>
#include <readerwritercircularbuffer.h>
#include <concurrentqueue.h>
#include <blockingconcurrentqueue.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, usize MAX_BLOCK_SIZE = 512>
using SerialQueue = moodycamel::ReaderWriterQueue<T, static_cast<size_t>(MAX_BLOCK_SIZE)>;

template <typename T>
using SerialCircularBuffer = moodycamel::BlockingReaderWriterCircularBuffer<T>;

template <typename T>
using ParallelQueue = moodycamel::ConcurrentQueue<T>;

template <typename T>
using ParallelBlockQueue = moodycamel::BlockingConcurrentQueue<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

