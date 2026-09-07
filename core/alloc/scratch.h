// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"

#include <global/arena_base.h>

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScratchArena : public ::ArenaBaseT<ScratchArena>{
private:
    using Base = ::ArenaBaseT<ScratchArena>;


public:
    static constexpr usize s_MaxAlignSize = 256;
    static constexpr usize s_DefaultInitialChunkBytes = 1024u;


private:
    class Chunk{
        friend class ScratchArena;


    public:
        [[nodiscard]] static inline Chunk* create(usize align, usize size){
            auto* chunk = new(std::nothrow) Chunk(align, size);
            if(!chunk || !chunk->m_buffer){
                delete chunk;
                return nullptr;
            }
            return chunk;
        }
        static inline void destroy(Chunk* chunk){
            CoreFreeAligned(chunk->m_buffer);
            delete chunk;
        }


    public:
        inline Chunk(usize align, usize size)
            : m_size(Alignment(align, size))
            , m_remaining(m_size)
            , m_next(nullptr)
            , m_buffer(CoreAllocAligned(m_size, align))
            , m_available(m_buffer)
        {}
    private:
        ~Chunk() = default;


    public:
        inline void* allocate(usize size){
            auto* ret = m_available;
            m_available = reinterpret_cast<u8*>(m_available) + static_cast<isize>(size);
            m_remaining -= size;
            return ret;
        }
        inline bool tryPopLifo(void* p, usize size){
            const usize available = reinterpret_cast<usize>(m_available);
            const usize allocationBegin = reinterpret_cast<usize>(p);
            if(AddOverflows<usize>(allocationBegin, size))
                return false;

            const usize allocationEnd = allocationBegin + size;
            if(allocationEnd != available)
                return false;

            const usize bufferBegin = reinterpret_cast<usize>(m_buffer);
            const bool isValidRequest = allocationBegin >= bufferBegin && allocationEnd <= available && size <= (m_size - m_remaining);
            if(!isValidRequest)
                return false;

            m_available = reinterpret_cast<void*>(allocationBegin);
            m_remaining += size;
            return true;
        }
        inline usize lifoTopSpan(void* p)const{
            const usize bufferBegin = reinterpret_cast<usize>(m_buffer);
            const usize allocationBegin = reinterpret_cast<usize>(p);
            const usize available = reinterpret_cast<usize>(m_available);
            if(allocationBegin < bufferBegin || allocationBegin > available)
                return 0;
            return available - allocationBegin;
        }
        inline bool tryResizeLifoTop(void* p, usize newSize){
            const usize bufferBegin = reinterpret_cast<usize>(m_buffer);
            const usize allocationBegin = reinterpret_cast<usize>(p);
            if(AddOverflows<usize>(allocationBegin, newSize))
                return false;

            const usize newEnd = allocationBegin + newSize;
            if(newEnd > bufferBegin + m_size)
                return false;

            m_available = reinterpret_cast<void*>(newEnd);
            m_remaining = m_size - (newEnd - bufferBegin);
            return true;
        }


    private:
        const usize m_size;
        usize m_remaining;
        Chunk* m_next;

        void* m_buffer;
        void* m_available;
    };
    struct ChunkWrapper{
        Chunk* active;
        Chunk* cached;
        usize size;
    };


public:
    using Base::allocate;
    using Base::deallocate;


public:
    explicit ScratchArena(const Name& allocationLog, usize initSize = s_DefaultInitialChunkBytes)
        : Base(allocationLog)
    {
        for(usize i = 0; i < LengthOf(m_bucket); ++i){
            auto& bucket = m_bucket[i];
            bucket.active = nullptr;
            bucket.cached = nullptr;
            bucket.size = Alignment(static_cast<usize>(1) << i, initSize);
        }
    }
    ~ScratchArena(){
        for(auto& bucket : m_bucket){
            for(auto* chunk : { bucket.active, bucket.cached }){
                while(chunk){
                    auto* previous = chunk->m_next;
                    Chunk::destroy(chunk);
                    chunk = previous;
                }
            }
        }
        m_memoryStats.releaseRetainedMemory();
    }


public:
    inline void* allocate(usize align, usize size){
        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= s_MaxAlignSize, NWB_TEXT("ScratchArena alignment exceeds s_MaxAlignSize"));
        if(align == 0 || align > s_MaxAlignSize)
            return nullptr;

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return nullptr;

        auto& bucket = m_bucket[bucketIndex];

        size = Alignment(align, size);
        if(!bucket.active || size > bucket.active->m_remaining){
            if(!acquireChunk(bucket, align, size))
                return nullptr;
        }

        // A raw zero-byte allocation may leave one empty active chunk; it owns no tracked bytes.
        void* p = bucket.active->allocate(size);
        if(p)
            m_memoryStats.recordAllocation(size);
        return p;
    }

    // LIFO reclaim only: p must be the bucket's most-recent allocation (same contract as deallocate);
    // resizes the top in place, or relocates to a fresh block and copies when it cannot grow in place.
    inline void* reallocate(void* p, usize align, usize size){
        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= s_MaxAlignSize, NWB_TEXT("ScratchArena alignment exceeds s_MaxAlignSize"));
        if(align == 0 || align > s_MaxAlignSize)
            return nullptr;
        if(!p)
            return allocate(align, size);

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return nullptr;

        auto& bucket = m_bucket[bucketIndex];
        NWB_ASSERT_MSG(bucket.active != nullptr, NWB_TEXT("Attempted to reallocate before allocating"));
        if(!bucket.active)
            return nullptr;

        size = Alignment(align, size);

        Chunk* chunk = bucket.active;
        const usize oldSize = chunk->lifoTopSpan(p);
        NWB_ASSERT_MSG(oldSize != 0, NWB_TEXT("ScratchArena can only reallocate its most-recent allocation"));
        if(oldSize == 0)
            return nullptr;

        if(chunk->tryResizeLifoTop(p, size)){
            m_memoryStats.recordReallocation(oldSize, size);
            if(chunk->m_remaining == chunk->m_size)
                cacheEmptyChunk(bucket, *chunk, nullptr);
            return p;
        }

        void* next = allocate(align, size);
        if(!next)
            return nullptr;

        NWB_MEMCPY(next, size, p, oldSize);
        if(chunk->tryPopLifo(p, oldSize)){
            m_memoryStats.recordDeallocation(oldSize);
            if(chunk->m_remaining == chunk->m_size)
                cacheEmptyChunk(bucket, *chunk, bucket.active);
        }
        return next;
    }

    // LIFO reclaim spans chunks: empty chunks are cached and expose the previous live allocation;
    // any out-of-order free is a no-op and is reclaimed in bulk when the arena is destroyed.
    inline void deallocate(void* p, usize align, usize size){
        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= s_MaxAlignSize, NWB_TEXT("ScratchArena alignment exceeds s_MaxAlignSize"));
        if(align == 0 || align > s_MaxAlignSize || size == 0u)
            return;

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return;

        auto& bucket = m_bucket[bucketIndex];
        NWB_ASSERT_MSG(bucket.active != nullptr, NWB_TEXT("Attempted to deallocate before allocating"));
        if(!bucket.active)
            return;

        size = Alignment(align, size);
        Chunk* chunk = bucket.active;
        if(chunk->tryPopLifo(p, size)){
            m_memoryStats.recordDeallocation(size);
            if(chunk->m_remaining == chunk->m_size)
                cacheEmptyChunk(bucket, *chunk, nullptr);
        }
    }


private:
    [[nodiscard]] Chunk* acquireChunk(ChunkWrapper& bucket, const usize align, const usize size){
        Chunk* chunk = bucket.cached;
        while(chunk && chunk->m_size < size)
            chunk = chunk->m_next;

        const bool created = !chunk;
        if(created){
            usize chunkSize = bucket.size;
            if(size > chunkSize)
                chunkSize = size > (static_cast<usize>(-1) >> 1) ? size : (size << 1);
            chunk = Chunk::create(align, chunkSize);
            if(!chunk)
                return nullptr;
        }

        // Secure the replacement before changing ownership. Each undersized cached chunk is retired once;
        // future new chunks keep the high-water size so alternating workloads do not recreate small chunks.
        while(bucket.cached && bucket.cached != chunk){
            Chunk* discarded = bucket.cached;
            bucket.cached = discarded->m_next;
            m_memoryStats.removeReservedBytes(static_cast<u64>(discarded->m_size));
            Chunk::destroy(discarded);
        }
        if(created){
            bucket.size = Max(bucket.size, chunk->m_size);
            m_memoryStats.addReservedBytes(static_cast<u64>(chunk->m_size));
        }
        else
            bucket.cached = chunk->m_next;

        chunk->m_next = bucket.active;
        bucket.active = chunk;
        return chunk;
    }

    void cacheEmptyChunk(ChunkWrapper& bucket, Chunk& chunk, Chunk* newer)noexcept{
        NWB_ASSERT(chunk.m_remaining == chunk.m_size);
        if(newer){
            NWB_ASSERT(newer->m_next == &chunk);
            newer->m_next = chunk.m_next;
        }
        else{
            NWB_ASSERT(bucket.active == &chunk);
            bucket.active = chunk.m_next;
        }
        chunk.m_next = bucket.cached;
        bucket.cached = &chunk;
    }


private:
    ChunkWrapper m_bucket[FloorLog2(s_MaxAlignSize) + 1];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

