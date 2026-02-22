// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using ValueTuple = Tuple<Entity, Ts&...>;

    ComponentTuple pools;
    usize index;
    usize count;

    ViewIterator(ComponentTuple _pools, usize _index, usize _count)
        : pools(Move(_pools))
        , index(_index)
        , count(_count)
    {
        skipInvalid();
    }

    void skipInvalid(){
        while(index < count){
            Entity e = Get<0>(pools)->denseEntities()[index];
            if(allHave(e))
                break;
            ++index;
        }
    }

    bool allHave(Entity e)const{
        return allHaveImpl(e, std::index_sequence_for<Ts...>{});
    }
    template<usize... Is>
    bool allHaveImpl(Entity e, std::index_sequence<Is...>)const{
        return (Get<Is>(pools)->has(e) && ...);
    }

    ValueTuple operator*()const{
        Entity e = Get<0>(pools)->denseEntities()[index];
        return deref(e, std::index_sequence_for<Ts...>{});
    }
    template<usize... Is>
    ValueTuple deref(Entity e, std::index_sequence<Is...>)const{
        return std::forward_as_tuple(e, Get<Is>(pools)->get(e)...);
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
    using ComponentTuple = std::tuple<ComponentPool<Ts>*...>;


public:
    View(ComponentTuple pools)
        : m_pools(pools)
        , m_count(Get<0>(pools) ? Get<0>(pools)->size() : 0)
    {}


public:
    IteratorType begin()const{
        return IteratorType(m_pools, 0, m_count);
    }
    IteratorType end()const{
        return IteratorType(m_pools, m_count, m_count);
    }


public:
    template<typename Func>
    void each(Func&& func)const{
        if(!Get<0>(m_pools))
            return;

        auto& entities = Get<0>(m_pools)->denseEntities();
        for(usize i = 0; i < entities.size(); ++i){
            Entity e = entities[i];
            if(allHave(e))
                applyFunc(func, e, std::index_sequence_for<Ts...>{});
        }
    }

    template<typename Func>
    void parallelEach(Func&& func)const{
        if(!Get<0>(m_pools))
            return;

        auto& entities = Get<0>(m_pools)->denseEntities();
        const usize n = entities.size();

        tbb::parallel_for(tbb::blocked_range<usize>(0, n),
            [this, &entities, &func](const tbb::blocked_range<usize>& range){
                for(usize i = range.begin(); i < range.end(); ++i){
                    Entity e = entities[i];
                    if(allHave(e))
                        applyFunc(func, e, std::index_sequence_for<Ts...>{});
                }
            }
        );
    }


private:
    bool allHave(Entity e)const{
        return allHaveImpl(e, std::index_sequence_for<Ts...>{});
    }

    template<usize... Is>
    bool allHaveImpl(Entity e, std::index_sequence<Is...>)const{
        return (Get<Is>(m_pools)->has(e) && ...);
    }

    template<typename Func, usize... Is>
    void applyFunc(Func& func, Entity e, std::index_sequence<Is...>)const{
        func(e, Get<Is>(m_pools)->get(e)...);
    }


private:
    ComponentTuple m_pools;
    usize m_count;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

