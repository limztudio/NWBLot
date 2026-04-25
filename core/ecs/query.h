// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize... Is>
using IndexSequence = ::IndexSequence<Is...>;

template<typename... Ts>
using IndexSequenceFor = ::IndexSequenceFor<Ts...>;

template<typename... Ts>
inline constexpr auto ForwardAsTuple(Ts&&... values){
    return Tuple<Ts&&...>(Forward<Ts>(values)...);
}

struct ViewTupleAccess{
    template<usize I = 0, typename... Ts>
    static EntityID entityAt(const Tuple<ComponentPool<Ts>*...>& pools, usize anchorPoolIndex, usize denseIndex){
        static_assert(sizeof...(Ts) > 0, "View requires at least one component type");
        if(anchorPoolIndex == I)
            return Get<I>(pools)->m_dense[denseIndex];

        if constexpr(I + 1u < sizeof...(Ts))
            return entityAt<I + 1u>(pools, anchorPoolIndex, denseIndex);
        return ENTITY_ID_INVALID;
    }

    template<usize I, typename... Ts>
    static bool hasComponent(const Tuple<ComponentPool<Ts>*...>& pools, usize anchorPoolIndex, EntityID entityId){
        if(I == anchorPoolIndex)
            return true;

        auto* pool = Get<I>(pools);
        return pool != nullptr && pool->has(entityId);
    }

    template<usize I, typename... Ts>
    static auto& componentAt(const Tuple<ComponentPool<Ts>*...>& pools, usize anchorPoolIndex, usize denseIndex, EntityID entityId){
        auto* pool = Get<I>(pools);
        if(I == anchorPoolIndex)
            return pool->m_components[denseIndex];

        return pool->get(entityId);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using ValueTuple = Tuple<EntityID, Ts&...>;

    ComponentTuple pools;
    usize anchorPoolIndex;
    usize index;
    usize count;
    bool valid;

    ViewIterator(ComponentTuple poolsValue, usize anchorPoolIndexValue, usize indexValue, usize countValue, bool validValue)
        : pools(Move(poolsValue))
        , anchorPoolIndex(anchorPoolIndexValue)
        , index(indexValue)
        , count(countValue)
        , valid(validValue)
    {
        if(valid)
            skipInvalid();
        else
            index = count;
    }

    void skipInvalid(){
        while(index < count){
            EntityID entityId = entityAt(index);
            if(entityId.valid() && allHave(entityId))
                break;
            ++index;
        }
    }

    bool allHave(EntityID entityId)const{
        return allHaveImpl(entityId, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    bool allHaveImpl(EntityID entityId, IndexSequence<Is...>)const{
        return (ViewTupleAccess::hasComponent<Is>(pools, anchorPoolIndex, entityId) && ...);
    }

    EntityID entityAt(usize denseIndex)const{
        return ViewTupleAccess::entityAt(pools, anchorPoolIndex, denseIndex);
    }

    ValueTuple operator*()const{
        EntityID entityId = entityAt(index);
        return deref(entityId, index, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    ValueTuple deref(EntityID entityId, usize denseIndex, IndexSequence<Is...>)const{
        return ForwardAsTuple(entityId, ViewTupleAccess::componentAt<Is>(pools, anchorPoolIndex, denseIndex, entityId)...);
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
    using IteratorType = ECSDetail::ViewIterator<Ts...>;
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

    [[nodiscard]] usize candidateCount()const noexcept{
        return m_valid ? m_count : 0u;
    }


public:
    template<typename Func>
    void each(Func&& func)const{
        if(!m_valid)
            return;

        for(usize i = 0; i < m_count; ++i){
            EntityID entityId = entityAt(i);
            if(entityId.valid() && allHave(entityId))
                applyFunc(func, entityId, i, ECSDetail::IndexSequenceFor<Ts...>{});
        }
    }

    template<typename Func>
    void parallelEach(Alloc::ThreadPool& pool, Func&& func)const{
        if(!m_valid)
            return;

        pool.parallelFor(static_cast<usize>(0), m_count,
            [this, &func](usize i){
                EntityID entityId = entityAt(i);
                if(entityId.valid() && allHave(entityId))
                    applyFunc(func, entityId, i, ECSDetail::IndexSequenceFor<Ts...>{});
            }
        );
    }


private:
    void initializeAnchor(){
        m_valid = true;
        m_anchorPoolIndex = 0;
        m_count = Limit<usize>::s_Max;

        initializeAnchorImpl(ECSDetail::IndexSequenceFor<Ts...>{});

        if(!m_valid)
            m_count = 0;
    }

    template<usize... Is>
    void initializeAnchorImpl(ECSDetail::IndexSequence<Is...>){
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

    EntityID entityAt(usize denseIndex)const{
        return ECSDetail::ViewTupleAccess::entityAt(m_pools, m_anchorPoolIndex, denseIndex);
    }

    bool allHave(EntityID entityId)const{
        return allHaveImpl(entityId, ECSDetail::IndexSequenceFor<Ts...>{});
    }

    template<usize... Is>
    bool allHaveImpl(EntityID entityId, ECSDetail::IndexSequence<Is...>)const{
        return (ECSDetail::ViewTupleAccess::hasComponent<Is>(m_pools, m_anchorPoolIndex, entityId) && ...);
    }

    template<typename Func, usize... Is>
    void applyFunc(Func& func, EntityID entityId, usize denseIndex, ECSDetail::IndexSequence<Is...>)const{
        func(entityId, ECSDetail::ViewTupleAccess::componentAt<Is>(m_pools, m_anchorPoolIndex, denseIndex, entityId)...);
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

