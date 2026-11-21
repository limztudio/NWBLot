// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ViewTupleAccess{
    template<usize I, typename... Ts>
    static Span<const EntityID> EntityVector(const Tuple<ComponentPool<Ts>*...>& pools)noexcept{
        return MakeNotNull(Get<I>(pools))->m_dense;
    }

    static EntityID EntityAt(const Span<const EntityID> entities, const usize denseIndex)noexcept{
        FOUNDATION_ASSERT(denseIndex < entities.size());
        return entities[denseIndex];
    }

    template<usize I, typename... Ts>
    static FOUNDATION_INLINE u32 FindDenseIndexRaw(const Tuple<ComponentPool<Ts>*...>& pools, usize anchorPoolIndex, usize anchorDenseIndex, EntityID entityId)noexcept{
        if(I == anchorPoolIndex)
            return static_cast<u32>(anchorDenseIndex);

        return MakeNotNull(Get<I>(pools))->findDenseIndexRaw(entityId);
    }

    template<usize I, typename... Ts>
    static FOUNDATION_INLINE auto MakeSlice(const Tuple<ComponentPool<Ts>*...>& pools)noexcept{
        auto& pool = *MakeNotNull(Get<I>(pools));
        using CompT = RemovePointer_T<decltype(pool.m_components.data())>;
        return PoolSlice<CompT>{ pool.m_sparse.data(), pool.m_sparse.size(), pool.m_components.data() };
    }

    template<usize I, typename... Ts>
    static auto& ComponentAtDense(const Tuple<ComponentPool<Ts>*...>& pools, u32 denseIndex)noexcept{
        return MakeNotNull(Get<I>(pools))->m_components[denseIndex];
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using ValueTuple = Tuple<EntityID, Ts&...>;
    using DenseIndexTuple = Tuple<Conditional_T<true, u32, Ts>...>;


public:
    ComponentTuple pools;
    Span<const EntityID> anchorEntities;
    usize anchorPoolIndex;
    usize index;
    usize count;
    DenseIndexTuple denseIndices;
    EntityID entity;


public:
    ViewIterator(ComponentTuple poolsValue, const Span<const EntityID> anchorEntitiesValue, usize anchorPoolIndexValue, usize indexValue, usize countValue, b8 validValue)noexcept
        : pools(Move(poolsValue))
        , anchorEntities(anchorEntitiesValue)
        , anchorPoolIndex(anchorPoolIndexValue)
        , index(indexValue)
        , count(countValue)
        , entity(s_InvalidEntityId)
    {
        if(validValue)
            skipInvalid();
        else
            index = count;
    }


public:
    ValueTuple operator*()const noexcept{
        return deref(IndexSequenceFor<Ts...>{});
    }

    ViewIterator& operator++()noexcept{
        ++index;
        skipInvalid();
        return *this;
    }


public:
    void skipInvalid()noexcept{
        if constexpr(sizeof...(Ts) == 1u){
            if(index < count){
                entity = ViewTupleAccess::EntityAt(anchorEntities, index);
                Get<0>(denseIndices) = static_cast<u32>(index);
            }
            else{
                entity = s_InvalidEntityId;
            }
            return;
        }

        while(index < count){
            const EntityID entityId = ViewTupleAccess::EntityAt(anchorEntities, index);
            if(resolveDenseIndicesImpl(entityId, index, IndexSequenceFor<Ts...>{})){
                entity = entityId;
                break;
            }
            ++index;
        }

        if(index >= count)
            entity = s_InvalidEntityId;
    }

    template<usize I>
    b8 resolveDenseIndex(EntityID entityId, usize anchorDenseIndex)noexcept{
        const u32 denseIndex = ViewTupleAccess::FindDenseIndexRaw<I>(pools, anchorPoolIndex, anchorDenseIndex, entityId);
        if(denseIndex == s_InvalidDenseIndex)
            return false;
        Get<I>(denseIndices) = denseIndex;
        return true;
    }

    template<usize... Is>
    b8 resolveDenseIndicesImpl(EntityID entityId, usize anchorDenseIndex, IndexSequence<Is...>)noexcept{
        return (resolveDenseIndex<Is>(entityId, anchorDenseIndex) && ...);
    }


    template<usize... Is>
    ValueTuple deref(IndexSequence<Is...>)const noexcept{
        return ::ForwardAsTuple(entity, ViewTupleAccess::ComponentAtDense<Is>(pools, Get<Is>(denseIndices))...);
    }


};
template<typename... Ts>
inline b8 operator==(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b)noexcept{ return a.index == b.index; }

template<typename... Ts>
inline b8 operator!=(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b)noexcept{ return a.index != b.index; }


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
    View(ComponentTuple pools)noexcept
        : m_pools(pools)
    {
        initializeAnchor();
    }


public:
    IteratorType begin()const noexcept{
        return IteratorType(m_pools, m_anchorEntities, m_anchorPoolIndex, 0, m_count, m_valid);
    }

    IteratorType end()const noexcept{
        return IteratorType(m_pools, m_anchorEntities, m_anchorPoolIndex, m_count, m_count, false);
    }

    [[nodiscard]] usize candidateCount()const noexcept{
        return m_valid ? m_count : 0u;
    }


public:
    template<typename Func>
    void each(Func&& func)const noexcept(noexcept(DeclVal<Func&>()(DeclVal<Conditional_T<sizeof...(Ts) == 1u, const EntityID&, EntityID&>>(), DeclVal<Ts&>()...))){
        if(!m_valid)
            return;

        const auto slices = makeSlices(IndexSequenceFor<Ts...>{});
        const EntityID* const anchorData = m_anchorEntities.data();
        const usize count = m_count;
        if constexpr(sizeof...(Ts) == 1u){
            runAnchorLoop<0u>(anchorData, count, slices, func);
        }
        else if constexpr(sizeof...(Ts) == 2u){
            if(m_anchorPoolIndex == 0u)
                runAnchorLoop<0u>(anchorData, count, slices, func);
            else
                runAnchorLoop<1u>(anchorData, count, slices, func);
        }
        else if constexpr(sizeof...(Ts) == 3u){
            switch(m_anchorPoolIndex){
            case 0u: runAnchorLoop<0u>(anchorData, count, slices, func); break;
            case 1u: runAnchorLoop<1u>(anchorData, count, slices, func); break;
            default: runAnchorLoop<2u>(anchorData, count, slices, func); break;
            }
        }
        else if constexpr(sizeof...(Ts) == 4u){
            switch(m_anchorPoolIndex){
            case 0u: runAnchorLoop<0u>(anchorData, count, slices, func); break;
            case 1u: runAnchorLoop<1u>(anchorData, count, slices, func); break;
            case 2u: runAnchorLoop<2u>(anchorData, count, slices, func); break;
            default: runAnchorLoop<3u>(anchorData, count, slices, func); break;
            }
        }
        else{
            const usize anchorPoolIndex = m_anchorPoolIndex;
            for(usize i = 0u; i < count; ++i)
                tryApplySliceRuntime<0u>(slices, anchorPoolIndex, func, anchorData[i], i);
        }
    }

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEach(CpuTaskScope& tasks, Func&& func)const{
        constexpr usize s_DefaultParallelEachGrainSize = 64u;
        return parallelEachImpl(func, [this, &tasks](const auto& apply){
            return tasks.parallelFor(static_cast<usize>(0), m_count, s_DefaultParallelEachGrainSize, apply);
        });
    }

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEach(CpuTaskScope& tasks, usize grainSize, Func&& func, CpuTaskOptions options = {})const{
        return parallelEachImpl(func, [this, &tasks, grainSize, options](const auto& apply){
            return tasks.parallelFor(static_cast<usize>(0), m_count, grainSize, apply, options);
        });
    }


private:
    template<usize... Is>
    FOUNDATION_INLINE auto makeSlices(IndexSequence<Is...>)const noexcept{
        return MakeTuple(ECSDetail::ViewTupleAccess::MakeSlice<Is>(m_pools)...);
    }

    template<usize AnchorIdx, usize I, typename SliceTupleT, typename Func, typename... Args>
    static FOUNDATION_INLINE void applySlicesAtAnchor(const SliceTupleT& slices, Func& func, EntityID entityId, const usize anchorDenseIndex, Args&... args)noexcept(noexcept(DeclVal<Func&>()(DeclVal<Conditional_T<sizeof...(Ts) == 1u, const EntityID&, EntityID&>>(), DeclVal<Ts&>()...))){
        if constexpr(I < sizeof...(Ts)){
            if constexpr(I == AnchorIdx){
                auto& component = Get<I>(slices).components[anchorDenseIndex];
                applySlicesAtAnchor<AnchorIdx, I + 1u>(slices, func, entityId, anchorDenseIndex, args..., component);
            }
            else{
                const u32 componentDenseIndex = Get<I>(slices).findDenseIndex(entityId);
                if(componentDenseIndex == ECSDetail::s_InvalidDenseIndex)
                    return;

                auto& component = Get<I>(slices).components[componentDenseIndex];
                applySlicesAtAnchor<AnchorIdx, I + 1u>(slices, func, entityId, anchorDenseIndex, args..., component);
            }
        }
        else{
            func(entityId, args...);
        }
    }

    template<usize AnchorIdx, typename SliceTupleT, typename Func>
    static void runAnchorLoop(const EntityID* const anchorData, const usize count, const SliceTupleT& slices, Func& func)noexcept(noexcept(applySlicesAtAnchor<AnchorIdx, 0u>(slices, func, DeclVal<EntityID&>(), 0u))){
        for(usize i = 0u; i < count; ++i){
            EntityID entityId = anchorData[i];
            applySlicesAtAnchor<AnchorIdx, 0u>(slices, func, entityId, i);
        }
    }

    template<usize I = 0u, typename SliceTupleT, typename Func, typename... Args>
    static FOUNDATION_INLINE void tryApplySliceRuntime(const SliceTupleT& slices, const usize anchorPoolIndex, Func& func, EntityID entityId, const usize anchorDenseIndex, Args&... args)noexcept(noexcept(DeclVal<Func&>()(DeclVal<Conditional_T<sizeof...(Ts) == 1u, const EntityID&, EntityID&>>(), DeclVal<Ts&>()...))){
        if constexpr(I < sizeof...(Ts)){
            const u32 componentDenseIndex = (I == anchorPoolIndex) ? static_cast<u32>(anchorDenseIndex) : Get<I>(slices).findDenseIndex(entityId);
            if(componentDenseIndex == ECSDetail::s_InvalidDenseIndex)
                return;

            auto& component = Get<I>(slices).components[componentDenseIndex];
            tryApplySliceRuntime<I + 1u>(slices, anchorPoolIndex, func, entityId, anchorDenseIndex, args..., component);
        }
        else{
            func(entityId, args...);
        }
    }

    template<typename Func, typename DispatchT>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEachImpl(Func&& func, DispatchT&& dispatch)const{
        if(!m_valid)
            return {};

        const auto slices = makeSlices(IndexSequenceFor<Ts...>{});
        const EntityID* const anchorData = m_anchorEntities.data();
        const usize anchorPoolIndex = m_anchorPoolIndex;
        if constexpr(sizeof...(Ts) == 1u){
            return dispatch([slices, anchorData, &func](const usize denseIndex)noexcept(noexcept(applySlicesAtAnchor<0u, 0u>(slices, func, DeclVal<EntityID&>(), denseIndex))){
                EntityID entityId = anchorData[denseIndex];
                applySlicesAtAnchor<0u, 0u>(slices, func, entityId, denseIndex);
            });
        }
        else{
            return dispatch([slices, anchorData, anchorPoolIndex, &func](const usize denseIndex)noexcept(noexcept(tryApplySliceRuntime<0u>(slices, anchorPoolIndex, func, DeclVal<EntityID&>(), denseIndex))){
                EntityID entityId = anchorData[denseIndex];
                tryApplySliceRuntime<0u>(slices, anchorPoolIndex, func, entityId, denseIndex);
            });
        }
    }


    void initializeAnchor()noexcept{
        m_valid = true;
        m_anchorEntities = {};
        m_anchorPoolIndex = 0;
        m_count = Limit<usize>::s_Max;

        initializeAnchorImpl(IndexSequenceFor<Ts...>{});

        if(!m_valid){
            m_anchorEntities = {};
            m_count = 0;
        }
    }

    template<usize... Is>
    void initializeAnchorImpl(IndexSequence<Is...>)noexcept{
        (updateAnchor<Is>(), ...);
    }

    template<usize I>
    void updateAnchor()noexcept{
        auto* pool = Get<I>(m_pools);
        if(!pool){
            m_valid = false;
            return;
        }

        const usize poolSize = pool->size();
        if(poolSize < m_count){
            m_count = poolSize;
            m_anchorEntities = ECSDetail::ViewTupleAccess::EntityVector<I>(m_pools);
            m_anchorPoolIndex = I;
        }
    }


private:
    ComponentTuple m_pools;
    Span<const EntityID> m_anchorEntities = {};
    usize m_anchorPoolIndex = 0;
    usize m_count = 0;
    b8 m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

