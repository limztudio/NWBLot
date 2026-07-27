// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "allocation_size.h"
#include "compile.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT>
[[nodiscard]] inline T* AllocateArenaTyped(ArenaT& arena, const usize count){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
    const usize byteCount = SizeOf<sizeof(T)>(count);
    if(byteCount == 0u)
        return nullptr;

    if(IsConstantEvaluated())
        return reinterpret_cast<T*>(arena.allocate(1u, byteCount));

    return reinterpret_cast<T*>(arena.allocate(alignof(T), byteCount));
}

template<typename T, typename ArenaT>
inline void DeallocateArenaTyped(ArenaT& arena, void* const ptr, const usize count){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling deallocate.");
    const usize byteCount = SizeOf<sizeof(T)>(count);
    if(byteCount == 0u)
        return;

    if(IsConstantEvaluated()){
        arena.deallocate(ptr, 1u, byteCount);
        return;
    }

    arena.deallocate(ptr, alignof(T), byteCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline void* AllocateArenaCMemory(ArenaT& arena, const usize size){
    return arena.allocate(alignof(MaxAlign), size == 0u ? 1u : size);
}

template<typename ArenaT>
inline void DeallocateArenaCMemory(ArenaT& arena, void* const ptr){
    if(!ptr)
        return;

    arena.deallocate(ptr, alignof(MaxAlign), 0u);
}

template<typename ArenaT>
[[nodiscard]] inline void* ReallocateArenaCMemory(ArenaT& arena, void* const ptr, const usize size){
    if(!ptr)
        return AllocateArenaCMemory(arena, size);
    if(size == 0u){
        DeallocateArenaCMemory(arena, ptr);
        return nullptr;
    }

    return arena.reallocate(ptr, alignof(MaxAlign), size);
}

template<typename ArenaT>
[[nodiscard]] inline char* DuplicateArenaCString(ArenaT& arena, const char* const text){
    if(!text)
        return nullptr;

    const usize byteCount = static_cast<usize>(NWB_STRLEN(text)) + 1u;
    char* const copy = static_cast<char*>(AllocateArenaCMemory(arena, byteCount));
    if(!copy)
        return nullptr;

    NWB_MEMCPY(copy, byteCount, text, byteCount);
    return copy;
}

template<typename ArenaT>
[[nodiscard]] inline void* ZeroAllocateArenaCMemory(ArenaT& arena, const usize count, const usize size){
    if(count != 0u && size > Limit<usize>::s_Max / count)
        return nullptr;

    const usize byteCount = count * size;
    void* const ptr = AllocateArenaCMemory(arena, byteCount);
    if(ptr)
        NWB_MEMSET(ptr, 0, byteCount);
    return ptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

