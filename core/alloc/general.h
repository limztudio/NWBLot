// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "base.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlobalArena : public ArenaBaseT<GlobalArena>{
public:
    using Base = ArenaBaseT<GlobalArena>;


public:
    using Base::allocate;
    using Base::deallocate;


public:
    explicit GlobalArena(const Name& allocationLog)
        : Base(allocationLog)
    {}
    ~GlobalArena() = default;


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        void* p = (align <= 1) ? CoreAlloc(size, log()) : CoreAllocAligned(size, align, log());
        if(p){
            m_memoryStats.addReservedBytes(size);
            m_memoryStats.recordAllocation(size);
        }
        return p;
    }

    inline void* reallocate(void* p, usize align, usize size){
        size = Alignment(align, size);

        if(!p && size == 0u)
            return nullptr;

        const u64 oldBytes = p ? static_cast<u64>(CoreMsize(p)) : 0u;
        void* next = (align <= 1) ? CoreRealloc(p, size, log()) : CoreReallocAligned(p, size, align, log());
        if(next || size == 0u){
            const u64 newBytes = next ? static_cast<u64>(CoreMsize(next)) : 0u;
            m_memoryStats.recordReallocation(oldBytes, newBytes);
            if(newBytes >= oldBytes)
                m_memoryStats.addReservedBytes(newBytes - oldBytes);
            else
                m_memoryStats.removeReservedBytes(oldBytes - newBytes);
        }
        return next;
    }

    inline void deallocate(void* p, usize align, usize size){
        size = Alignment(align, size);

        if(p){
            m_memoryStats.recordDeallocation(size);
            m_memoryStats.removeReservedBytes(size);
        }
        if(align <= 1)
            CoreFree(p, log());
        else
            CoreFreeAligned(p, log());
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using GlobalUniquePtr = UniquePtr<T, ArenaDeleter<T, NWB::Core::Alloc::GlobalArena>>;

template<typename T, typename... Args>
inline typename EnableIf<!IsArray<T>::value, GlobalUniquePtr<T>>::type MakeGlobalUnique(NWB::Core::Alloc::GlobalArena& arena, Args&&... args){
    auto* mem = arena.template allocate<T>(1);
    if(!mem)
        return GlobalUniquePtr<T>(nullptr, typename GlobalUniquePtr<T>::deleter_type(arena));

    return GlobalUniquePtr<T>(new(mem) T(Forward<Args>(args)...), typename GlobalUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, GlobalUniquePtr<T>>::type MakeGlobalUnique(NWB::Core::Alloc::GlobalArena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    auto* mem = arena.template allocate<TBase>(n);
    if(!mem)
        return GlobalUniquePtr<T>(static_cast<TBase*>(nullptr), typename GlobalUniquePtr<T>::deleter_type(arena, n));

    return GlobalUniquePtr<T>(new(mem) TBase[n], typename GlobalUniquePtr<T>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeGlobalUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

