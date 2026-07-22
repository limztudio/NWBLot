// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "base.h"
#include "global.h"
#include "core.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScratchArena : public ArenaBaseT<ScratchArena>{
private:
    using Base = ArenaBaseT<ScratchArena>;


public:
    static constexpr usize s_MaxAlignSize = 256;
    static constexpr usize s_DefaultInitialChunkBytes = 1024u;


private:
    class Chunk{
        friend class ScratchArena;


    public:
        [[nodiscard]] static inline Chunk* create(usize align, usize size, const char* log){
            auto* chunk = new(std::nothrow) Chunk(align, size, log);
            if(!chunk || !chunk->m_buffer){
                delete chunk;
                return nullptr;
            }
            return chunk;
        }
        static inline void destroy(Chunk* chunk, const char* log){
            CoreFreeAligned(chunk->m_buffer, log);
            delete chunk;
        }


    public:
        inline Chunk(usize align, usize size, const char* log)
            : m_size(Alignment(align, size))
            , m_remaining(m_size)
            , m_next(nullptr)
            , m_buffer(CoreAllocAligned(m_size, align, log))
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

        inline void add(Chunk* next){
            m_next = next;
        }


    private:
        const usize m_size;
        usize m_remaining;
        Chunk* m_next;

        void* m_buffer;
        void* m_available;
    };
    struct ChunkWrapper{
        Chunk* head;
        Chunk* last;
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
            bucket.head = nullptr;
            bucket.last = nullptr;
            bucket.size = Alignment(static_cast<usize>(1) << i, initSize);
        }
    }
    ~ScratchArena(){
        const char* allocationLog = Base::log();
        for(auto& bucket : m_bucket){
            for(auto* cur = bucket.head; cur;){
                auto* next = cur->m_next;
                Chunk::destroy(cur, allocationLog);
                cur = next;
            }
        }
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
        usize chunkSize = bucket.size;
        if(size > chunkSize)
            chunkSize = (size > (static_cast<usize>(-1) >> 1)) ? size : (size << 1);

        if(!bucket.head){
            auto* chunk = Chunk::create(align, chunkSize, Base::log());
            if(!chunk)
                return nullptr;

            bucket.head = chunk;
            bucket.last = chunk;
            bucket.size = chunkSize;
            m_memoryStats.addReservedBytes(static_cast<u64>(chunk->m_size));
        }
        else if(size > bucket.last->m_remaining){
            auto* chunk = Chunk::create(align, chunkSize, Base::log());
            if(!chunk)
                return nullptr;

            bucket.last->add(chunk);
            bucket.last = chunk;
            bucket.size = chunkSize;
            m_memoryStats.addReservedBytes(static_cast<u64>(chunk->m_size));
        }

        void* p = bucket.last->allocate(size);
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
        NWB_ASSERT_MSG(bucket.last != nullptr, NWB_TEXT("Attempted to reallocate before allocating"));
        if(!bucket.last)
            return nullptr;

        size = Alignment(align, size);

        Chunk* chunk = bucket.last;
        const usize oldSize = chunk->lifoTopSpan(p);
        NWB_ASSERT_MSG(oldSize != 0, NWB_TEXT("ScratchArena can only reallocate its most-recent allocation"));
        if(oldSize == 0)
            return nullptr;

        if(chunk->tryResizeLifoTop(p, size)){
            m_memoryStats.recordReallocation(oldSize, size);
            return p;
        }

        void* next = allocate(align, size);
        if(!next)
            return nullptr;

        NWB_MEMCPY(next, size, p, oldSize);
        if(chunk->tryPopLifo(p, oldSize))
            m_memoryStats.recordDeallocation(oldSize);
        return next;
    }

    // LIFO reclaim only: returns space when p is the bucket's most-recent allocation;
    // any out-of-order free is a no-op and is reclaimed in bulk when the arena is destroyed.
    inline void deallocate(void* p, usize align, usize size){
        NWB_ASSERT_MSG(align != 0, NWB_TEXT("ScratchArena alignment must be non-zero"));
        NWB_ASSERT_MSG(align <= s_MaxAlignSize, NWB_TEXT("ScratchArena alignment exceeds s_MaxAlignSize"));
        if(align == 0 || align > s_MaxAlignSize)
            return;

        const usize bucketIndex = FloorLog2(align);
        NWB_ASSERT_MSG(bucketIndex < LengthOf(m_bucket), NWB_TEXT("ScratchArena alignment bucket index is out of range"));
        if(bucketIndex >= LengthOf(m_bucket))
            return;

        auto& bucket = m_bucket[bucketIndex];
        NWB_ASSERT_MSG(bucket.last != nullptr, NWB_TEXT("Attempted to deallocate before allocating"));
        if(!bucket.last)
            return;

        size = Alignment(align, size);
        if(bucket.last->tryPopLifo(p, size))
            m_memoryStats.recordDeallocation(size);
    }


private:
    ChunkWrapper m_bucket[FloorLog2(s_MaxAlignSize) + 1];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

