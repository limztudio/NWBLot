// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "capacity_private.h"
#include "system.h"

#include <core/common/log.h>
#include <impl/ecs_scene/components.h>

#include <global/algorithm.h>
#include <global/assert.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MaterialTypedByteRange{
    u32 byteOffset = 0;
    u32 byteCount = 0;
};

struct MaterialTypedInstanceRanges{
    MaterialTypedByteRange constantRange;
    MaterialTypedByteRange mutableRange;
};

struct MaterialTypedInstanceRangeVector{
    explicit MaterialTypedInstanceRangeVector(Core::Alloc::ScratchArena& arena)
        : m_ranges(arena)
    {}

    [[nodiscard]] bool empty()const{ return m_ranges.empty(); }
    [[nodiscard]] usize size()const{ return m_ranges.size(); }
    void reserve(const usize count){ m_ranges.reserve(count); }
    void push_back(const MaterialTypedInstanceRanges& ranges){ m_ranges.push_back(ranges); }

    [[nodiscard]] auto begin()const{ return m_ranges.begin(); }
    [[nodiscard]] auto end()const{ return m_ranges.end(); }

private:
    Vector<MaterialTypedInstanceRanges, Core::Alloc::ScratchArena> m_ranges;
};

struct MaterialTypedByteAppendRange{
    MaterialTypedByteRange byteRange;
    usize alignedByteEnd = 0u;
};

struct MaterialTypedByteContentKey{
    u64 byteHash = 0u;
    Vector<u8, Core::Alloc::ScratchArena> bytes;

    template<typename MaterialTypedByteVector>
    MaterialTypedByteContentKey(Core::Alloc::ScratchArena& arena, const MaterialTypedByteVector& typedBytes)
        : byteHash(ComputeFnv64Bytes(typedBytes.data(), typedBytes.size()))
        , bytes(arena)
    {
        bytes.assign(typedBytes.begin(), typedBytes.end());
    }

    friend bool operator==(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs){
        if(lhs.byteHash != rhs.byteHash || lhs.bytes.size() != rhs.bytes.size())
            return false;
        if(lhs.bytes.empty())
            return true;

        return NWB_MEMCMP(lhs.bytes.data(), rhs.bytes.data(), lhs.bytes.size()) == 0;
    }
};

struct MaterialTypedByteContentKeyHasher{
    usize operator()(const MaterialTypedByteContentKey& key)const{
        usize seed = Hasher<u64>{}(key.byteHash);
        Core::CoreDetail::HashCombine(seed, key.bytes.size());
        return seed;
    }
};

[[nodiscard]] inline bool TryBuildMaterialTypedByteAppendRange(
    const usize currentByteCount,
    const usize appendByteCount,
    MaterialTypedByteAppendRange& outAppendRange
){
    outAppendRange = {};
    if(appendByteCount == 0u)
        return true;

    if(appendByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte count exceeds u32 limits"));
        return false;
    }

    usize alignedByteBegin = 0u;
    if(!AlignUpChecked(currentByteCount, sizeof(u32), alignedByteBegin)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset overflows alignment"));
        return false;
    }
    if(appendByteCount > Limit<usize>::s_Max - alignedByteBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count overflows"));
        return false;
    }

    const usize byteEnd = alignedByteBegin + appendByteCount;
    usize alignedByteEnd = 0u;
    if(!AlignUpChecked(byteEnd, sizeof(u32), alignedByteEnd)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte end overflows alignment"));
        return false;
    }
    if(alignedByteBegin > static_cast<usize>(Limit<u32>::s_Max) || alignedByteEnd > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count exceeds u32 limits"));
        return false;
    }

    outAppendRange.byteRange.byteOffset = static_cast<u32>(alignedByteBegin);
    outAppendRange.byteRange.byteCount = static_cast<u32>(appendByteCount);
    outAppendRange.alignedByteEnd = alignedByteEnd;
    return true;
}

template<typename DestinationByteVector, typename SourceByteVector>
[[nodiscard]] inline bool AppendMaterialTypedByteRange(
    DestinationByteVector& materialTypedBytes,
    const SourceByteVector& typedBytes,
    MaterialTypedByteRange& outRange
){
    MaterialTypedByteAppendRange appendRange;
    if(!TryBuildMaterialTypedByteAppendRange(
        materialTypedBytes.size(),
        typedBytes.size(),
        appendRange
    ))
        return false;

    outRange = appendRange.byteRange;
    if(typedBytes.empty())
        return true;

    const usize requiredTypedByteCapacity = appendRange.alignedByteEnd;
    if(requiredTypedByteCapacity > materialTypedBytes.capacity())
        materialTypedBytes.reserve(NextGrowingCapacity(
            materialTypedBytes.capacity(),
            requiredTypedByteCapacity
        ));
    materialTypedBytes.resize(outRange.byteOffset, 0u);
    AppendTriviallyCopyableVector(materialTypedBytes, typedBytes);
    materialTypedBytes.resize(appendRange.alignedByteEnd, 0u);

    return true;
}

template<typename DestinationByteVector, typename SourceByteVector, typename MaterialTypedByteRangeMap>
[[nodiscard]] inline bool FindOrAppendMaterialTypedByteRange(
    DestinationByteVector& materialTypedBytes,
    MaterialTypedByteRangeMap& rangeMap,
    const SourceByteVector& typedBytes,
    MaterialTypedByteRange& outRange
){
    outRange = {};
    if(typedBytes.empty())
        return true;

    MaterialTypedByteContentKey rangeKey(materialTypedBytes.get_allocator().arena(), typedBytes);
    const auto foundRange = rangeMap.find(rangeKey);
    if(foundRange != rangeMap.end()){
        outRange = foundRange.value();
        return true;
    }

    if(!AppendMaterialTypedByteRange(materialTypedBytes, typedBytes, outRange))
        return false;

    rangeMap.emplace(Move(rangeKey), outRange);
    return true;
}

[[nodiscard]] inline bool MaterialTypedByteRangeEmptyOffsetValid(const MaterialTypedByteRange& range){
    return range.byteCount != 0u || range.byteOffset == 0u;
}

inline InstanceGpuData BuildInstanceGpuData(
    const NWB::Impl::Scene::TransformComponent* transform,
    const MaterialTypedInstanceRanges& materialTypedRanges
){
#if defined(NWB_DEBUG)
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(materialTypedRanges.constantRange));
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(materialTypedRanges.mutableRange));
#endif

    InstanceGpuData data;
    data.materialTypedByteOffsets.x = materialTypedRanges.constantRange.byteOffset;
    data.materialTypedByteOffsets.y = materialTypedRanges.mutableRange.byteOffset;
    if(!transform)
        return data;

    data.rotation = transform->rotation;
    data.translation = transform->position;
    data.scale = transform->scale;
    return data;
}

template<typename MaterialTypedByteVector>
[[nodiscard]] inline bool ResolveMaterialTypedUploadByteCount(
    const MaterialTypedByteVector& materialTypedBytes,
    usize& outUploadByteCount
){
    outUploadByteCount = 0u;
    if(materialTypedBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload is empty"));
        return false;
    }
    if((materialTypedBytes.size() & (sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload is not word-aligned"));
        return false;
    }

    outUploadByteCount = materialTypedBytes.size();
    if(!AlignUpChecked(outUploadByteCount, sizeof(u32), outUploadByteCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed data upload size overflows alignment"));
        return false;
    }

    return true;
}

[[nodiscard]] inline bool ValidateMaterialTypedUploadRange(
    const MaterialTypedByteRange& range,
    const usize uploadByteCount,
    const tchar* rangeName
){
    if(range.byteCount == 0u){
        if(MaterialTypedByteRangeEmptyOffsetValid(range))
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} material typed byte range has zero count with nonzero offset"), rangeName);
        return false;
    }

    if(((range.byteOffset | range.byteCount) & static_cast<u32>(sizeof(u32) - 1u)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} material typed byte range is not word-aligned"), rangeName);
        return false;
    }

    const usize byteOffset = static_cast<usize>(range.byteOffset);
    if(byteOffset > uploadByteCount || static_cast<usize>(range.byteCount) > uploadByteCount - byteOffset){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: {} material typed byte range exceeds upload data"), rangeName);
        return false;
    }

    return true;
}

[[nodiscard]] inline bool ValidateMaterialTypedInstanceRange(
    const MaterialTypedInstanceRanges& ranges,
    const usize uploadByteCount
){
    if(!ValidateMaterialTypedUploadRange(ranges.constantRange, uploadByteCount, NWB_TEXT("constant")))
        return false;
    if(!ValidateMaterialTypedUploadRange(ranges.mutableRange, uploadByteCount, NWB_TEXT("mutable")))
        return false;

    return true;
}

template<typename MaterialTypedByteVector>
[[nodiscard]] inline bool ValidateMaterialTypedUploadRanges(
    const MaterialTypedInstanceRangeVector& instanceRanges,
    const MaterialTypedByteVector& materialTypedBytes
){
    if(instanceRanges.empty())
        return true;

    usize uploadByteCount = 0u;
    if(!ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadByteCount))
        return false;

    for(const MaterialTypedInstanceRanges& ranges : instanceRanges){
        if(!ValidateMaterialTypedInstanceRange(ranges, uploadByteCount))
            return false;
    }

    return true;
}

struct MaterialTypedInstanceRangeCollector{
    explicit MaterialTypedInstanceRangeCollector(Core::Alloc::ScratchArena& arena)
#if defined(NWB_DEBUG)
        : m_ranges(arena)
#endif
    {
#if !defined(NWB_DEBUG)
        static_cast<void>(arena);
#endif
    }

    void reserve(const usize count){
#if defined(NWB_DEBUG)
        m_ranges.reserve(count);
#else
        static_cast<void>(count);
#endif
    }

    void push_back(const MaterialTypedInstanceRanges& ranges){
#if defined(NWB_DEBUG)
        m_ranges.push_back(ranges);
#else
        static_cast<void>(ranges);
#endif
    }

    template<typename MaterialTypedByteVector>
    [[nodiscard]] bool uploadRangesReady(
        const usize instanceCount,
        const MaterialTypedByteVector& materialTypedBytes
    )const{
#if defined(NWB_DEBUG)
        NWB_ASSERT(instanceCount == m_ranges.size());
        return ValidateMaterialTypedUploadRanges(m_ranges, materialTypedBytes);
#else
        static_cast<void>(instanceCount);
        static_cast<void>(materialTypedBytes);
        return true;
#endif
    }

private:
#if defined(NWB_DEBUG)
    MaterialTypedInstanceRangeVector m_ranges;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
