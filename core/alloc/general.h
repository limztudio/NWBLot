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
    explicit GlobalArena(const char* allocationLog = "NWB::Core::Alloc::GlobalArena")
        : Base(allocationLog)
    {}
    ~GlobalArena() = default;


public:
    inline void* allocate(usize align, usize size){
        size = Alignment(align, size);

        return (align <= 1) ? CoreAlloc(size, log()) : CoreAllocAligned(size, align, log());
    }

    inline void deallocate(void* p, usize align, usize size){
        static_cast<void>(size);

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


namespace AllocDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
void DestroyArenaReference(NWB::Core::Alloc::GlobalArena* arena, T* p)noexcept{
    p->~T();
    arena->deallocate(p, alignof(T), sizeof(T));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
            using AllocDetail::DestroyArenaReference;
            DestroyArenaReference(arena, p);
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

