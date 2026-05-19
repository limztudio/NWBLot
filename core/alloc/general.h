// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "base.h"
#include "core.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ALLOC_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GlobalArena : public ArenaBase{
public:
    explicit GlobalArena(const char* allocationLog = "NWB::Core::Alloc::GlobalArena")
        : ArenaBase(allocationLog)
    {}
    ~GlobalArena() = default;


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? CoreAlloc(size, log()) : CoreAllocAligned(size, align, log());
    }
    template<typename T>
    inline T* allocate(usize count){
        return AllocDetail::AllocateTyped<T>(*this, count);
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(size);

        if(align <= 1)
            CoreFree(p, log());
        else
            CoreFreeAligned(p, log());
    }
    template<typename T>
    inline void deallocate(void* p, usize count){
        AllocDetail::DeallocateTyped<T>(*this, p, count);
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
    return GlobalUniquePtr<T>(new(arena.allocate<T>(1)) T(Forward<Args>(args)...), typename GlobalUniquePtr<T>::deleter_type(arena));
}
template<typename T>
inline typename EnableIf<IsUnboundedArray<T>::value, GlobalUniquePtr<T>>::type MakeGlobalUnique(NWB::Core::Alloc::GlobalArena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    return GlobalUniquePtr<T>(new(arena.allocate<TBase>(n)) TBase[n], typename GlobalUniquePtr<T>::deleter_type(arena, n));
}
template<typename T, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeGlobalUnique(Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct ArenaRefDeleter{
    NWB::Core::Alloc::GlobalArena* arena = nullptr;

    constexpr ArenaRefDeleter()noexcept = default;
    constexpr explicit ArenaRefDeleter(NWB::Core::Alloc::GlobalArena* a)noexcept
        : arena(a)
    {}
    template<typename U>
    ArenaRefDeleter(const ArenaRefDeleter<U>& other, typename EnableIf<IsConvertible<U*, T*>::value>::type* = 0)noexcept
        : arena(other.arena)
    {}

    void operator()(T* p)const noexcept{
        if(p && arena){
            p->~T();
            arena->deallocate(p, alignof(T), sizeof(T));
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

