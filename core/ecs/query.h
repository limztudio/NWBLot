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
    const u64* entityMasks = nullptr;
    usize entityMaskCount = 0u;
    u64 requiredMask = 0u;
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
        b8 validValue,
        const u64* entityMasksValue = nullptr,
        usize entityMaskCountValue = 0u,
        u64 requiredMaskValue = 0u
    )noexcept
        : anchorData(anchorEntitiesValue.data())
        , entityMasks(entityMasksValue)
        , entityMaskCount(entityMaskCountValue)
        , requiredMask(requiredMaskValue)
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
        const b8 useMask = entityMasks != nullptr && requiredMask != 0u;
        while(index < count){
            const EntityID entityId = anchorData[index];
            if(useMask){
                const usize entityIndex = static_cast<usize>(entityId.index());
                if(entityIndex >= entityMaskCount || (entityMasks[entityIndex] & requiredMask) != requiredMask){
                    ++index;
                    continue;
                }
            }
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

template<typename T0>
struct ViewIterator<T0>{
    using ComponentTuple = Tuple<ComponentPool<T0>*>;
    using ValueTuple = Tuple<EntityID, T0&>;


public:
    const EntityID* anchorData = nullptr;
    T0* components = nullptr;
    usize index = 0u;
    usize count = 0u;


public:
    ViewIterator(
        const ComponentTuple& poolsValue,
        const Array<usize, 1u>&,
        const Span<const EntityID> anchorEntitiesValue,
        usize,
        usize indexValue,
        usize countValue,
        b8 validValue,
        const u64* = nullptr,
        usize = 0u,
        u64 = 0u
    )noexcept
        : anchorData(anchorEntitiesValue.data())
        , index(validValue && indexValue < countValue ? indexValue : countValue)
        , count(countValue)
    {
        if(validValue && index < count)
            components = ViewTupleAccess::MakeSlice<0u>(poolsValue).components;
    }


public:
    ValueTuple operator*()const noexcept{
        return ValueTuple(anchorData[index], components[index]);
    }

    ViewIterator& operator++()noexcept{
        ++index;
        return *this;
    }
};

template<typename T0, typename T1>
struct ViewIterator<T0, T1>{
    using ComponentTuple = Tuple<ComponentPool<T0>*, ComponentPool<T1>*>;
    using ValueTuple = Tuple<EntityID, T0&, T1&>;


public:
    PoolSlice<T0> s0{};
    PoolSlice<T1> s1{};
    const EntityID* anchorData = nullptr;
    usize index = 0u;
    usize count = 0u;
    EntityID entity = s_InvalidEntityId;
    u32 otherDense = 0u;
    b8 anchorIsFirst = true;


public:
    ViewIterator(
        const ComponentTuple& poolsValue,
        const Array<usize, 2u>&,
        const Span<const EntityID> anchorEntitiesValue,
        usize anchorPoolIndexValue,
        usize indexValue,
        usize countValue,
        b8 validValue,
        const u64* = nullptr,
        usize = 0u,
        u64 = 0u
    )noexcept
        : anchorData(anchorEntitiesValue.data())
        , index(indexValue)
        , count(countValue)
        , anchorIsFirst(anchorPoolIndexValue == 0u)
    {
        if(validValue && index < count){
            s0 = ViewTupleAccess::MakeSlice<0u>(poolsValue);
            s1 = ViewTupleAccess::MakeSlice<1u>(poolsValue);
            skipInvalid();
        }
        else{
            index = count;
        }
    }


public:
    ValueTuple operator*()const noexcept{
        if(anchorIsFirst)
            return ValueTuple(entity, s0.components[index], s1.components[otherDense]);
        return ValueTuple(entity, s0.components[otherDense], s1.components[index]);
    }

    ViewIterator& operator++()noexcept{
        ++index;
        skipInvalid();
        return *this;
    }


public:
    FOUNDATION_INLINE void skipInvalid()noexcept{
        if(anchorIsFirst){
            while(index < count){
                const EntityID entityId = anchorData[index];
                const u32 found = s1.findDenseIndex(entityId);
                if(found != s_InvalidDenseIndex){
                    otherDense = found;
                    entity = entityId;
                    return;
                }
                ++index;
            }
        }
        else{
            while(index < count){
                const EntityID entityId = anchorData[index];
                const u32 found = s0.findDenseIndex(entityId);
                if(found != s_InvalidDenseIndex){
                    otherDense = found;
                    entity = entityId;
                    return;
                }
                ++index;
            }
        }
        entity = s_InvalidEntityId;
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

    View(ComponentTuple pools, const u64* entityMasks, usize entityMaskCount, u64 requiredMask)noexcept
        : m_pools(pools)
        , m_entityMasks(entityMasks)
        , m_entityMaskCount(entityMaskCount)
        , m_requiredMask(requiredMask)
    {
        initializeAnchor();
    }


public:
    IteratorType begin()const noexcept{
        return IteratorType(m_pools, m_poolSizes, m_anchorEntities, m_anchorPoolIndex, 0, m_count, m_valid, m_entityMasks, m_entityMaskCount, m_requiredMask);
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
        if(!m_valid || m_count == 0u)
            return;

        const auto slices = makeSlices(IndexSequenceFor<Ts...>{});
        const EntityID* const anchorData = m_anchorEntities.data();
        const usize count = m_count;
        if constexpr(sizeof...(Ts) == 1u){
            runAnchorLoop1Range(anchorData, 0u, count, slices, func);
        }
        else if constexpr(sizeof...(Ts) == 2u){
            if(m_anchorPoolIndex == 0u)
                runAnchorLoop2Range<0u>(anchorData, 0u, count, slices, func);
            else
                runAnchorLoop2Range<1u>(anchorData, 0u, count, slices, func);
        }
        else if constexpr(sizeof...(Ts) == 3u){
            if(hasFastMaskPrefilter())
                dispatchAnchor3Range<true>(anchorData, 0u, count, slices, func);
            else
                dispatchAnchor3Range<false>(anchorData, 0u, count, slices, func);
        }
        else{
            const auto sparseViews = makeSparseViews(IndexSequenceFor<Ts...>{});
            Array<usize, sizeof...(Ts)> probeOrder{};
            ECSDetail::SortProbeOrderAscending<sizeof...(Ts)>(probeOrder, m_anchorPoolIndex, m_poolSizes);
            if(hasFastMaskPrefilter())
                runAnchorLoopOrderedRange<true>(anchorData, 0u, count, m_anchorPoolIndex, probeOrder, sparseViews, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            else
                runAnchorLoopOrderedRange<false>(anchorData, 0u, count, m_anchorPoolIndex, probeOrder, sparseViews, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
        }
    }

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEach(CpuTaskScope& tasks, Func&& func)const{
        constexpr usize s_DefaultParallelEachGrainSize = 64u;
        return parallelEachImpl(tasks, s_DefaultParallelEachGrainSize, {}, func);
    }

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEach(CpuTaskScope& tasks, usize grainSize, Func&& func, CpuTaskOptions options = {})const{
        return parallelEachImpl(tasks, grainSize, options, func);
    }


private:
    [[nodiscard]] FOUNDATION_INLINE b8 hasFastMaskPrefilter()const noexcept{
        return m_entityMasks != nullptr && m_requiredMask != 0u;
    }

    template<usize... Is>
    FOUNDATION_INLINE auto makeSlices(IndexSequence<Is...>)const noexcept{
        return MakeTuple(ECSDetail::ViewTupleAccess::MakeSlice<Is>(m_pools)...);
    }

    template<usize... Is>
    FOUNDATION_INLINE Array<ECSDetail::SparsePoolView, sizeof...(Ts)> makeSparseViews(IndexSequence<Is...>)const noexcept{
        return { ECSDetail::ViewTupleAccess::MakeSparseView<Is>(m_pools)... };
    }

    template<typename SliceTupleT, typename Func>
    static FOUNDATION_INLINE void runAnchorLoop1Range(const EntityID* const anchorData, const usize first, const usize last, const SliceTupleT& slices, Func& func)noexcept(noexcept(DeclVal<Func&>()(DeclVal<const EntityID&>(), DeclVal<Ts&>()...))){
        auto* const c0 = Get<0>(slices).components;
        for(usize i = first; i < last; ++i)
            func(anchorData[i], c0[i]);
    }

    template<usize AnchorIdx, typename SliceTupleT, typename Func>
    static FOUNDATION_INLINE void runAnchorLoop2Range(const EntityID* const anchorData, const usize first, const usize last, const SliceTupleT& slices, Func& func)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        constexpr usize OtherIdx = 1u - AnchorIdx;
        auto* const cAnchor = Get<AnchorIdx>(slices).components;
        const auto& sOther = Get<OtherIdx>(slices);
        for(usize i = first; i < last; ++i){
            EntityID entityId = anchorData[i];
            const u32 otherDense = sOther.findDenseIndex(entityId);
            if(otherDense == ECSDetail::s_InvalidDenseIndex)
                continue;
            if constexpr(AnchorIdx == 0u)
                func(entityId, cAnchor[i], sOther.components[otherDense]);
            else
                func(entityId, sOther.components[otherDense], cAnchor[i]);
        }
    }

    template<usize AnchorIdx, usize Probe1, usize Probe2, b8 UseMask, typename SliceTupleT, typename Func>
    static void runAnchorLoop3Range(
        const EntityID* const anchorData,
        const usize first,
        const usize last,
        const SliceTupleT& slices,
        const u64* const entityMasks,
        const usize entityMaskCount,
        const u64 requiredMask,
        Func& func
    )noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        const auto& s1 = Get<Probe1>(slices);
        const auto& s2 = Get<Probe2>(slices);
        auto* const c0 = Get<0>(slices).components;
        auto* const c1 = Get<1>(slices).components;
        auto* const c2 = Get<2>(slices).components;
        for(usize i = first; i < last; ++i){
            EntityID entityId = anchorData[i];
            if constexpr(UseMask){
                const usize entityIndex = static_cast<usize>(entityId.index());
                if(entityIndex >= entityMaskCount || (entityMasks[entityIndex] & requiredMask) != requiredMask)
                    continue;
            }
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
            func(entityId, c0[d[0]], c1[d[1]], c2[d[2]]);
        }
    }

    template<b8 UseMask, typename SliceTupleT, typename Func>
    void dispatchAnchor3Range(const EntityID* const anchorData, const usize first, const usize last, const SliceTupleT& slices, Func& func)const noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        switch(m_anchorPoolIndex){
        case 0u:
            if(m_poolSizes[1u] <= m_poolSizes[2u])
                runAnchorLoop3Range<0u, 1u, 2u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            else
                runAnchorLoop3Range<0u, 2u, 1u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            break;
        case 1u:
            if(m_poolSizes[0u] <= m_poolSizes[2u])
                runAnchorLoop3Range<1u, 0u, 2u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            else
                runAnchorLoop3Range<1u, 2u, 0u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            break;
        default:
            if(m_poolSizes[0u] <= m_poolSizes[1u])
                runAnchorLoop3Range<2u, 0u, 1u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            else
                runAnchorLoop3Range<2u, 1u, 0u, UseMask>(anchorData, first, last, slices, m_entityMasks, m_entityMaskCount, m_requiredMask, func);
            break;
        }
    }

    template<typename SliceTupleT, typename Func, usize... Is>
    static FOUNDATION_INLINE void invokeWithDenseIndices(EntityID entityId, const SliceTupleT& slices, const Array<u32, sizeof...(Ts)>& d, Func& func, IndexSequence<Is...>)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        func(entityId, Get<Is>(slices).components[d[Is]]...);
    }

    template<b8 UseMask, typename SliceTupleT, typename Func>
    static void runAnchorLoopOrderedRange(
        const EntityID* const anchorData,
        const usize first,
        const usize last,
        const usize anchorPoolIndex,
        const Array<usize, sizeof...(Ts)>& probeOrder,
        const Array<ECSDetail::SparsePoolView, sizeof...(Ts)>& sparseViews,
        const SliceTupleT& slices,
        const u64* const entityMasks,
        const usize entityMaskCount,
        const u64 requiredMask,
        Func& func
    )noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
        Array<u32, sizeof...(Ts)> d{};
        for(usize i = first; i < last; ++i){
            EntityID entityId = anchorData[i];
            if constexpr(UseMask){
                const usize entityIndex = static_cast<usize>(entityId.index());
                if(entityIndex >= entityMaskCount || (entityMasks[entityIndex] & requiredMask) != requiredMask)
                    continue;
            }
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

    template<typename Func>
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> parallelEachImpl(
        CpuTaskScope& tasks,
        const usize grainSize,
        const CpuTaskOptions& options,
        Func& func
    )const{
        if(!m_valid || m_count == 0u)
            return {};

        const auto slices = makeSlices(IndexSequenceFor<Ts...>{});
        const EntityID* const anchorData = m_anchorEntities.data();
        if constexpr(sizeof...(Ts) == 1u){
            return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<const EntityID&>(), DeclVal<Ts&>()...))){
                runAnchorLoop1Range(anchorData, first, last, slices, func);
            }, options);
        }
        else if constexpr(sizeof...(Ts) == 2u){
            if(m_anchorPoolIndex == 0u){
                return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                    runAnchorLoop2Range<0u>(anchorData, first, last, slices, func);
                }, options);
            }
            return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                runAnchorLoop2Range<1u>(anchorData, first, last, slices, func);
            }, options);
        }
        else if constexpr(sizeof...(Ts) == 3u){
            const u64* const entityMasks = m_entityMasks;
            const usize entityMaskCount = m_entityMaskCount;
            const u64 requiredMask = m_requiredMask;
            auto dispatchPerm = [&]<usize AnchorIdx, usize Probe1, usize Probe2>() -> Expected<void, CpuTaskAdmissionError::Enum>{
                if(hasFastMaskPrefilter()){
                    return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, entityMasks, entityMaskCount, requiredMask, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                        runAnchorLoop3Range<AnchorIdx, Probe1, Probe2, true>(anchorData, first, last, slices, entityMasks, entityMaskCount, requiredMask, func);
                    }, options);
                }
                return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                    runAnchorLoop3Range<AnchorIdx, Probe1, Probe2, false>(anchorData, first, last, slices, nullptr, 0u, 0u, func);
                }, options);
            };
            switch(m_anchorPoolIndex){
            case 0u:
                return (m_poolSizes[1u] <= m_poolSizes[2u]) ? dispatchPerm.template operator()<0u, 1u, 2u>() : dispatchPerm.template operator()<0u, 2u, 1u>();
            case 1u:
                return (m_poolSizes[0u] <= m_poolSizes[2u]) ? dispatchPerm.template operator()<1u, 0u, 2u>() : dispatchPerm.template operator()<1u, 2u, 0u>();
            default:
                return (m_poolSizes[0u] <= m_poolSizes[1u]) ? dispatchPerm.template operator()<2u, 0u, 1u>() : dispatchPerm.template operator()<2u, 1u, 0u>();
            }
        }
        else{
            const usize anchorPoolIndex = m_anchorPoolIndex;
            const auto sparseViews = makeSparseViews(IndexSequenceFor<Ts...>{});
            Array<usize, sizeof...(Ts)> probeOrder{};
            ECSDetail::SortProbeOrderAscending<sizeof...(Ts)>(probeOrder, anchorPoolIndex, m_poolSizes);
            const u64* const entityMasks = m_entityMasks;
            const usize entityMaskCount = m_entityMaskCount;
            const u64 requiredMask = m_requiredMask;
            if(hasFastMaskPrefilter()){
                return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, anchorPoolIndex, probeOrder, sparseViews, entityMasks, entityMaskCount, requiredMask, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                    runAnchorLoopOrderedRange<true>(anchorData, first, last, anchorPoolIndex, probeOrder, sparseViews, slices, entityMasks, entityMaskCount, requiredMask, func);
                }, options);
            }
            return tasks.parallelRange(0u, m_count, grainSize, [slices, anchorData, anchorPoolIndex, probeOrder, sparseViews, &func](const usize first, const usize last)noexcept(noexcept(DeclVal<Func&>()(DeclVal<EntityID&>(), DeclVal<Ts&>()...))){
                runAnchorLoopOrderedRange<false>(anchorData, first, last, anchorPoolIndex, probeOrder, sparseViews, slices, nullptr, 0u, 0u, func);
            }, options);
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
    const u64* m_entityMasks = nullptr;
    usize m_entityMaskCount = 0u;
    u64 m_requiredMask = 0u;
    usize m_anchorPoolIndex = 0;
    usize m_count = 0;
    b8 m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

