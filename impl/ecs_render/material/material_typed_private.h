// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/renderer_mesh_types.h>

#include <core/common/log.h>
#include <impl/assets/graphics/mesh/material_typed_constants.h>
#include <impl/ecs_scene/components.h>

#include <global/algorithm.h>
#include <global/assert.h>
#include <global/compile.h>
#include <global/containers.h>
#include <global/hash_utils.h>
#include <global/span.h>


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

struct MaterialTypedByteContentLookup{
    u64 byteHash = 0u;
    Span<const u8> bytes;
};

struct MaterialTypedByteContentKey{
    u64 byteHash = 0u;
    Vector<u8, Core::Alloc::ScratchArena> bytes;

    MaterialTypedByteContentKey(Core::Alloc::ScratchArena& arena, const MaterialTypedByteContentLookup& lookup)
        : byteHash(lookup.byteHash)
        , bytes(arena)
    {
        if(!lookup.bytes.empty()){
            bytes.reserve(lookup.bytes.size());
            bytes.assign(lookup.bytes.begin(), lookup.bytes.end());
        }
    }
};

inline bool operator==(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs){
    if(lhs.byteHash != rhs.byteHash || lhs.bytes.size() != rhs.bytes.size())
        return false;
    if(lhs.bytes.empty())
        return true;

    return NWB_MEMCMP(lhs.bytes.data(), rhs.bytes.data(), lhs.bytes.size()) == 0;
}

struct MaterialTypedByteContentKeyHasher{
    usize operator()(const MaterialTypedByteContentKey& key)const{
        usize seed = Hasher<u64>{}(key.byteHash);
        ::HashCombine(seed, key.bytes.size());
        return seed;
    }

    usize operator()(const MaterialTypedByteContentLookup& lookup)const{
        usize seed = Hasher<u64>{}(lookup.byteHash);
        ::HashCombine(seed, lookup.bytes.size());
        return seed;
    }
};

struct MaterialTypedByteContentKeyEqual{
    using is_transparent = void;

    bool operator()(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs)const{
        return lhs == rhs;
    }

    bool operator()(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentLookup& rhs)const{
        if(lhs.byteHash != rhs.byteHash || lhs.bytes.size() != rhs.bytes.size())
            return false;
        if(lhs.bytes.empty())
            return true;

        return NWB_MEMCMP(lhs.bytes.data(), rhs.bytes.data(), lhs.bytes.size()) == 0;
    }
};

// Dedup map for mutable typed ranges; identical blocks share one range.
// Used by the material draw pass and the shadow occluder packing alike.
using MaterialTypedByteContentRangeMap = HashMap<MaterialTypedByteContentKey, MaterialTypedByteRange, Core::Alloc::ScratchArena, MaterialTypedByteContentKeyHasher, MaterialTypedByteContentKeyEqual>;

[[nodiscard]] inline Expected<MaterialTypedByteAppendRange> TryBuildMaterialTypedByteAppendRange(
    const usize currentByteCount,
    const usize appendByteCount
){
    MaterialTypedByteAppendRange appendRange;
    if(appendByteCount == 0u)
        return appendRange;

    if(appendByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte count exceeds u32 limits"));
        return MakeUnexpected(Failure{});
    }

    const auto alignedByteBeginResult = AlignUpChecked(currentByteCount, s_MaterialTypedWordBytes);
    if(!alignedByteBeginResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte offset overflows alignment"));
        return MakeUnexpected(Failure{});
    }
    const usize alignedByteBegin = *alignedByteBeginResult;
    if(appendByteCount > Limit<usize>::s_Max - alignedByteBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count overflows"));
        return MakeUnexpected(Failure{});
    }

    const usize byteEnd = alignedByteBegin + appendByteCount;
    const auto alignedByteEndResult = AlignUpChecked(byteEnd, s_MaterialTypedWordBytes);
    if(!alignedByteEndResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte end overflows alignment"));
        return MakeUnexpected(Failure{});
    }
    const usize alignedByteEnd = *alignedByteEndResult;
    if(alignedByteBegin > static_cast<usize>(Limit<u32>::s_Max) || alignedByteEnd > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material typed byte count exceeds u32 limits"));
        return MakeUnexpected(Failure{});
    }

    appendRange.byteRange.byteOffset = static_cast<u32>(alignedByteBegin);
    appendRange.byteRange.byteCount = static_cast<u32>(appendByteCount);
    appendRange.alignedByteEnd = alignedByteEnd;
    return appendRange;
}

template<typename DestinationByteVector, typename SourceByteVector>
[[nodiscard]] inline Expected<MaterialTypedByteRange> AppendMaterialTypedByteRange(
    DestinationByteVector& materialTypedBytes,
    const SourceByteVector& typedBytes
){
    const auto appendRange = TryBuildMaterialTypedByteAppendRange(materialTypedBytes.size(), typedBytes.size());
    if(!appendRange)
        return MakeUnexpected(Failure{});
    if(typedBytes.empty())
        return appendRange->byteRange;

    const usize requiredTypedByteCapacity = appendRange->alignedByteEnd;
    if(requiredTypedByteCapacity > materialTypedBytes.capacity())
        materialTypedBytes.reserve(::NextGrowingCapacity(materialTypedBytes.capacity(), requiredTypedByteCapacity));
    materialTypedBytes.resize(appendRange->byteRange.byteOffset, 0u);
    AppendTriviallyCopyableVector(materialTypedBytes, typedBytes);
    materialTypedBytes.resize(appendRange->alignedByteEnd, 0u);
    return appendRange->byteRange;
}

template<typename DestinationByteVector, typename SourceByteVector, typename MaterialTypedByteRangeMap>
[[nodiscard]] inline Expected<MaterialTypedByteRange> FindOrAppendMaterialTypedByteRange(
    DestinationByteVector& materialTypedBytes,
    MaterialTypedByteRangeMap& rangeMap,
    const SourceByteVector& typedBytes
){
    if(typedBytes.empty())
        return MaterialTypedByteRange{};

    const MaterialTypedByteContentLookup lookup{
        ComputeFnv64Bytes(typedBytes.data(), typedBytes.size()), Span<const u8>(typedBytes.data(), typedBytes.size())
    };
    const auto foundRange = rangeMap.find(lookup);
    if(foundRange != rangeMap.end())
        return foundRange.value();

    // Copy before growing the upload vector: typedBytes may alias its existing storage.
    MaterialTypedByteContentKey rangeKey(materialTypedBytes.get_allocator().arena(), lookup);
    const Span<const u8> ownedBytes(rangeKey.bytes.data(), rangeKey.bytes.size());
    const auto range = AppendMaterialTypedByteRange(materialTypedBytes, ownedBytes);
    if(!range)
        return MakeUnexpected(Failure{});

    const auto insertedRange = rangeMap.emplace(Move(rangeKey), *range);
    if(!insertedRange.second){
        NWB_ASSERT_MSG(false, NWB_TEXT("RendererSystem: material typed range insertion duplicated a missing key"));
        return MakeUnexpected(Failure{});
    }
    return range;
}

[[nodiscard]] inline bool MaterialTypedByteRangeEmptyOffsetValid(const MaterialTypedByteRange& range)noexcept{
    return range.byteCount != 0u || range.byteOffset == 0u;
}

inline InstanceGpuData BuildInstanceGpuData(
    const Core::ECS::EntityID entity,
    const NWB::Impl::Scene::TransformComponent* transform,
    const MaterialTypedInstanceRanges& materialTypedRanges
)noexcept{
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
        data.scale = Float3UInt(transform->scale.x, transform->scale.y, transform->scale.z, entity.id);
    }
    else{
        data.translation.w = materialTypedRanges.mutableRange.byteOffset;
        data.scale.w = entity.id;
    }
    return data;
}

template<typename MaterialTypedByteVector>
[[nodiscard]] inline usize ResolveMaterialTypedUploadByteCount(
    const MaterialTypedByteVector& materialTypedBytes
){
    const usize uploadByteCount = materialTypedBytes.size();
    NWB_ASSERT_MSG(
        uploadByteCount != 0u,
        NWB_TEXT("RendererSystem: material typed data upload is empty")
    );
    NWB_ASSERT_MSG(
        (uploadByteCount & (s_MaterialTypedWordBytes - 1u)) == 0u,
        NWB_TEXT("RendererSystem: material typed data upload is not word-aligned")
    );

    return uploadByteCount;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void AssertMaterialTypedUploadRange(
    const MaterialTypedByteRange& range,
    const usize uploadByteCount,
    [[maybe_unused]] const TStringView rangeName
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

