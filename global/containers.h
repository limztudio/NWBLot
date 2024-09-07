// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"

#include "../3rd_parties/cameron314_queue/readerwriterqueue.h"
#include "../3rd_parties/cameron314_queue/readerwritercircularbuffer.h"
#include "../3rd_parties/cameron314_queue/concurrentqueue.h"
#include "../3rd_parties/cameron314_queue/blockingconcurrentqueue.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T, usize MAX_BLOCK_SIZE = 512>
using serial_queue = moodycamel::ReaderWriterQueue<T, static_cast<size_t>(MAX_BLOCK_SIZE)>;

template <typename T>
using serial_circularbuffer = moodycamel::BlockingReaderWriterCircularBuffer<T>;

template <typename T>
using parallel_queue = moodycamel::ConcurrentQueue<T>;

template <typename T>
using parallel_blockqueue = moodycamel::BlockingConcurrentQueue<T>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

