// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "allocation_size.h"
#include "compile.h"
#include "limit.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename ArenaT>
[[nodiscard]] inline T* AllocateArenaTyped(ArenaT& arena, const usize count)noexcept([]()consteval noexcept{
    // Throwing arenas decide first, so friend NewArenaObject declarations do not need a complete T.
    if constexpr(!noexcept(DeclVal<ArenaT&>().allocate(1u, DeclVal<const usize&>())))
        return false;
    else
        return noexcept(SizeOf<sizeof(T)>(DeclVal<const usize&>())) && noexcept(DeclVal<ArenaT&>().allocate(alignof(T), DeclVal<const usize&>()));
}()){
    static_assert(sizeof(T) > 0, "value_type must be complete before calling allocate.");
    const usize byteCount = SizeOf<sizeof(T)>(count);
    if(byteCount == 0u)
        return nullptr;

    if(IsConstantEvaluated())
        return reinterpret_cast<T*>(arena.allocate(1u, byteCount));

    return reinterpret_cast<T*>(arena.allocate(alignof(T), byteCount));
}

template<typename T, typename ArenaT>
inline void DeallocateArenaTyped(ArenaT& arena, void* const ptr, const usize count)noexcept(noexcept(SizeOf<sizeof(T)>(count)) && noexcept(arena.deallocate(ptr, 1u, count)) && noexcept(arena.deallocate(ptr, alignof(T), count))){
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
[[nodiscard]] inline void* AllocateArenaCMemory(ArenaT& arena, const usize size)noexcept(noexcept(arena.allocate(alignof(MaxAlign), size)) && IsNothrowConvertible_V<decltype(arena.allocate(alignof(MaxAlign), size)), void*>){
    return arena.allocate(alignof(MaxAlign), size == 0u ? 1u : size);
}

template<typename ArenaT>
inline void DeallocateArenaCMemory(ArenaT& arena, void* const ptr)noexcept(noexcept(arena.deallocate(ptr, alignof(MaxAlign), 0u))){
    if(!ptr)
        return;

    arena.deallocate(ptr, alignof(MaxAlign), 0u);
}

template<typename ArenaT>
[[nodiscard]] inline void* ReallocateArenaCMemory(ArenaT& arena, void* const ptr, const usize size)noexcept(
    noexcept(AllocateArenaCMemory(arena, size))
    && noexcept(DeallocateArenaCMemory(arena, ptr))
    && noexcept(arena.reallocate(ptr, alignof(MaxAlign), size))
    && IsNothrowConvertible_V<decltype(arena.reallocate(ptr, alignof(MaxAlign), size)), void*>
){
    if(!ptr)
        return AllocateArenaCMemory(arena, size);
    if(size == 0u){
        DeallocateArenaCMemory(arena, ptr);
        return nullptr;
    }

    return arena.reallocate(ptr, alignof(MaxAlign), size);
}

template<typename ArenaT>
[[nodiscard]] inline char* DuplicateArenaCString(ArenaT& arena, const AStringView text)noexcept(noexcept(AllocateArenaCMemory(arena, text.size()))){
    if(text.size() == Limit<usize>::s_Max)
        return nullptr;

    const usize byteCount = text.size() + 1u;
    char* const copy = static_cast<char*>(AllocateArenaCMemory(arena, byteCount));
    if(!copy)
        return nullptr;

    if(!text.empty())
        NWB_MEMCPY(copy, byteCount, text.data(), text.size());
    copy[text.size()] = 0;
    return copy;
}

template<typename ArenaT>
[[nodiscard]] inline void* ZeroAllocateArenaCMemory(ArenaT& arena, const usize count, const usize size)noexcept(noexcept(AllocateArenaCMemory(arena, size))){
    if(count != 0u && size > Limit<usize>::s_Max / count)
        return nullptr;

    const usize byteCount = count * size;
    void* const ptr = AllocateArenaCMemory(arena, byteCount);
    if(ptr)
        NWB_MEMSET(ptr, 0, byteCount);
    return ptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

