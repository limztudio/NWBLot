// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize... Is>
using IndexSequence = ::IndexSequence<Is...>;

template<typename... Ts>
using IndexSequenceFor = ::IndexSequenceFor<Ts...>;

template<typename... Ts>
inline constexpr auto ForwardAsTuple(Ts&&... values){
    return Tuple<Ts&&...>(Forward<Ts>(values)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using ValueTuple = Tuple<Entity, Ts&...>;

    ComponentTuple pools;
    usize anchorPoolIndex;
    usize index;
    usize count;
    bool valid;

    ViewIterator(ComponentTuple _pools, usize _anchorPoolIndex, usize _index, usize _count, bool _valid)
        : pools(Move(_pools))
        , anchorPoolIndex(_anchorPoolIndex)
        , index(_index)
        , count(_count)
        , valid(_valid)
    {
        if(valid)
            skipInvalid();
        else
            index = count;
    }

    void skipInvalid(){
        while(index < count){
            Entity e = entityAt(index);
            if(e.valid() && allHave(e))
                break;
            ++index;
        }
    }

    bool allHave(Entity e)const{
        return allHaveImpl(e, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    bool allHaveImpl(Entity e, IndexSequence<Is...>)const{
        return ((Get<Is>(pools) != nullptr && Get<Is>(pools)->has(e)) && ...);
    }

    Entity entityAt(usize denseIndex)const{
        return entityAtImpl(denseIndex, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    Entity entityAtImpl(usize denseIndex, IndexSequence<Is...>)const{
        Entity result = ENTITY_INVALID;
        (void)((anchorPoolIndex == Is ? (result = Get<Is>(pools)->denseEntities()[denseIndex], true) : false) || ...);
        return result;
    }

    ValueTuple operator*()const{
        Entity e = entityAt(index);
        return deref(e, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    ValueTuple deref(Entity e, IndexSequence<Is...>)const{
        return ForwardAsTuple(e, Get<Is>(pools)->get(e)...);
    }

    ViewIterator& operator++(){
        ++index;
        skipInvalid();
        return *this;
    }
};
template<typename... Ts>
inline bool operator==(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b){ return a.index == b.index; }
template<typename... Ts>
inline bool operator!=(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b){ return a.index != b.index; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
class View{
    static_assert(sizeof...(Ts) > 0, "View requires at least one component type");


public:
    using IteratorType = __hidden_ecs::ViewIterator<Ts...>;
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;


public:
    View(ComponentTuple pools)
        : m_pools(pools)
    {
        initializeAnchor();
    }


public:
    IteratorType begin()const{
        return IteratorType(m_pools, m_anchorPoolIndex, 0, m_count, m_valid);
    }
    IteratorType end()const{
        return IteratorType(m_pools, m_anchorPoolIndex, m_count, m_count, m_valid);
    }


public:
    template<typename Func>
    void each(Func&& func)const{
        if(!m_valid)
            return;

        for(usize i = 0; i < m_count; ++i){
            Entity e = entityAt(i);
            if(e.valid() && allHave(e))
                applyFunc(func, e, __hidden_ecs::IndexSequenceFor<Ts...>{});
        }
    }

    template<typename Func>
    void parallelEach(Alloc::ThreadPool& pool, Func&& func)const{
        if(!m_valid)
            return;

        pool.parallelFor(static_cast<usize>(0), m_count,
            [this, &func](usize i){
                Entity e = entityAt(i);
                if(e.valid() && allHave(e))
                    applyFunc(func, e, __hidden_ecs::IndexSequenceFor<Ts...>{});
            }
        );
    }


private:
    void initializeAnchor(){
        m_valid = true;
        m_anchorPoolIndex = 0;
        m_count = static_cast<usize>(-1);

        initializeAnchorImpl(__hidden_ecs::IndexSequenceFor<Ts...>{});

        if(!m_valid)
            m_count = 0;
    }

    template<usize... Is>
    void initializeAnchorImpl(__hidden_ecs::IndexSequence<Is...>){
        (updateAnchor<Is>(), ...);
    }

    template<usize I>
    void updateAnchor(){
        auto* pool = Get<I>(m_pools);
        if(!pool){
            m_valid = false;
            return;
        }

        const usize poolSize = pool->size();
        if(poolSize < m_count){
            m_count = poolSize;
            m_anchorPoolIndex = I;
        }
    }

    Entity entityAt(usize denseIndex)const{
        return entityAtImpl(denseIndex, __hidden_ecs::IndexSequenceFor<Ts...>{});
    }

    template<usize... Is>
    Entity entityAtImpl(usize denseIndex, __hidden_ecs::IndexSequence<Is...>)const{
        Entity result = ENTITY_INVALID;
        (void)((m_anchorPoolIndex == Is ? (result = Get<Is>(m_pools)->denseEntities()[denseIndex], true) : false) || ...);
        return result;
    }

    bool allHave(Entity e)const{
        return allHaveImpl(e, __hidden_ecs::IndexSequenceFor<Ts...>{});
    }

    template<usize... Is>
    bool allHaveImpl(Entity e, __hidden_ecs::IndexSequence<Is...>)const{
        return ((Get<Is>(m_pools) != nullptr && Get<Is>(m_pools)->has(e)) && ...);
    }

    template<typename Func, usize... Is>
    void applyFunc(Func& func, Entity e, __hidden_ecs::IndexSequence<Is...>)const{
        func(e, Get<Is>(m_pools)->get(e)...);
    }


private:
    ComponentTuple m_pools;
    usize m_anchorPoolIndex = 0;
    usize m_count = 0;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
