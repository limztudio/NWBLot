// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "unique_ptr.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArenaObjectDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena>
class ConstructionStorage final : NoCopy{
public:
    ConstructionStorage(Arena& arena, T* const memory, const usize count)noexcept
        : m_arena(arena)
        , m_memory(memory)
        , m_count(count)
    {}
    ~ConstructionStorage(){
        if(m_memory)
            m_arena.template deallocate<T>(m_memory, m_count);
    }


public:
    void release()noexcept{ m_memory = nullptr; }


private:
    Arena& m_arena;
    T* m_memory;
    usize m_count;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Concrete, typename Arena, typename... Args>
Concrete* NewArenaObject(Arena& arena, Args&&... args){
    auto* mem = arena.template allocate<Concrete>(1);
    if(!mem)
        return nullptr;

    ArenaObjectDetail::ConstructionStorage<Concrete, Arena> storage(arena, mem, 1u);
    auto* const object = new(mem) Concrete(Forward<Args>(args)...);
    storage.release();
    return object;
}

template<typename Concrete, typename Arena>
void DestroyArenaObject(Arena& arena, Concrete* p){
    if(p){
        p->~Concrete();
        arena.template deallocate<Concrete>(p, 1);
    }
}

template<typename Concrete, typename Arena>
void DestroyArenaObjectNoexcept(Arena& arena, Concrete* p)noexcept{
    static_assert(IsNothrowDestructible_V<Concrete>, "No-throw arena destruction requires a no-throw object destructor");
    static_assert(noexcept(arena.template deallocateObject<Concrete>(p)));
    if(p){
        p->~Concrete();
        arena.template deallocateObject<Concrete>(p);
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
    return ArenaUniquePtr<T, Arena>(NewArenaObject<T>(arena, Forward<Args>(args)...), typename ArenaUniquePtr<T, Arena>::deleter_type(arena));
}
template<typename T, typename Arena>
inline typename EnableIf<IsUnboundedArray<T>::value, ArenaUniquePtr<T, Arena>>::type MakeArenaUnique(Arena& arena, usize n){
    typedef typename RemoveExtent<T>::type TBase;
    auto* mem = arena.template allocate<TBase>(n);
    if(!mem)
        return ArenaUniquePtr<T, Arena>(static_cast<TBase*>(nullptr), typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));

    ArenaObjectDetail::ConstructionStorage<TBase, Arena> storage(arena, mem, n);
    auto* const objects = new(mem) TBase[n];
    storage.release();
    return ArenaUniquePtr<T, Arena>(objects, typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));
}
template<typename T, typename Arena, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type
MakeArenaUnique(Arena&, Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

