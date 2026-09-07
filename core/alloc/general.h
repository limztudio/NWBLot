// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "core.h"

#include <global/arena_base.h>
#include <global/arena_object.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlobalArena : public ::ArenaBaseT<GlobalArena>{
public:
    using Base = ::ArenaBaseT<GlobalArena>;


public:
    using Base::allocate;
    using Base::deallocate;


public:
    explicit GlobalArena(const Name& allocationLog)
        : Base(allocationLog, ArenaMemoryReservation::FollowsUsage)
    {}
    ~GlobalArena() = default;


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? CoreAlloc(size, &m_memoryStats) : CoreAllocAligned(size, align, &m_memoryStats);
    }

    inline void* reallocate(void* p, usize align, usize size){
        size = Alignment(align, size);

        if(!p && size == 0u)
            return nullptr;

        return (align <= 1) ? CoreRealloc(p, size, &m_memoryStats) : CoreReallocAligned(p, size, align, &m_memoryStats);
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(size);

        if(align <= 1)
            CoreFree(p, &m_memoryStats);
        else
            CoreFreeAligned(p, &m_memoryStats);
    }

    template<typename T>
    inline void deallocateObject(T* const p)noexcept{
        constexpr usize allocationSize = Alignment(alignof(T), sizeof(T));

        if constexpr(alignof(T) <= 1u)
            CoreFreeSize(p, allocationSize, &m_memoryStats);
        else
            CoreFreeSizeAligned(p, allocationSize, &m_memoryStats);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using GlobalUniquePtr = ::ArenaUniquePtr<T, Alloc::GlobalArena>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, GlobalUniquePtr<T>>::type MakeGlobalUnique(NWB::Core::Alloc::GlobalArena& arena, Args&&... args){
    return ::MakeArenaUnique<T>(arena, Forward<Args>(args)...);
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, GlobalUniquePtr<T>>::type MakeGlobalUnique(NWB::Core::Alloc::GlobalArena& arena, usize n){
    return ::MakeArenaUnique<T>(arena, n);
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeGlobalUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

