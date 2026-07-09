// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_capacity_private.h>
#include <impl/ecs_render/kernel/renderer_types.h>

#include <global/core/common/log.h>
#include <impl/assets/graphics/mesh/material_typed_constants.h>
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


inline constexpr usize s_MaterialTypedWordBytes = static_cast<usize>(NWB_MATERIAL_TYPED_WORD_BYTES);

struct MaterialTypedByteRange{
    u32 byteOffset = 0;
    u32 byteCount = 0;
};

struct MaterialTypedInstanceRanges{
    MaterialTypedByteRange constantRange;
    MaterialTypedByteRange mutableRange;
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
        ::HashCombine(seed, key.bytes.size());
        return seed;
    }
};

// Content-addressed dedup map for mutable typed byte ranges within one upload buffer: identical mutable blocks
// (the common case -- many instances sharing a material's default mutable storage) share one appended range.
// Used by the material draw pass and the shadow occluder packing alike.
using MaterialTypedByteContentRangeMap = HashMap<
    MaterialTypedByteContentKey,
    MaterialTypedByteRange,
    MaterialTypedByteContentKeyHasher,
    EqualTo<MaterialTypedByteContentKey>,
    Core::Alloc::ScratchArena
>;

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
    if(!AlignUpChecked(currentByteCount, s_MaterialTypedWordBytes, alignedByteBegin)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset overflows alignment"));
        return false;
    }
    if(appendByteCount > Limit<usize>::s_Max - alignedByteBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count overflows"));
        return false;
    }

    const usize byteEnd = alignedByteBegin + appendByteCount;
    usize alignedByteEnd = 0u;
    if(!AlignUpChecked(byteEnd, s_MaterialTypedWordBytes, alignedByteEnd)){
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
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(materialTypedRanges.constantRange));
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(materialTypedRanges.mutableRange));

    InstanceGpuData data;
    if(transform){
        data.rotation = transform->rotation;
        data.translation = Float3UInt(
            transform->position.x,
            transform->position.y,
            transform->position.z,
            materialTypedRanges.mutableRange.byteOffset
        );
        data.scale = transform->scale;
    }
    else
        data.translation.w = materialTypedRanges.mutableRange.byteOffset;
    return data;
}

template<typename MaterialTypedByteVector>
[[nodiscard]] inline bool ResolveMaterialTypedUploadByteCount(
    const MaterialTypedByteVector& materialTypedBytes,
    usize& outUploadByteCount
){
    outUploadByteCount = materialTypedBytes.size();
    NWB_ASSERT_MSG(
        outUploadByteCount != 0u,
        NWB_TEXT("RendererSystem: material typed data upload is empty")
    );
    NWB_ASSERT_MSG(
        (outUploadByteCount & (s_MaterialTypedWordBytes - 1u)) == 0u,
        NWB_TEXT("RendererSystem: material typed data upload is not word-aligned")
    );

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AssertMaterialTypedUploadRange(
    const MaterialTypedByteRange& range,
    const usize uploadByteCount,
    [[maybe_unused]] const tchar* rangeName
){
    if(range.byteCount == 0u){
        NWB_ASSERT_MSG(
            MaterialTypedByteRangeEmptyOffsetValid(range),
            NWB_TEXT("RendererSystem: {} material typed byte range has zero count with nonzero offset"),
            rangeName
        );
        return;
    }

    NWB_ASSERT_MSG(
        ((range.byteOffset | range.byteCount) & static_cast<u32>(NWB_MATERIAL_TYPED_WORD_BYTES - 1u)) == 0u,
        NWB_TEXT("RendererSystem: {} material typed byte range is not word-aligned"),
        rangeName
    );

    const usize byteOffset = static_cast<usize>(range.byteOffset);
    NWB_ASSERT_MSG(
        byteOffset <= uploadByteCount,
        NWB_TEXT("RendererSystem: {} material typed byte range offset exceeds upload data"),
        rangeName
    );
    NWB_ASSERT_MSG(
        byteOffset <= uploadByteCount && static_cast<usize>(range.byteCount) <= uploadByteCount - byteOffset,
        NWB_TEXT("RendererSystem: {} material typed byte range exceeds upload data"),
        rangeName
    );
}

inline void AssertMaterialTypedInstanceRange(
    const MaterialTypedInstanceRanges& ranges,
    const usize uploadByteCount
){
    AssertMaterialTypedUploadRange(ranges.constantRange, uploadByteCount, NWB_TEXT("constant"));
    AssertMaterialTypedUploadRange(ranges.mutableRange, uploadByteCount, NWB_TEXT("mutable"));
}

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

template<typename MaterialTypedByteVector>
inline void AssertMaterialTypedUploadRanges(
    const MaterialTypedInstanceRangeVector& instanceRanges,
    const MaterialTypedByteVector& materialTypedBytes
){
    if(instanceRanges.empty())
        return;

    const usize uploadByteCount = materialTypedBytes.size();
    NWB_ASSERT_MSG(
        uploadByteCount != 0u,
        NWB_TEXT("RendererSystem: material typed data upload is empty")
    );
    NWB_ASSERT_MSG(
        (uploadByteCount & (s_MaterialTypedWordBytes - 1u)) == 0u,
        NWB_TEXT("RendererSystem: material typed data upload is not word-aligned")
    );

    for(const MaterialTypedInstanceRanges& ranges : instanceRanges){
        AssertMaterialTypedInstanceRange(ranges, uploadByteCount);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

