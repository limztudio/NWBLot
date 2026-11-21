// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "core.h"

#include <global/allocation_size.h>
#include <global/arena_base.h>
#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ScratchArena final : public ::ArenaBaseT<ScratchArena>{
private:
    using Base = ::ArenaBaseT<ScratchArena>;


public:
    static inline constexpr Name s_DefaultAllocationLog{ "core/alloc/scratch" };
    static constexpr usize s_MaxAlignSize = 256;
    static constexpr usize s_DefaultInitialChunkBytes = 1024u;


private:
    class Chunk{
        friend class ScratchArena;


    public:
        [[nodiscard]] static inline Chunk* Create(usize align, usize size){
            const usize payloadSize = Alignment(align, size);
            if(payloadSize == 0u)
                return nullptr;
            const usize backingAlign = Max(align, alignof(Chunk));
            const usize payloadOffset = Alignment(backingAlign, sizeof(Chunk));
            if(AddOverflows<usize>(payloadOffset, payloadSize))
                return nullptr;
            void* const backing = CoreAllocAligned(payloadOffset + payloadSize, backingAlign);
            if(!backing)
                return nullptr;

            return new(backing) Chunk(payloadSize, MakeNotNull(static_cast<u8*>(backing) + payloadOffset));
        }

        static inline void Destroy(Chunk& chunk)noexcept{
            chunk.~Chunk();
            CoreFreeAligned(&chunk);
        }


    public:
        inline Chunk(usize size, NotNull<u8*> buffer)noexcept
            : m_size(size)
            , m_remaining(m_size)
            , m_next(nullptr)
            , m_buffer(buffer)
            , m_available(m_buffer)
        {}
    private:
        ~Chunk()noexcept = default;


    public:
        inline NotNull<u8*> allocate(usize size)noexcept{
            const auto ret = m_available;
            m_available = MakeNotNull(m_available.get() + static_cast<isize>(size));
            m_remaining -= size;
            return ret;
        }

        inline b8 tryPopLifo(void* p, usize size)noexcept{
            const usize available = reinterpret_cast<usize>(m_available.get());
            const usize allocationBegin = reinterpret_cast<usize>(p);
            if(AddOverflows<usize>(allocationBegin, size))
                return false;

            const usize allocationEnd = allocationBegin + size;
            if(allocationEnd != available)
                return false;

            const usize bufferBegin = reinterpret_cast<usize>(m_buffer.get());
            const b8 isValidRequest = allocationBegin >= bufferBegin && allocationEnd <= available && size <= (m_size - m_remaining);
            if(!isValidRequest)
                return false;

            m_available = MakeNotNull(reinterpret_cast<u8*>(allocationBegin));
            m_remaining += size;
            return true;
        }

        inline usize lifoTopSpan(void* p)const noexcept{
            const usize bufferBegin = reinterpret_cast<usize>(m_buffer.get());
            const usize allocationBegin = reinterpret_cast<usize>(p);
            const usize available = reinterpret_cast<usize>(m_available.get());
            if(allocationBegin < bufferBegin || allocationBegin > available)
                return 0;
            return available - allocationBegin;
        }

        inline b8 tryResizeLifoTop(NotNull<void*> p, usize newSize)noexcept{
            const usize bufferBegin = reinterpret_cast<usize>(m_buffer.get());
            const usize allocationBegin = reinterpret_cast<usize>(p.get());
            if(AddOverflows<usize>(allocationBegin, newSize))
                return false;

            const usize newEnd = allocationBegin + newSize;
            if(newEnd > bufferBegin + m_size)
                return false;

            m_available = MakeNotNull(reinterpret_cast<u8*>(newEnd));
            m_remaining = m_size - (newEnd - bufferBegin);
            return true;
        }


    private:
        const usize m_size;
        usize m_remaining;
        Chunk* m_next;

        NotNull<u8*> m_buffer;
        NotNull<u8*> m_available;
    };
    struct ChunkWrapper{
        Chunk* active;
        Chunk* cached;
        usize size;
    };


private:
    [[nodiscard]] static constexpr b8 IsValidAlignment(usize align)noexcept{
        return align != 0u && align <= s_MaxAlignSize && IsSupportedAlignmentValue(align);
    }


public:
    class Scope final : ::NoCopy{
    private:
        struct SavedBucket{
            Chunk* active = nullptr;
            u8* available = nullptr;
            usize remaining = 0u;
        };


    public:
        explicit Scope(ScratchArena& arena)noexcept
            : m_arena(arena)
        {
            for(usize i = 0u; i < arena.m_bucket.size(); ++i){
                const Chunk* const active = arena.m_bucket[i].active;
                if(active){
                    m_saved[i] = SavedBucket{
                        .active = const_cast<Chunk*>(active),
                        .available = active->m_available.get(),
                        .remaining = active->m_remaining,
                    };
                }
            }
        }

        ~Scope()noexcept{
            for(usize i = 0u; i < m_arena.m_bucket.size(); ++i){
                auto& bucket = m_arena.m_bucket[i];
                const SavedBucket& saved = m_saved[i];
                while(bucket.active && bucket.active != saved.active){
                    Chunk& chunk = *bucket.active;
                    const usize unpopped = chunk.m_size - chunk.m_remaining;
                    if(unpopped != 0u)
                        m_arena.m_memoryStats.recordDeallocation(unpopped);
                    chunk.m_available = chunk.m_buffer;
                    chunk.m_remaining = chunk.m_size;
                    m_arena.cacheEmptyChunk(bucket, chunk);
                }
                if(bucket.active && bucket.active == saved.active){
                    Chunk& chunk = *bucket.active;
                    if(chunk.m_remaining < saved.remaining){
                        const usize unpopped = saved.remaining - chunk.m_remaining;
                        m_arena.m_memoryStats.recordDeallocation(unpopped);
                        chunk.m_available = MakeNotNull(saved.available);
                        chunk.m_remaining = saved.remaining;
                        if(chunk.m_remaining == chunk.m_size)
                            m_arena.cacheEmptyChunk(bucket, chunk);
                    }
                }
            }
        }


    private:
        ScratchArena& m_arena;
        Array<SavedBucket, FloorLog2(s_MaxAlignSize) + 1> m_saved = {};
    };


public:
    using Base::allocate;
    using Base::deallocate;


public:
    explicit ScratchArena(const Name& allocationLog = s_DefaultAllocationLog, usize initSize = s_DefaultInitialChunkBytes)
        : Base(allocationLog)
    {
        for(usize i = 0u; i < LengthOf(m_bucket); ++i){
            auto& bucket = m_bucket[i];
            bucket.active = nullptr;
            bucket.cached = nullptr;
            bucket.size = AlignmentPowerOfTwo(static_cast<usize>(1) << i, initSize);
        }
    }
    ~ScratchArena()noexcept override{
        for(auto& bucket : m_bucket){
            for(auto* chunk : { bucket.active, bucket.cached }){
                while(chunk){
                    auto* previous = chunk->m_next;
                    Chunk::Destroy(*chunk);
                    chunk = previous;
                }
            }
        }
        m_memoryStats.releaseRetainedMemory();
    }


public:
    [[nodiscard]] FOUNDATION_INLINE void* allocate(usize align, usize size)override{
        FOUNDATION_ASSERT_MSG(IsValidAlignment(align), FOUNDATION_TEXT("ScratchArena alignment must be a non-zero power of two up to s_MaxAlignSize"));
        if(!IsValidAlignment(align))
            return nullptr;

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        auto& bucket = m_bucket[bucketIndex];

        size = AlignmentPowerOfTwo(align, size);
        if(!bucket.active || size > bucket.active->m_remaining){
            if(!acquireChunk(bucket, align, size))
                return nullptr;
        }

        void* p = bucket.active->allocate(size).get();
        m_memoryStats.recordAllocation(size);
        return p;
    }

    [[nodiscard]] FOUNDATION_INLINE void* reallocate(void* p, usize align, usize size)override{
        FOUNDATION_ASSERT_MSG(IsValidAlignment(align), FOUNDATION_TEXT("ScratchArena alignment must be a non-zero power of two up to s_MaxAlignSize"));
        if(!IsValidAlignment(align))
            return nullptr;
        if(!p)
            return allocate(align, size);

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        auto& bucket = m_bucket[bucketIndex];
        FOUNDATION_ASSERT_MSG(bucket.active != nullptr, FOUNDATION_TEXT("Attempted to reallocate before allocating"));
        if(!bucket.active)
            return nullptr;

        size = AlignmentPowerOfTwo(align, size);

        Chunk& chunk = *bucket.active;
        const usize oldSize = chunk.lifoTopSpan(p);
        FOUNDATION_ASSERT_MSG(oldSize != 0, FOUNDATION_TEXT("ScratchArena can only reallocate its most-recent allocation"));
        if(oldSize == 0)
            return nullptr;

        if(chunk.tryResizeLifoTop(MakeNotNull(p), size)){
            m_memoryStats.recordReallocation(oldSize, size);
            if(chunk.m_remaining == chunk.m_size)
                cacheEmptyChunk(bucket, chunk);
            return p;
        }

        void* next = allocate(align, size);
        if(!next)
            return nullptr;

        FOUNDATION_MEMCPY(next, size, p, oldSize);
        if(chunk.tryPopLifo(p, oldSize)){
            m_memoryStats.recordDeallocation(oldSize);
            if(chunk.m_remaining == chunk.m_size)
                cacheEmptyChunk(bucket, chunk, *MakeNotNull(bucket.active));
        }
        return next;
    }

    FOUNDATION_INLINE void deallocate(void* p, usize align, usize size)noexcept override{
        FOUNDATION_ASSERT_MSG(IsValidAlignment(align), FOUNDATION_TEXT("ScratchArena alignment must be a non-zero power of two up to s_MaxAlignSize"));
        if(!IsValidAlignment(align) || size == 0u)
            return;

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        auto& bucket = m_bucket[bucketIndex];
        FOUNDATION_ASSERT_MSG(bucket.active != nullptr, FOUNDATION_TEXT("Attempted to deallocate before allocating"));
        if(!bucket.active)
            return;

        const auto alignedSize = AlignUpPowerOfTwoChecked(size, align);
        if(!alignedSize)
            return;
        size = *alignedSize;
        Chunk& chunk = *bucket.active;
        if(chunk.tryPopLifo(p, size)){
            m_memoryStats.recordDeallocation(size);
            if(chunk.m_remaining == chunk.m_size)
                cacheEmptyChunk(bucket, chunk);
        }
    }


private:
    [[nodiscard]] Chunk* acquireChunk(ChunkWrapper& bucket, const usize align, const usize size){
        Chunk* chunk = bucket.cached;
        while(chunk && chunk->m_size < size)
            chunk = chunk->m_next;

        const b8 created = !chunk;
        if(created){
            usize chunkSize = bucket.size;
            if(size > chunkSize)
                chunkSize = size > (static_cast<usize>(-1) >> 1) ? size : (size << 1);
            chunk = Chunk::Create(align, chunkSize);
            if(!chunk)
                return nullptr;
        }

        while(bucket.cached && bucket.cached != chunk){
            Chunk* discarded = bucket.cached;
            bucket.cached = discarded->m_next;
            m_memoryStats.removeReservedBytes(static_cast<u64>(discarded->m_size));
            Chunk::Destroy(*discarded);
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

    void cacheEmptyChunk(ChunkWrapper& bucket, Chunk& chunk)noexcept{
        FOUNDATION_ASSERT(chunk.m_remaining == chunk.m_size);
        FOUNDATION_ASSERT(bucket.active == &chunk);
        bucket.active = chunk.m_next;
        chunk.m_next = bucket.cached;
        bucket.cached = &chunk;
    }

    void cacheEmptyChunk(ChunkWrapper& bucket, Chunk& chunk, Chunk& newer)noexcept{
        FOUNDATION_ASSERT(chunk.m_remaining == chunk.m_size);
        FOUNDATION_ASSERT(newer.m_next == &chunk);
        newer.m_next = chunk.m_next;
        chunk.m_next = bucket.cached;
        bucket.cached = &chunk;
    }


private:
    Array<ChunkWrapper, FloorLog2(s_MaxAlignSize) + 1> m_bucket;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

