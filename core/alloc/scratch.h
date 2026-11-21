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
    static constexpr usize s_BucketCount = FloorLog2(s_MaxAlignSize) + 1u;

    [[noreturn]] FOUNDATION_NOINLINE static void ThrowAllocationSizeOverflow(){
        throw AllocationSizeException{};
    }

    class Chunk{
        friend class ScratchArena;


    public:
        [[nodiscard]] static inline Chunk* Create(usize align, usize size){
            const usize payloadSize = AlignmentPowerOfTwo(align, size);
            if(payloadSize == 0u)
                return nullptr;
            const usize backingAlign = Max(align, alignof(Chunk));
            const usize payloadOffset = AlignmentPowerOfTwo(backingAlign, sizeof(Chunk));
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


private:
    [[nodiscard]] static constexpr b8 IsValidAlignment(usize align)noexcept{
        return align != 0u && align <= s_MaxAlignSize && IsSupportedAlignmentValue(align);
    }


public:
    class Scope final : ::NoCopy{
    private:
        struct SavedBucket{
            Chunk* active;
            u8* available;
            usize remaining;
        };


    public:
        explicit Scope(ScratchArena& arena)noexcept
            : m_arena(arena)
            , m_savedMask(arena.m_activeBucketMask)
        {
            for(u32 bits = m_savedMask; bits != 0u; bits &= bits - 1u){
                const usize i = static_cast<usize>(CountTrailingZeros(bits));
                const Chunk* const active = arena.m_active[i];
                m_saved[i] = SavedBucket{
                    .active = const_cast<Chunk*>(active),
                    .available = active->m_available.get(),
                    .remaining = active->m_remaining,
                };
            }
        }

        ~Scope()noexcept{
            for(u32 bits = m_arena.m_activeBucketMask; bits != 0u; bits &= bits - 1u){
                const usize i = static_cast<usize>(CountTrailingZeros(bits));
                const b8 hadSaved = (m_savedMask & static_cast<u16>(1u << i)) != 0u;
                Chunk* const savedActive = hadSaved ? m_saved[i].active : nullptr;
                while(m_arena.m_active[i] && m_arena.m_active[i] != savedActive){
                    Chunk& chunk = *m_arena.m_active[i];
                    const usize unpopped = chunk.m_size - chunk.m_remaining;
                    if(unpopped != 0u)
                        m_arena.m_memoryStats.recordDeallocationSingleThread(unpopped);
                    chunk.m_available = chunk.m_buffer;
                    chunk.m_remaining = chunk.m_size;
                    if(!chunk.m_next)
                        break;
                    m_arena.cacheEmptyChunk(i, chunk);
                }
                if(hadSaved && m_arena.m_active[i] && m_arena.m_active[i] == savedActive){
                    Chunk& chunk = *m_arena.m_active[i];
                    const SavedBucket& saved = m_saved[i];
                    if(chunk.m_remaining < saved.remaining){
                        const usize unpopped = saved.remaining - chunk.m_remaining;
                        m_arena.m_memoryStats.recordDeallocationSingleThread(unpopped);
                        chunk.m_available = MakeNotNull(saved.available);
                        chunk.m_remaining = saved.remaining;
                        if(chunk.m_remaining == chunk.m_size && chunk.m_next)
                            m_arena.cacheEmptyChunk(i, chunk);
                    }
                }
            }
        }


    private:
        ScratchArena& m_arena;
        Array<SavedBucket, s_BucketCount> m_saved;
        u16 m_savedMask = 0u;
    };


public:
    using Base::allocate;
    using Base::deallocate;


public:
    explicit ScratchArena(const Name& allocationLog = s_DefaultAllocationLog, usize initSize = s_DefaultInitialChunkBytes)
        : Base(allocationLog, ArenaMemoryReservation::Separate, true)
        , m_initSize(initSize)
    {}
    ~ScratchArena()noexcept override{
        for(usize i = 0u; i < s_BucketCount; ++i){
            for(auto* chunk : { m_active[i], m_cached[i] }){
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
        if(!IsValidAlignment(align)) [[unlikely]]
            return nullptr;

        const usize alignMask = align - 1u;
        if(size > Limit<usize>::s_Max - alignMask) [[unlikely]]
            ThrowAllocationSizeOverflow();
        size = (size + alignMask) & ~alignMask;

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        Chunk* active = m_active[bucketIndex];
        if(!active || size > active->m_remaining) [[unlikely]]{
            active = acquireChunk(bucketIndex, align, size);
            if(!active)
                return nullptr;
        }

        void* p = active->allocate(size).get();
        m_memoryStats.recordAllocationSingleThread(size);
        return p;
    }

    [[nodiscard]] FOUNDATION_INLINE void* reallocate(void* p, usize align, usize size)override{
        FOUNDATION_ASSERT_MSG(IsValidAlignment(align), FOUNDATION_TEXT("ScratchArena alignment must be a non-zero power of two up to s_MaxAlignSize"));
        if(!IsValidAlignment(align)) [[unlikely]]
            return nullptr;
        if(!p)
            return allocate(align, size);

        const usize alignMask = align - 1u;
        if(size > Limit<usize>::s_Max - alignMask) [[unlikely]]
            ThrowAllocationSizeOverflow();
        size = (size + alignMask) & ~alignMask;

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        Chunk* const active = m_active[bucketIndex];
        FOUNDATION_ASSERT_MSG(active != nullptr, FOUNDATION_TEXT("Attempted to reallocate before allocating"));
        if(!active)
            return nullptr;

        Chunk& chunk = *active;
        const usize oldSize = chunk.lifoTopSpan(p);
        FOUNDATION_ASSERT_MSG(oldSize != 0, FOUNDATION_TEXT("ScratchArena can only reallocate its most-recent allocation"));
        if(oldSize == 0)
            return nullptr;

        if(chunk.tryResizeLifoTop(MakeNotNull(p), size)){
            m_memoryStats.recordReallocationSingleThread(oldSize, size);
            if(chunk.m_remaining == chunk.m_size && chunk.m_next)
                cacheEmptyChunk(bucketIndex, chunk);
            return p;
        }

        void* next = allocate(align, size);
        if(!next)
            return nullptr;

        FOUNDATION_MEMCPY(next, size, p, oldSize);
        if(chunk.tryPopLifo(p, oldSize)){
            m_memoryStats.recordDeallocationSingleThread(oldSize);
            if(chunk.m_remaining == chunk.m_size)
                cacheEmptyChunk(bucketIndex, chunk, *MakeNotNull(m_active[bucketIndex]));
        }
        return next;
    }

    FOUNDATION_INLINE void deallocate(void* p, usize align, usize size)noexcept override{
        FOUNDATION_ASSERT_MSG(IsValidAlignment(align), FOUNDATION_TEXT("ScratchArena alignment must be a non-zero power of two up to s_MaxAlignSize"));
        if(!IsValidAlignment(align) || size == 0u)
            return;

        const usize bucketIndex = static_cast<usize>(CountTrailingZeros(align));
        Chunk* const active = m_active[bucketIndex];
        FOUNDATION_ASSERT_MSG(active != nullptr, FOUNDATION_TEXT("Attempted to deallocate before allocating"));
        if(!active)
            return;

        const usize alignMask = align - 1u;
        if(size > Limit<usize>::s_Max - alignMask) [[unlikely]]
            return;
        size = (size + alignMask) & ~alignMask;
        Chunk& chunk = *active;
        if(chunk.tryPopLifo(p, size)){
            m_memoryStats.recordDeallocationSingleThread(size);
            if(chunk.m_remaining == chunk.m_size && chunk.m_next) [[unlikely]]
                cacheEmptyChunk(bucketIndex, chunk);
        }
    }


private:
    [[nodiscard]] FOUNDATION_NOINLINE Chunk* acquireChunk(const usize bucketIndex, const usize align, const usize size){
        if(m_bucketSizes[bucketIndex] == 0u && m_initSize != 0u)
            m_bucketSizes[bucketIndex] = AlignmentPowerOfTwo(align, m_initSize);

        Chunk* chunk = m_cached[bucketIndex];
        while(chunk && chunk->m_size < size)
            chunk = chunk->m_next;

        const b8 created = !chunk;
        if(created){
            usize chunkSize = m_bucketSizes[bucketIndex];
            if(size > chunkSize)
                chunkSize = size > (static_cast<usize>(-1) >> 1) ? size : (size << 1);
            chunk = Chunk::Create(align, chunkSize);
            if(!chunk)
                return nullptr;
        }

        m_memoryStats.ensureAttached();

        while(m_cached[bucketIndex] && m_cached[bucketIndex] != chunk){
            Chunk* discarded = m_cached[bucketIndex];
            m_cached[bucketIndex] = discarded->m_next;
            m_memoryStats.removeReservedBytesSingleThread(static_cast<u64>(discarded->m_size));
            Chunk::Destroy(*discarded);
        }
        if(created){
            m_bucketSizes[bucketIndex] = Max(m_bucketSizes[bucketIndex], chunk->m_size);
            m_memoryStats.addReservedBytesSingleThread(static_cast<u64>(chunk->m_size));
        }
        else
            m_cached[bucketIndex] = chunk->m_next;

        chunk->m_next = m_active[bucketIndex];
        m_active[bucketIndex] = chunk;
        m_activeBucketMask |= static_cast<u16>(1u << bucketIndex);
        return chunk;
    }

    FOUNDATION_NOINLINE void cacheEmptyChunk(const usize bucketIndex, Chunk& chunk)noexcept{
        FOUNDATION_ASSERT(chunk.m_remaining == chunk.m_size);
        FOUNDATION_ASSERT(m_active[bucketIndex] == &chunk);
        m_active[bucketIndex] = chunk.m_next;
        chunk.m_next = m_cached[bucketIndex];
        m_cached[bucketIndex] = &chunk;
        if(!m_active[bucketIndex])
            m_activeBucketMask &= static_cast<u16>(~(1u << bucketIndex));
    }

    FOUNDATION_NOINLINE void cacheEmptyChunk(const usize bucketIndex, Chunk& chunk, Chunk& newer)noexcept{
        FOUNDATION_ASSERT(chunk.m_remaining == chunk.m_size);
        FOUNDATION_ASSERT(newer.m_next == &chunk);
        newer.m_next = chunk.m_next;
        chunk.m_next = m_cached[bucketIndex];
        m_cached[bucketIndex] = &chunk;
    }


private:
    Array<Chunk*, s_BucketCount> m_active{};
    Array<Chunk*, s_BucketCount> m_cached{};
    Array<usize, s_BucketCount> m_bucketSizes{};
    usize m_initSize = s_DefaultInitialChunkBytes;
    u16 m_activeBucketMask = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

