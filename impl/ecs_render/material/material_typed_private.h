// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/material/pipeline_types.adhd.h>
#include <impl/ecs_scene/components.adhd.h>

#include <core/common/log.h>

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


using MaterialTypedByteRangeKeyHasher = Hasher<MaterialTypedByteRangeKey>;
using MaterialTypedConstantRangeMap = HashMap<MaterialTypedByteRangeKey, MaterialTypedByteRange, Core::Alloc::ScratchArena, MaterialTypedByteRangeKeyHasher, EqualTo<MaterialTypedByteRangeKey>>;

struct MaterialTypedByteContentKey{
    u64 byteHash = 0u;
    Vector<u8, Core::Alloc::ScratchArena> bytes;


public:
    MaterialTypedByteContentKey(Core::Alloc::ScratchArena& arena, const MaterialTypedByteContentLookup& lookup);
};

[[nodiscard]] b8 operator==(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs)noexcept;

struct MaterialTypedByteContentKeyHasher{
    [[nodiscard]] usize operator()(const MaterialTypedByteContentKey& key)const noexcept;
    [[nodiscard]] usize operator()(const MaterialTypedByteContentLookup& lookup)const noexcept;
};

struct MaterialTypedByteContentKeyEqual{
    using is_transparent = void;


public:
    [[nodiscard]] b8 operator()(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs)const noexcept{
        return lhs == rhs;
    }

    [[nodiscard]] b8 operator()(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentLookup& rhs)const noexcept;
};

// Dedup map for mutable typed ranges; identical blocks share one range.
// Used by the material draw pass and the shadow occluder packing alike.
using MaterialTypedByteContentRangeMap = HashMap<MaterialTypedByteContentKey, MaterialTypedByteRange, Core::Alloc::ScratchArena, MaterialTypedByteContentKeyHasher, MaterialTypedByteContentKeyEqual>;

[[nodiscard]] Expected<MaterialTypedByteAppendRange> TryBuildMaterialTypedByteAppendRange(usize currentByteCount, usize appendByteCount);

template<typename DestinationByteVector>
[[nodiscard]] inline Expected<MaterialTypedByteRange> AppendMaterialTypedByteRange(const Span<const u8> typedBytes, DestinationByteVector& materialTypedBytes){
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

template<typename DestinationByteVector>
[[nodiscard]] inline Expected<MaterialTypedByteRange> FindOrAppendMaterialConstantTypedByteRange(
    const Name& materialName,
    const u64 typedLayoutHash,
    const Span<const u8> typedBytes,
    DestinationByteVector& materialTypedBytes,
    MaterialTypedConstantRangeMap& rangeMap
){
    MaterialTypedByteRangeKey key;
    key.materialName = materialName;
    key.typedLayoutHash = typedLayoutHash;
    const auto foundRange = rangeMap.find(key);
    if(foundRange != rangeMap.end())
        return foundRange.value();

    const auto range = AppendMaterialTypedByteRange(typedBytes, materialTypedBytes);
    if(!range)
        return MakeUnexpected(Failure{});
    if(!rangeMap.emplace(key, *range).second){
        NWB_ASSERT(false);
        return MakeUnexpected(Failure{});
    }
    return range;
}

template<typename DestinationByteVector, typename MaterialTypedByteRangeMap>
[[nodiscard]] inline Expected<MaterialTypedByteRange> FindOrAppendMaterialTypedByteRange(const Span<const u8> typedBytes, DestinationByteVector& materialTypedBytes, MaterialTypedByteRangeMap& rangeMap){
    if(typedBytes.empty())
        return MaterialTypedByteRange{};

    const MaterialTypedByteContentLookup lookup{ ComputeFnv64Bytes(typedBytes.data(), typedBytes.size()), typedBytes };
    const auto foundRange = rangeMap.find(lookup);
    if(foundRange != rangeMap.end())
        return foundRange.value();

    // Copy before growing the upload vector: typedBytes may alias its existing storage.
    MaterialTypedByteContentKey rangeKey(materialTypedBytes.get_allocator().arena(), lookup);
    const Span<const u8> ownedBytes(rangeKey.bytes.data(), rangeKey.bytes.size());
    const auto range = AppendMaterialTypedByteRange(ownedBytes, materialTypedBytes);
    if(!range)
        return MakeUnexpected(Failure{});

    const auto insertedRange = rangeMap.emplace(Move(rangeKey), *range);
    if(!insertedRange.second){
        NWB_ASSERT_MSG(false, NWB_TEXT("RendererSystem: material typed range insertion duplicated a missing key"));
        return MakeUnexpected(Failure{});
    }
    return range;
}

[[nodiscard]] usize ResolveMaterialTypedUploadByteCount(Span<const u8> materialTypedBytes)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AssertMaterialTypedUploadRange(const MaterialTypedByteRange& range, usize uploadByteCount, TStringView rangeName)noexcept;
void AssertMaterialTypedInstanceRange(const MaterialTypedInstanceRanges& ranges, usize uploadByteCount)noexcept;

using MaterialTypedInstanceRangeVector = Vector<MaterialTypedInstanceRanges, Core::Alloc::ScratchArena>;

void AssertMaterialTypedUploadRanges(Span<const MaterialTypedInstanceRanges> instanceRanges, Span<const u8> materialTypedBytes)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

