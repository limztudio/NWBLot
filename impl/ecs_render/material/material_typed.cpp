// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_material_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MaterialTypedByteContentKey::MaterialTypedByteContentKey(Core::Alloc::ScratchArena& arena, const MaterialTypedByteContentLookup& lookup)
    : byteHash(lookup.byteHash)
    , bytes(arena)
{
    if(!lookup.bytes.empty()){
        bytes.reserve(lookup.bytes.size());
        bytes.assign(lookup.bytes.begin(), lookup.bytes.end());
    }
}

b8 operator==(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentKey& rhs)noexcept{
    if(lhs.byteHash != rhs.byteHash || lhs.bytes.size() != rhs.bytes.size())
        return false;
    if(lhs.bytes.empty())
        return true;

    return NWB_MEMCMP(lhs.bytes.data(), rhs.bytes.data(), lhs.bytes.size()) == 0;
}

usize MaterialTypedByteContentKeyHasher::operator()(const MaterialTypedByteContentKey& key)const noexcept{
    usize seed = Hasher<u64>{}(key.byteHash);
    ::HashCombine(seed, key.bytes.size());
    return seed;
}

usize MaterialTypedByteContentKeyHasher::operator()(const MaterialTypedByteContentLookup& lookup)const noexcept{
    usize seed = Hasher<u64>{}(lookup.byteHash);
    ::HashCombine(seed, lookup.bytes.size());
    return seed;
}

b8 MaterialTypedByteContentKeyEqual::operator()(const MaterialTypedByteContentKey& lhs, const MaterialTypedByteContentLookup& rhs)const noexcept{
    if(lhs.byteHash != rhs.byteHash || lhs.bytes.size() != rhs.bytes.size())
        return false;
    if(lhs.bytes.empty())
        return true;

    return NWB_MEMCMP(lhs.bytes.data(), rhs.bytes.data(), lhs.bytes.size()) == 0;
}

Expected<MaterialTypedByteAppendRange> TryBuildMaterialTypedByteAppendRange(const usize currentByteCount, const usize appendByteCount){
    MaterialTypedByteAppendRange appendRange;
    if(appendByteCount == 0u)
        return appendRange;

    if(appendByteCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material typed byte count exceeds u32 limits"));
        return MakeUnexpected(Failure{});
    }

    const auto alignedByteBeginResult = AlignUpChecked(currentByteCount, static_cast<usize>(s_MaterialTypedWordBytes));
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
    const auto alignedByteEndResult = AlignUpChecked(byteEnd, static_cast<usize>(s_MaterialTypedWordBytes));
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

usize ResolveMaterialTypedUploadByteCount(const Span<const u8> materialTypedBytes)noexcept{
    const usize uploadByteCount = materialTypedBytes.size();
    NWB_ASSERT_MSG(uploadByteCount != 0u, NWB_TEXT("RendererSystem: material typed data upload is empty"));
    NWB_ASSERT_MSG((uploadByteCount & (s_MaterialTypedWordBytes - 1u)) == 0u, NWB_TEXT("RendererSystem: material typed data upload is not word-aligned"));

    return uploadByteCount;
}


#if defined(NWB_DEBUG)


void AssertMaterialTypedUploadRange(const MaterialTypedByteRange& range, const usize uploadByteCount, [[maybe_unused]] const TStringView rangeName)noexcept{
    if(range.byteCount == 0u){
        NWB_ASSERT_MSG(MaterialTypedByteRangeEmptyOffsetValid(range), NWB_TEXT("RendererSystem: {} material typed byte range has zero count with nonzero offset"), rangeName);
        return;
    }

    NWB_ASSERT_MSG(((range.byteOffset | range.byteCount) & (s_MaterialTypedWordBytes - 1u)) == 0u, NWB_TEXT("RendererSystem: {} material typed byte range is not word-aligned"), rangeName);

    const usize byteOffset = static_cast<usize>(range.byteOffset);
    NWB_ASSERT_MSG(byteOffset <= uploadByteCount, NWB_TEXT("RendererSystem: {} material typed byte range offset exceeds upload data"), rangeName);
    NWB_ASSERT_MSG(byteOffset <= uploadByteCount && static_cast<usize>(range.byteCount) <= uploadByteCount - byteOffset, NWB_TEXT("RendererSystem: {} material typed byte range exceeds upload data"), rangeName);
}

void AssertMaterialTypedInstanceRange(const MaterialTypedInstanceRanges& ranges, const usize uploadByteCount)noexcept{
    AssertMaterialTypedUploadRange(ranges.constantRange, uploadByteCount, NWB_TEXT("constant"));
    AssertMaterialTypedUploadRange(ranges.mutableRange, uploadByteCount, NWB_TEXT("mutable"));
}

void AssertMaterialTypedUploadRanges(const Span<const MaterialTypedInstanceRanges> instanceRanges, const Span<const u8> materialTypedBytes)noexcept{
    if(instanceRanges.empty())
        return;

    const usize uploadByteCount = materialTypedBytes.size();
    NWB_ASSERT_MSG(uploadByteCount != 0u, NWB_TEXT("RendererSystem: material typed data upload is empty"));
    NWB_ASSERT_MSG((uploadByteCount & (s_MaterialTypedWordBytes - 1u)) == 0u, NWB_TEXT("RendererSystem: material typed data upload is not word-aligned"));

    for(const MaterialTypedInstanceRanges& ranges : instanceRanges)
        AssertMaterialTypedInstanceRange(ranges, uploadByteCount);
}


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialState::invalidateResources()noexcept{
    m_pipelines.clear();
    m_materialPassBindingLayout.reset();
    m_computeBindingLayout.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_emulationVertexShader.reset();
    m_emulationInputLayout.reset();
    m_objectGeometryDecodeShader.reset();
    m_objectGeometryDecodePipeline.reset();
    m_objectGeometryInputLayout.reset();
    m_instanceMutableCache.clear();
    m_loggedMaterialPaths.clear();
    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
    m_instanceMutableCacheComponentMutationVersion = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MaterialPassMeshResourceSnapshot::geometryHeapHandlesReady()const noexcept{
    constexpr Array<u32, 11> s_SourceBindingSlots = {
        NWB_MESH_BINDING_POSITION,
        NWB_MESH_BINDING_NORMAL,
        NWB_MESH_BINDING_TANGENT,
        NWB_MESH_BINDING_UV0,
        NWB_MESH_BINDING_COLOR,
        NWB_MESH_BINDING_MESHLET_DESC,
        NWB_MESH_BINDING_MESHLET_BOUNDS,
        NWB_MESH_BINDING_MESHLET_POSITION_REFS,
        NWB_MESH_BINDING_MESHLET_ATTRIBUTE_REFS,
        NWB_MESH_BINDING_MESHLET_LOCAL_VERTEX_REFS,
        NWB_MESH_BINDING_MESHLET_PRIMITIVE_INDICES,
    };
    for(const u32 bindingSlot : s_SourceBindingSlots){
        const Core::GpuDescriptorHandle handle = geometryHeapHandles[bindingSlot];
        if(!handle.valid() || handle.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialPassDrawItems::reserve(const usize capacity){
    meshDrawItems.reserve(capacity);
    indexedDrawItems.reserve(capacity);
    computeDrawItems.reserve(capacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void MaterialPassDrawItemPartitions::reserve(const usize capacity){
    regular.reserve(capacity);
    csg.reserve(capacity);
    csgReceiverSurface.reserve(capacity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

