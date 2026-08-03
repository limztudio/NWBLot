// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "unique_ptr.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Concrete, typename Arena, typename... Args>
Concrete* NewArenaObject(Arena& arena, Args&&... args){
    auto* mem = arena.template allocate<Concrete>(1);
    if(!mem)
        return nullptr;

    return new(mem) Concrete(Forward<Args>(args)...);
}

template<typename Concrete, typename Arena>
void DestroyArenaObject(Arena& arena, Concrete* p){
    if(p){
        p->~Concrete();
        arena.template deallocate<Concrete>(p, 1);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArenaObjectDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename ValueT>
void DestroyArenaReference(ArenaT* arena, ValueT* value)noexcept{
    value->~ValueT();
    arena->deallocate(value, alignof(ValueT), sizeof(ValueT));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ValueT, typename ArenaT>
struct ArenaRefDeleter{
    ArenaT* arena = nullptr;

    constexpr ArenaRefDeleter()noexcept = default;
    constexpr explicit ArenaRefDeleter(ArenaT* value)noexcept
        : arena(value)
    {}
    template<typename OtherValueT>
    ArenaRefDeleter(const ArenaRefDeleter<OtherValueT, ArenaT>& other, typename EnableIf<IsConvertible<OtherValueT*, ValueT*>::value>::type* = 0)noexcept
        : arena(other.arena)
    {}

    void operator()(ValueT* value)const noexcept{
        if(value && arena){
            using ArenaObjectDetail::DestroyArenaReference;
            DestroyArenaReference(arena, value);
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena>
using ArenaUniquePtr = UniquePtr<T, ArenaDeleter<T, Arena>>;

template<typename T, typename Arena, typename... Args>
inline typename EnableIf<!IsArray<T>::value, ArenaUniquePtr<T, Arena>>::type MakeArenaUnique(Arena& arena, Args&&... args){
    auto* mem = arena.template allocate<T>(1);
    if(!mem)
        return ArenaUniquePtr<T, Arena>(nullptr, typename ArenaUniquePtr<T, Arena>::deleter_type(arena));

    return ArenaUniquePtr<T, Arena>(new(mem) T(Forward<Args>(args)...), typename ArenaUniquePtr<T, Arena>::deleter_type(arena));
}
template<typename T, typename Arena>
inline typename EnableIf<IsUnboundedArray<T>::value, ArenaUniquePtr<T, Arena>>::type MakeArenaUnique(Arena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    auto* mem = arena.template allocate<TBase>(n);
    if(!mem)
        return ArenaUniquePtr<T, Arena>(static_cast<TBase*>(nullptr), typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));

    return ArenaUniquePtr<T, Arena>(new(mem) TBase[n], typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));
}
template<typename T, typename Arena, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeArenaUnique(Arena&, Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

