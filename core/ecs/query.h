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
using ViewDenseIndexTuple = Tuple<Conditional_T<true, u32, Ts>...>;

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
    static bool findDenseIndex(const Tuple<ComponentPool<Ts>*...>& pools, usize anchorPoolIndex, usize anchorDenseIndex, EntityID entityId, u32& outDenseIndex){
        if(I == anchorPoolIndex){
            outDenseIndex = static_cast<u32>(anchorDenseIndex);
            return true;
        }

        auto* pool = Get<I>(pools);
        return pool != nullptr && pool->findDenseIndex(entityId, outDenseIndex);
    }

    template<usize I, typename... Ts>
    static auto& componentAtDense(const Tuple<ComponentPool<Ts>*...>& pools, u32 denseIndex){
        return Get<I>(pools)->m_components[denseIndex];
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using ValueTuple = Tuple<EntityID, Ts&...>;
    using DenseIndexTuple = ViewDenseIndexTuple<Ts...>;

    ComponentTuple pools;
    DenseIndexTuple denseIndices;
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
            if(resolveDenseIndices(entityId, index))
                break;
            ++index;
        }
    }

    bool resolveDenseIndices(EntityID entityId, usize anchorDenseIndex){
        return resolveDenseIndicesImpl(entityId, anchorDenseIndex, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    bool resolveDenseIndicesImpl(EntityID entityId, usize anchorDenseIndex, IndexSequence<Is...>){
        return (ViewTupleAccess::findDenseIndex<Is>(pools, anchorPoolIndex, anchorDenseIndex, entityId, Get<Is>(denseIndices)) && ...);
    }

    EntityID entityAt(usize denseIndex)const{
        return ViewTupleAccess::entityAt(pools, anchorPoolIndex, denseIndex);
    }

    ValueTuple operator*()const{
        EntityID entityId = entityAt(index);
        return deref(entityId, IndexSequenceFor<Ts...>{});
    }
    template<usize... Is>
    ValueTuple deref(EntityID entityId, IndexSequence<Is...>)const{
        return ForwardAsTuple(entityId, ViewTupleAccess::componentAtDense<Is>(pools, Get<Is>(denseIndices))...);
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
            tryApplyFunc(func, entityId, i);
        }
    }

    template<typename Func>
    void parallelEach(Alloc::ThreadPool& pool, Func&& func)const{
        if(!m_valid)
            return;

        pool.parallelFor(static_cast<usize>(0), m_count,
            [this, &func](usize i){
                EntityID entityId = entityAt(i);
                tryApplyFunc(func, entityId, i);
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

    template<usize I = 0, typename Func, typename... Args>
    void tryApplyFunc(Func& func, EntityID entityId, usize denseIndex, Args&... args)const{
        if constexpr(I < sizeof...(Ts)){
            u32 componentDenseIndex = 0;
            if(!ECSDetail::ViewTupleAccess::findDenseIndex<I>(m_pools, m_anchorPoolIndex, denseIndex, entityId, componentDenseIndex))
                return;

            auto& component = ECSDetail::ViewTupleAccess::componentAtDense<I>(m_pools, componentDenseIndex);
            tryApplyFunc<I + 1u>(func, entityId, denseIndex, args..., component);
        }
        else{
            func(entityId, args...);
        }
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

