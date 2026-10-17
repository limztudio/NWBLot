// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "generic.h"
#include "not_null.h"
#include "unique_ptr.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArenaObjectDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena, bool DestroyCompleted = true, bool OwnsConstructed = false>
class ConstructionStorage final : NoCopy{
public:
    ConstructionStorage(Arena& arena, NotNull<T*> memory, const usize count, T*& constructed)noexcept
        : m_arena(arena)
        , m_memory(memory.get())
        , m_count(count)
        , m_constructed(constructed)
    {}
    ~ConstructionStorage()noexcept{
        if(m_memory){
            if constexpr(DestroyCompleted && (OwnsConstructed || requires(T* value){ value->~T(); })){
                if(m_constructed){
                    ArenaDeleter<T, Arena>{ m_arena }(m_constructed);
                    return;
                }
            }
            m_arena.template deallocate<T>(m_memory, m_count);
        }
    }


public:
    void release()noexcept{ m_memory = nullptr; }


private:
    Arena& m_arena;
    T* m_memory;
    usize m_count;
    T*& m_constructed;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename... Args>
consteval bool CanConstructArenaObject()noexcept{
    return requires{ ::new(DeclVal<void*>()) T(DeclVal<Args&&>()...); };
}

template<typename T, typename... Args>
consteval bool IsNothrowArenaObjectConstruction()noexcept{
    if constexpr(CanConstructArenaObject<T, Args...>())
        return noexcept(::new(DeclVal<void*>()) T(DeclVal<Args&&>()...));
    else
        return false;
}

template<typename T>
consteval bool IsNothrowArenaArrayConstruction()noexcept{
    if constexpr(requires{ ::new(DeclVal<void*>()) T[DeclVal<usize&>()]; })
        return noexcept(::new(DeclVal<void*>()) T[DeclVal<usize&>()]);
    else
        return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Construction and destruction retain the supplied arena allocation; class-specific allocation functions do not replace that storage.
// Raw construction does not require access to a private destructor; only accessible completed objects are retired on argument cleanup failure.
template<typename Concrete, typename Arena, typename... Args>
Concrete* NewArenaObject(Arena& arena, Args&&... args)noexcept(noexcept(arena.template allocate<Concrete>(1)) && ArenaObjectDetail::IsNothrowArenaObjectConstruction<Concrete, Args...>()){
    auto* mem = arena.template allocate<Concrete>(1);
    if(!mem)
        return nullptr;

    Concrete* object = nullptr;
    ArenaObjectDetail::ConstructionStorage<Concrete, Arena> storage(arena, MakeNotNull(mem), 1u, object);
    object = ::new(static_cast<void*>(mem)) Concrete(Forward<Args>(args)...);
    if(object)
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


public:
    constexpr ArenaRefDeleter()noexcept = default;
    constexpr explicit ArenaRefDeleter(ArenaT* value)noexcept
        : arena(value)
    {}
    template<typename OtherValueT>
    ArenaRefDeleter(const ArenaRefDeleter<OtherValueT, ArenaT>& other, typename EnableIf<IsConvertible<OtherValueT*, ValueT*>::value>::type* = 0)noexcept
        : arena(other.arena)
    {}


public:
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

namespace ArenaObjectDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena, typename... Args>
consteval bool IsNothrowArenaUniqueConstruction()noexcept{
    if constexpr(CanConstructArenaObject<T, Args...>()){
        return
            noexcept(DeclVal<Arena&>().template allocate<T>(1))
            && IsNothrowArenaObjectConstruction<T, Args...>()
            && noexcept(DeclVal<Arena&>().template deallocate<T>(DeclVal<T*&>(), DeclVal<usize&>()))
            && noexcept(ArenaUniquePtr<T, Arena>(DeclVal<T*>(), typename ArenaUniquePtr<T, Arena>::deleter_type(DeclVal<Arena&>())))
        ;
    }
    else
        return noexcept(ArenaUniquePtr<T, Arena>(NewArenaObject<T>(DeclVal<Arena&>(), DeclVal<Args&&>()...), typename ArenaUniquePtr<T, Arena>::deleter_type(DeclVal<Arena&>())));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Arena, typename... Args>
inline typename EnableIf<!IsArray<T>::value, ArenaUniquePtr<T, Arena>>::type MakeArenaUnique(Arena& arena, Args&&... args)noexcept(ArenaObjectDetail::IsNothrowArenaUniqueConstruction<T, Arena, Args...>()){
    if constexpr(ArenaObjectDetail::CanConstructArenaObject<T, Args...>()){
        auto* mem = arena.template allocate<T>(1);
        if(!mem)
            return ArenaUniquePtr<T, Arena>(static_cast<T*>(nullptr), typename ArenaUniquePtr<T, Arena>::deleter_type(arena));

        T* object = nullptr;
        ArenaObjectDetail::ConstructionStorage<T, Arena, true, true> storage(arena, MakeNotNull(mem), 1u, object);
        object = ::new(static_cast<void*>(mem)) T(Forward<Args>(args)...);
        if(object)
            storage.release();
        return ArenaUniquePtr<T, Arena>(object, typename ArenaUniquePtr<T, Arena>::deleter_type(arena));
    }
    else
        return ArenaUniquePtr<T, Arena>(NewArenaObject<T>(arena, Forward<Args>(args)...), typename ArenaUniquePtr<T, Arena>::deleter_type(arena));
}
template<typename T, typename Arena>
inline typename EnableIf<IsUnboundedArray<T>::value, ArenaUniquePtr<T, Arena>>::type MakeArenaUnique(Arena& arena, usize n)noexcept(
    noexcept(arena.template allocate<RemoveExtent_T<T>>(n))
    && ArenaObjectDetail::IsNothrowArenaArrayConstruction<RemoveExtent_T<T>>()
    && noexcept(arena.template deallocate<RemoveExtent_T<T>>(DeclVal<RemoveExtent_T<T>*&>(), DeclVal<usize&>()))
    && noexcept(ArenaUniquePtr<T, Arena>(DeclVal<RemoveExtent_T<T>*>(), typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n)))
){
    typedef typename RemoveExtent<T>::type TBase;
    auto* mem = arena.template allocate<TBase>(n);
    if(!mem)
        return ArenaUniquePtr<T, Arena>(static_cast<TBase*>(nullptr), typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));

    TBase* objects = nullptr;
    ArenaObjectDetail::ConstructionStorage<TBase, Arena, false> storage(arena, MakeNotNull(mem), n, objects);
    objects = ::new(static_cast<void*>(mem)) TBase[n];
    if(objects)
        storage.release();
    return ArenaUniquePtr<T, Arena>(objects, typename ArenaUniquePtr<T, Arena>::deleter_type(arena, n));
}
template<typename T, typename Arena, typename... Args>
typename EnableIf<IsBoundedArray<T>::value>::type MakeArenaUnique(Arena&, Args&&...) = delete;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

