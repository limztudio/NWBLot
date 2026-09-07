// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include <tbb/scalable_allocator.h>
#include <global/arena_memory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void* CoreAlloc(usize size, ArenaMemoryTracker* arenaTracker){
    void* const result = scalable_malloc(size);
    if(result){
        const u64 bytes = static_cast<u64>(scalable_msize(result));
        RecordHeapMemoryAllocation(bytes);
        if(arenaTracker)
            arenaTracker->recordAllocation(bytes);
    }
    return result;
}

void* CoreRealloc(void* p, usize size, ArenaMemoryTracker* arenaTracker){
    const u64 oldBytes = p ? static_cast<u64>(scalable_msize(p)) : 0u;
    void* const result = scalable_realloc(p, size);
    if(result || size == 0u){
        const u64 newBytes = result ? static_cast<u64>(scalable_msize(result)) : 0u;
        RecordHeapMemoryReallocation(oldBytes, newBytes);
        if(arenaTracker)
            arenaTracker->recordReallocation(oldBytes, newBytes);
    }
    return result;
}

void* CoreReallocAligned(void* p, usize size, usize align, ArenaMemoryTracker* arenaTracker){
    if(align == 0u || (align & (align - 1u)) != 0u)
        return scalable_aligned_realloc(p, size, align);

    const u64 oldBytes = p ? static_cast<u64>(scalable_msize(p)) : 0u;
    void* const result = scalable_aligned_realloc(p, size, align);
    if(result || size == 0u){
        const u64 newBytes = result ? static_cast<u64>(scalable_msize(result)) : 0u;
        RecordHeapMemoryReallocation(oldBytes, newBytes);
        if(arenaTracker)
            arenaTracker->recordReallocation(oldBytes, newBytes);
    }
    return result;
}

void* CoreAllocAligned(usize size, usize align, ArenaMemoryTracker* arenaTracker){
    void* const result = scalable_aligned_malloc(size, align);
    if(result){
        const u64 bytes = static_cast<u64>(scalable_msize(result));
        RecordHeapMemoryAllocation(bytes);
        if(arenaTracker)
            arenaTracker->recordAllocation(bytes);
    }
    return result;
}

usize CoreMsize(void* ptr)noexcept{
    return static_cast<usize>(scalable_msize(ptr));
}

void CoreFree(void* ptr, ArenaMemoryTracker* arenaTracker)noexcept{
    if(ptr){
        const u64 bytes = static_cast<u64>(scalable_msize(ptr));
        RecordHeapMemoryDeallocation(bytes);
        if(arenaTracker)
            arenaTracker->recordDeallocation(bytes);
    }
    scalable_free(ptr);
}

void CoreFreeSize(void* ptr, usize size, ArenaMemoryTracker* arenaTracker)noexcept{
    static_cast<void>(size);
    CoreFree(ptr, arenaTracker);
}

void CoreFreeAligned(void* ptr, ArenaMemoryTracker* arenaTracker)noexcept{
    if(ptr){
        const u64 bytes = static_cast<u64>(scalable_msize(ptr));
        RecordHeapMemoryDeallocation(bytes);
        if(arenaTracker)
            arenaTracker->recordDeallocation(bytes);
    }
    scalable_aligned_free(ptr);
}

void CoreFreeSizeAligned(void* ptr, usize size, ArenaMemoryTracker* arenaTracker)noexcept{
    static_cast<void>(size);
    CoreFreeAligned(ptr, arenaTracker);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

