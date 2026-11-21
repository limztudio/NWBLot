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

    template<usize I, typename... Ts>
    static FOUNDATION_INLINE auto MakeSlice(const Tuple<ComponentPool<Ts>*...>& pools)noexcept{
        auto& pool = *MakeNotNull(Get<I>(pools));
        using CompT = RemovePointer_T<decltype(pool.m_components.data())>;
        return PoolSlice<CompT>{ pool.m_sparse.data(), pool.m_sparse.size(), pool.m_components.data() };
    }

    template<usize I, typename... Ts>
    static FOUNDATION_INLINE SparsePoolView MakeSparseView(const Tuple<ComponentPool<Ts>*...>& pools)noexcept{
        auto& pool = *MakeNotNull(Get<I>(pools));
        return SparsePoolView{ pool.m_sparse.data(), pool.m_sparse.size() };
    }
};


struct ViewSentinel{};


template<usize Count>
inline void SortProbeOrderAscending(Array<usize, Count>& order, const usize anchorPoolIndex, const Array<usize, Count>& poolSizes)noexcept{
    for(usize slot = 0u, write = 0u; slot < Count; ++slot){
        if(slot != anchorPoolIndex){
            order[write] = slot;
            ++write;
        }
    }
    const usize probeCount = Count - 1u;
    for(usize i = 1u; i < probeCount; ++i){
        const usize key = order[i];
        const usize keySize = poolSizes[key];
        usize j = i;
        while(j > 0u && poolSizes[order[j - 1u]] > keySize){
            order[j] = order[j - 1u];
            --j;
        }
        order[j] = key;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
struct ViewIterator{
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;
    using SliceTuple = Tuple<PoolSlice<Ts>...>;
    using ValueTuple = Tuple<EntityID, Ts&...>;


public:
    SliceTuple slices{};
    Array<SparsePoolView, sizeof...(Ts)> sparseViews{};
    Array<usize, sizeof...(Ts)> probeOrder{};
    const EntityID* anchorData = nullptr;
    usize anchorPoolIndex = 0u;
    usize index = 0u;
    usize count = 0u;
    Array<u32, sizeof...(Ts)> denseIndices{};
    EntityID entity = s_InvalidEntityId;


public:
    ViewIterator(
        const ComponentTuple& poolsValue,
        const Array<usize, sizeof...(Ts)>& poolSizesValue,
        const Span<const EntityID> anchorEntitiesValue,
        usize anchorPoolIndexValue,
        usize indexValue,
        usize countValue,
        b8 validValue
    )noexcept
        : anchorData(anchorEntitiesValue.data())
        , anchorPoolIndex(anchorPoolIndexValue)
        , index(indexValue)
        , count(countValue)
    {
        if(validValue && index < count){
            initSlices(poolsValue, IndexSequenceFor<Ts...>{});
            if constexpr(sizeof...(Ts) >= 3u)
                SortProbeOrderAscending<sizeof...(Ts)>(probeOrder, anchorPoolIndex, poolSizesValue);
            skipInvalid();
        }
        else{
            index = count;
        }
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
    template<usize... Is>
    FOUNDATION_INLINE void initSlices(const ComponentTuple& poolsValue, IndexSequence<Is...>)noexcept{
        slices = MakeTuple(ViewTupleAccess::MakeSlice<Is>(poolsValue)...);
        sparseViews = Array<SparsePoolView, sizeof...(Ts)>{ ViewTupleAccess::MakeSparseView<Is>(poolsValue)... };
    }

    FOUNDATION_INLINE void skipInvalid()noexcept{
        if constexpr(sizeof...(Ts) == 1u){
            if(index < count){
                entity = anchorData[index];
                denseIndices[0] = static_cast<u32>(index);
            }
            else{
                entity = s_InvalidEntityId;
            }
            return;
        }

        if constexpr(sizeof...(Ts) == 2u){
            const auto& otherSparse = (anchorPoolIndex == 0u) ? sparseViews[1] : sparseViews[0];
            const usize otherSlot = 1u - anchorPoolIndex;
            while(index < count){
                const EntityID entityId = anchorData[index];
                const u32 otherDense = otherSparse.findDenseIndex(entityId);
                if(otherDense != s_InvalidDenseIndex){
                    denseIndices[anchorPoolIndex] = static_cast<u32>(index);
                    denseIndices[otherSlot] = otherDense;
                    entity = entityId;
                    return;
                }
                ++index;
            }
            entity = s_InvalidEntityId;
            return;
        }

        while(index < count){
            const EntityID entityId = anchorData[index];
            b8 matched = true;
            for(usize k = 0u; k < sizeof...(Ts) - 1u; ++k){
                const usize slot = probeOrder[k];
                const u32 d = sparseViews[slot].findDenseIndex(entityId);
                if(d == s_InvalidDenseIndex){
                    matched = false;
                    break;
                }
                denseIndices[slot] = d;
            }
            if(matched){
                denseIndices[anchorPoolIndex] = static_cast<u32>(index);
                entity = entityId;
                return;
            }
            ++index;
        }

        entity = s_InvalidEntityId;
    }


    template<usize... Is>
    FOUNDATION_INLINE ValueTuple deref(IndexSequence<Is...>)const noexcept{
        return ::ForwardAsTuple(entity, Get<Is>(slices).components[denseIndices[Is]]...);
    }
};
template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator==(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b)noexcept{ return a.index == b.index; }

template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator!=(const ViewIterator<Ts...>& a, const ViewIterator<Ts...>& b)noexcept{ return a.index != b.index; }

template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator==(const ViewIterator<Ts...>& it, ViewSentinel)noexcept{ return it.index >= it.count; }

template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator!=(const ViewIterator<Ts...>& it, ViewSentinel)noexcept{ return it.index < it.count; }

template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator==(ViewSentinel, const ViewIterator<Ts...>& it)noexcept{ return it.index >= it.count; }

template<typename... Ts>
[[nodiscard]] FOUNDATION_INLINE b8 operator!=(ViewSentinel, const ViewIterator<Ts...>& it)noexcept{ return it.index < it.count; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename... Ts>
class View{
    static_assert(sizeof...(Ts) > 0, "View requires at least one component type");


public:
    using IteratorType = ECSDetail::ViewIterator<Ts...>;
    using SentinelType = ECSDetail::ViewSentinel;
    using ComponentTuple = Tuple<ComponentPool<Ts>*...>;


public:
    View(ComponentTuple pools)noexcept
        : m_pools(pools)
    {
        initializeAnchor();
    }


public:
    IteratorType begin()const noexcept{
        return IteratorType(m_pools, m_poolSizes, m_anchorEntities, m_anchorPoolIndex, 0, m_count, m_valid);
    }

    SentinelType end()const noexcept{
        return SentinelType{};
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
            case 0u:
                if(m_poolSizes[1u] <= m_poolSizes[2u])
                    runAnchorLoop3<0u, 1u, 2u>(anchorData, count, slices, func);
                else
                    runAnchorLoop3<0u, 2u, 1u>(anchorData, count, slices, func);
                break;
            case 1u:
                if(m_poolSizes[0u] <= m_poolSizes[2u])
                    runAnchorLoop3<1u, 0u, 2u>(anchorData, count, slices, func);
                else
                    runAnchorLoop3<1u, 2u, 0u>(anchorData, count, slices, func);
                break;
            default:
                if(m_poolSizes[0u] <= m_poolSizes[1u])
                    runAnchorLoop3<2u, 0u, 1u>(anchorData, count, slices, func);
                else
                    runAnchorLoop3<2u, 1u, 0u>(anchorData, count, slices, func);
                break;
            }
        }
        else{
            const auto sparseViews = makeSparseViews(IndexSequenceFor<Ts...>{});
            Array<usize, sizeof...(Ts)> probeOrder{};
            ECSDetail::SortProbeOrderAscending<sizeof...(Ts)>(probeOrder, m_anchorPoolIndex, m_poolSizes);
            runAnchorLoopOrdered(anchorData, count, m_anchorPoolIndex, probeOrder, sparseViews, slices, func);
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

    template<usize... Is>
    FOUNDATION_INLINE Array<ECSDetail::SparsePoolView, sizeof...(Ts)> makeSparseViews(IndexSequence<Is...>)const noexcept{
        return { ECSDetail::ViewTupleAccess::MakeSparseView<Is>(m_pools)... };
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

    template<usize AnchorIdx, usize Probe1, usize Probe2, typename SliceTupleT, typename Func>
    static void runAnchorLoop3(const EntityID* const anchorData, const usize count, const SliceTupleT& slices, Func& func)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        const auto& s1 = Get<Probe1>(slices);
        const auto& s2 = Get<Probe2>(slices);
        for(usize i = 0u; i < count; ++i){
            EntityID entityId = anchorData[i];
            const u32 d1 = s1.findDenseIndex(entityId);
            if(d1 == ECSDetail::s_InvalidDenseIndex)
                continue;
            const u32 d2 = s2.findDenseIndex(entityId);
            if(d2 == ECSDetail::s_InvalidDenseIndex)
                continue;
            Array<u32, 3u> d{};
            d[AnchorIdx] = static_cast<u32>(i);
            d[Probe1] = d1;
            d[Probe2] = d2;
            func(entityId, Get<0>(slices).components[d[0]], Get<1>(slices).components[d[1]], Get<2>(slices).components[d[2]]);
        }
    }

    template<typename SliceTupleT, typename Func, usize... Is>
    static FOUNDATION_INLINE void invokeWithDenseIndices(EntityID entityId, const SliceTupleT& slices, const Array<u32, sizeof...(Ts)>& d, Func& func, IndexSequence<Is...>)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        func(entityId, Get<Is>(slices).components[d[Is]]...);
    }

    template<typename SliceTupleT, typename Func>
    static void runAnchorLoopOrdered(
        const EntityID* const anchorData,
        const usize count,
        const usize anchorPoolIndex,
        const Array<usize, sizeof...(Ts)>& probeOrder,
        const Array<ECSDetail::SparsePoolView, sizeof...(Ts)>& sparseViews,
        const SliceTupleT& slices,
        Func& func
    )noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        Array<u32, sizeof...(Ts)> d{};
        for(usize i = 0u; i < count; ++i){
            EntityID entityId = anchorData[i];
            b8 matched = true;
            for(usize k = 0u; k < sizeof...(Ts) - 1u; ++k){
                const usize slot = probeOrder[k];
                const u32 found = sparseViews[slot].findDenseIndex(entityId);
                if(found == ECSDetail::s_InvalidDenseIndex){
                    matched = false;
                    break;
                }
                d[slot] = found;
            }
            if(!matched)
                continue;
            d[anchorPoolIndex] = static_cast<u32>(i);
            invokeWithDenseIndices(entityId, slices, d, func, IndexSequenceFor<Ts...>{});
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
            return dispatch([slices, anchorData, &func](const usize denseIndex)noexcept(noexcept(DeclVal<Func&>()(DeclVal<const EntityID&>(), DeclVal<Ts&>()...))){
                const EntityID entityId = anchorData[denseIndex];
                func(entityId, Get<0>(slices).components[denseIndex]);
            });
        }
        else if constexpr(sizeof...(Ts) == 2u){
            if(anchorPoolIndex == 0u){
                return dispatch([slices, anchorData, &func](const usize denseIndex)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                    EntityID entityId = anchorData[denseIndex];
                    const u32 otherDense = Get<1>(slices).findDenseIndex(entityId);
                    if(otherDense == ECSDetail::s_InvalidDenseIndex)
                        return;
                    func(entityId, Get<0>(slices).components[denseIndex], Get<1>(slices).components[otherDense]);
                });
            }
            return dispatch([slices, anchorData, &func](const usize denseIndex)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                EntityID entityId = anchorData[denseIndex];
                const u32 otherDense = Get<0>(slices).findDenseIndex(entityId);
                if(otherDense == ECSDetail::s_InvalidDenseIndex)
                    return;
                func(entityId, Get<0>(slices).components[otherDense], Get<1>(slices).components[denseIndex]);
            });
        }
        else{
            const auto sparseViews = makeSparseViews(IndexSequenceFor<Ts...>{});
            Array<usize, sizeof...(Ts)> probeOrder{};
            ECSDetail::SortProbeOrderAscending<sizeof...(Ts)>(probeOrder, anchorPoolIndex, m_poolSizes);
            return dispatch([slices, anchorData, anchorPoolIndex, probeOrder, sparseViews, &func](const usize denseIndex)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                EntityID entityId = anchorData[denseIndex];
                Array<u32, sizeof...(Ts)> d{};
                for(usize k = 0u; k < sizeof...(Ts) - 1u; ++k){
                    const usize slot = probeOrder[k];
                    const u32 found = sparseViews[slot].findDenseIndex(entityId);
                    if(found == ECSDetail::s_InvalidDenseIndex)
                        return;
                    d[slot] = found;
                }
                d[anchorPoolIndex] = static_cast<u32>(denseIndex);
                invokeWithDenseIndices(entityId, slices, d, func, IndexSequenceFor<Ts...>{});
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
        m_poolSizes[I] = poolSize;
        if(poolSize < m_count){
            m_count = poolSize;
            m_anchorEntities = ECSDetail::ViewTupleAccess::EntityVector<I>(m_pools);
            m_anchorPoolIndex = I;
        }
    }


private:
    ComponentTuple m_pools;
    Array<usize, sizeof...(Ts)> m_poolSizes{};
    Span<const EntityID> m_anchorEntities = {};
    usize m_anchorPoolIndex = 0;
    usize m_count = 0;
    b8 m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

