//=============================================================================
// Copyright (c) 2023-2026 Advanced Micro Devices, Inc. All rights reserved.
/// @author AMD Developer Tools Team
/// @file
/// @brief  Implementation of functions for a dynamic memory pool
///         allocator optimized for small text strings.
//=============================================================================

#include "rmt_string_memory_pool.h"

#include <new>

#include "rmt_assert.h"
#include "rmt_error.h"

RmtStringMemoryPool::RmtStringMemoryPool(const size_t block_size)
{
    memory_pool_block_size_    = block_size;
    memory_pool_buffer_        = nullptr;
    memory_pool_buffer_offset_ = 0;
}

RmtStringMemoryPool::~RmtStringMemoryPool()
{
    FreeAll();
}

RmtErrorCode RmtStringMemoryPool::Allocate(const size_t buffer_size, char** out_buffer)
{
    RMT_RETURN_ON_ERROR(out_buffer != nullptr, kRmtErrorInvalidPointer);
    RMT_RETURN_ON_ERROR(buffer_size != 0, kRmtErrorInvalidSize);
    RMT_RETURN_ON_ERROR(buffer_size <= memory_pool_block_size_, kRmtErrorInvalidSize);

    // Guard the offset add so a crafted size cannot wrap the room test.
    RMT_RETURN_ON_ERROR(memory_pool_buffer_offset_ <= memory_pool_block_size_, kRmtErrorMalformedData);
    const size_t remaining = memory_pool_block_size_ - memory_pool_buffer_offset_;
    if (buffer_size > remaining)
    {
        memory_pool_buffer_ = nullptr;
    }

    // If no buffer, allocate one and add it to the buffer cache.
    if (memory_pool_buffer_ == nullptr)
    {
        char* new_block = new (std::nothrow) char[memory_pool_block_size_];
        RMT_RETURN_ON_ERROR(new_block != nullptr, kRmtErrorOutOfMemory);
        memory_pool_buffer_ = new_block;
        memory_pool_blocks_.push_back(memory_pool_buffer_);
        memory_pool_buffer_offset_ = 0;
    }

    // Generate memory pointer from the buffer.
    *out_buffer = memory_pool_buffer_ + memory_pool_buffer_offset_;
    memory_pool_buffer_offset_ += buffer_size;

    return kRmtOk;
}

RmtErrorCode RmtStringMemoryPool::FreeAll()
{
    memory_pool_buffer_offset_ = 0;
    memory_pool_buffer_        = nullptr;
    for (auto memory_block : memory_pool_blocks_)
    {
        delete[] (memory_block);
    }
    memory_pool_blocks_.clear();

    return kRmtOk;
}
