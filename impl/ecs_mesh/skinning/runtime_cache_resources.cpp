// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"
#include "local_bounds.h"

#include <impl/ecs_mesh/runtime/types.adhd.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_RestPositionLabel = NWB_TEXT("rest position");
static constexpr TStringView s_RestNormalLabel = NWB_TEXT("rest normal");
static constexpr TStringView s_RestTangentLabel = NWB_TEXT("rest tangent");
static constexpr TStringView s_SkinnedPositionLabel = NWB_TEXT("skinned position");
static constexpr TStringView s_SkinnedNormalLabel = NWB_TEXT("skinned normal");
static constexpr TStringView s_SkinnedTangentLabel = NWB_TEXT("skinned tangent");
static constexpr TStringView s_Uv0Label = NWB_TEXT("uv0");
static constexpr TStringView s_ColorLabel = NWB_TEXT("color");
static constexpr TStringView s_MeshletDescriptorLabel = NWB_TEXT("meshlet descriptor");
static constexpr TStringView s_MeshletBoundsLabel = NWB_TEXT("meshlet bounds");
static constexpr TStringView s_MeshletPositionRefDeltaLabel = NWB_TEXT("meshlet position ref delta");
static constexpr TStringView s_MeshletAttributeRefDeltaLabel = NWB_TEXT("meshlet attribute ref delta");
static constexpr TStringView s_MeshletLocalVertexRefLabel = NWB_TEXT("meshlet local vertex ref");
static constexpr TStringView s_MeshletPrimitiveIndexLabel = NWB_TEXT("meshlet primitive index");
static constexpr TStringView s_AttributeSkinLabel = NWB_TEXT("attribute skin");
static constexpr TStringView s_RtTriangleIndexLabel = NWB_TEXT("rt triangle index");
static constexpr TStringView s_RtTriangleAttributeLabel = NWB_TEXT("rt triangle attribute");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve the name only when a diagnostic is emitted.
[[nodiscard]] AStringView RuntimeMeshSourceText(const MeshSkinningRuntimeInstance& instance){
    return instance.sourceName ? instance.sourceName.resolvedText() : AStringView("<unnamed>");
}

[[nodiscard]] b8 ValidateRuntimeMeshUploadPayload(const MeshSkinningRuntimeInstance& instance){
    if(!Core::Mesh::MeshClassUsesSkinning(instance.meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has invalid mesh class"), ConvertString(RuntimeMeshSourceText(instance)));
        return false;
    }
    if(!instance.hasCompleteSplitMeshPayload()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has incomplete split mesh payload"), ConvertString(RuntimeMeshSourceText(instance)));
        return false;
    }
    if(
        !FitsU32(instance.restPositions.size())
        || !FitsU32(instance.uv0.size())
        || !FitsU32(instance.colors.size())
        || !FitsU32(instance.skin.size())
        || !FitsU32(instance.meshlets.size())
        || !FitsU32(instance.meshletPositionRefDeltas.size())
        || !FitsU32(instance.meshletAttributeRefDeltas.size())
        || !FitsU32(instance.meshletLocalVertexRefs.size())
        || !FitsU32(instance.meshletPrimitiveIndices.size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' exceeds u32 payload limits"), ConvertString(RuntimeMeshSourceText(instance)));
        return false;
    }
    // Skin::loadBinary already admitted the copied skin and inverse bind data.
    NWB_ASSERT(instance.skeletonJointCount > 0u && instance.skeletonJointCount <= static_cast<u32>(Limit<u16>::s_Max) + 1u);
    NWB_ASSERT(ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount));
#if defined(NWB_DEBUG)
    for(const SkeletonJointMatrix& storedInverseBind : instance.inverseBindMatrices)
        NWB_ASSERT(MatrixIsInvertibleAffine(LoadFloat(storedInverseBind), s_SkinValidationEpsilon, s_SkinValidationEpsilon));
    for(const SkinInfluence4& skin : instance.skin)
        NWB_ASSERT(ValidSkinInfluenceWeights(skin.weight) && SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount));
#endif

    usize logicalAttributeRefIndex = 0u;
    for(usize meshletIndex = 0u; meshletIndex < instance.meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = instance.meshlets[meshletIndex];
        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            MeshletPositionStreamRef ref;
            if(TryDecodeMeshletPositionRef(instance.meshletPositionRefDeltas, meshlet, localPositionIndex, true, ref) && MeshletPositionRefInRange(ref, instance.restPositions.size(), instance.skin.size(), true))
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} position ref {} is out of range"), ConvertString(RuntimeMeshSourceText(instance)), meshletIndex, localPositionIndex);
            return false;
        }
        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            MeshletAttributeStreamRef ref;
            if(
                TryDecodeMeshletAttributeRef(instance.meshletAttributeRefDeltas, meshlet, localAttributeIndex, ref)
                && MeshletAttributeRefInRange(ref, instance.restNormals.size(), instance.restTangents.size(), instance.uv0.size(), instance.colors.size())
                && logicalAttributeRefIndex < instance.attributeSkins.size()
                && instance.attributeSkins[logicalAttributeRefIndex] < instance.skin.size()
            ){
                ++logicalAttributeRefIndex;
                continue;
            }

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} attribute ref {} is out of range"), ConvertString(RuntimeMeshSourceText(instance)), meshletIndex, localAttributeIndex);
            return false;
        }
    }
    if(logicalAttributeRefIndex != instance.attributeSkins.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has mismatched attribute skin payload"), ConvertString(RuntimeMeshSourceText(instance)));
        return false;
    }

    return true;
}

[[nodiscard]] Name DeriveNamedRuntimeBuffer(const MeshSkinningRuntimeInstance& instance, const AStringView suffix, const TStringView label){
    const Name bufferName = DeriveRuntimeResourceName(instance.sourceName, instance.entity.id, instance.editRevision, suffix);
    if(!bufferName)
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'"), label, instance.handle.value);
    return bufferName;
}

[[nodiscard]] b8 FinalizeRuntimeBufferUpload(
    Expected<Core::BufferHandle, BufferSetupFailure::Enum>&& buffer,
    const MeshSkinningRuntimeInstance& instance,
    const TStringView label,
    Core::BufferHandle& outBuffer
){
    if(buffer){
        outBuffer = Move(*buffer);
        return true;
    }
    outBuffer.reset();
    switch(buffer.error()){
    case BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return false;
    case BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return false;
    case BufferSetupFailure::CreateFailed:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'"), label, instance.handle.value);
        return false;
    }

    NWB_ASSERT(false);
    return false;
}

template<typename PayloadT>
[[nodiscard]] b8 AssignRuntimeBuffer(
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const Span<const PayloadT> payload,
    const b8 canHaveUavs,
    const TStringView label,
    Core::GraphicsRuntime& graphics,
    Core::BufferHandle& outBuffer,
    const b8 canHaveRawViews = false,
    const b8 accelStructBuildInput = false,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
    const b8 isIndexBuffer = false
){
    const Name bufferName = DeriveNamedRuntimeBuffer(instance, suffix, label);
    if(!bufferName){
        outBuffer.reset();
        return false;
    }

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(graphics, bufferName, payload, { canHaveUavs, canHaveRawViews, accelStructBuildInput, queueSharing, isIndexBuffer });
    return FinalizeRuntimeBufferUpload(Move(buffer), instance, label, outBuffer);
}

[[nodiscard]] b8 AssignPaddedRawRuntimeBuffer(
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const Span<const u8> payload,
    const b8 canHaveUavs,
    const TStringView label,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    Core::BufferHandle& outBuffer
){
    const Name bufferName = DeriveNamedRuntimeBuffer(instance, suffix, label);
    if(!bufferName){
        outBuffer = nullptr;
        return false;
    }

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredPaddedRawByteBuffer(graphics, scratchArena, bufferName, payload, { canHaveUavs, true, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute });
    return FinalizeRuntimeBufferUpload(Move(buffer), instance, label, outBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MeshSkinningRuntimeCache::uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
    if(!__hidden_runtime_cache_resources::ValidateRuntimeMeshUploadPayload(instance))
        return false;

    const b8 rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Software and hybrid tails read raw position/index buffers; keep views even with HWRT.

    b8 uploaded = CreateMeshSkinningLocalBoundsBuffers(m_graphics, instance);
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        instance,
        s_RestPositionsSuffix,
        instance.restPositions,
        false,
        __hidden_runtime_cache_resources::s_RestPositionLabel,
        m_graphics,
        instance.restPositionBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_RestNormalsSuffix,
        instance.restNormals,
        false,
        __hidden_runtime_cache_resources::s_RestNormalLabel,
        m_graphics,
        instance.restNormalBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_RestTangentsSuffix,
        instance.restTangents,
        false,
        __hidden_runtime_cache_resources::s_RestTangentLabel,
        m_graphics,
        instance.restTangentBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        instance,
        s_SkinnedPositionsSuffix,
        instance.restPositions,
        true,
        __hidden_runtime_cache_resources::s_SkinnedPositionLabel,
        m_graphics,
        instance.skinnedPositionBuffer,
        true,
        rtSupported
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_SkinnedNormalsSuffix,
        instance.restNormals,
        true,
        __hidden_runtime_cache_resources::s_SkinnedNormalLabel,
        m_graphics,
        instance.skinnedNormalBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_SkinnedTangentsSuffix,
        instance.restTangents,
        true,
        __hidden_runtime_cache_resources::s_SkinnedTangentLabel,
        m_graphics,
        instance.skinnedTangentBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float2U>(instance, s_Uv0Suffix, instance.uv0, false, __hidden_runtime_cache_resources::s_Uv0Label, m_graphics, instance.uv0Buffer) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(instance, s_ColorsSuffix, instance.colors, false, __hidden_runtime_cache_resources::s_ColorLabel, m_graphics, instance.colorBuffer) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletDesc>(
        instance,
        s_MeshletsSuffix,
        instance.meshlets,
        false,
        __hidden_runtime_cache_resources::s_MeshletDescriptorLabel,
        m_graphics,
        instance.meshletDescBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletBounds>(
        instance,
        s_MeshletBoundsSuffix,
        instance.meshletBounds,
        true,
        __hidden_runtime_cache_resources::s_MeshletBoundsLabel,
        m_graphics,
        instance.meshletBoundsBuffer,
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletPositionRefDeltasSuffix,
        instance.meshletPositionRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletPositionRefDeltaLabel,
        m_graphics,
        scratchArena,
        instance.meshletPositionRefDeltaBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletAttributeRefDeltasSuffix,
        instance.meshletAttributeRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletAttributeRefDeltaLabel,
        m_graphics,
        scratchArena,
        instance.meshletAttributeRefDeltaBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletLocalVertexRef>(
        instance,
        s_MeshletLocalVertexRefsSuffix,
        instance.meshletLocalVertexRefs,
        false,
        __hidden_runtime_cache_resources::s_MeshletLocalVertexRefLabel,
        m_graphics,
        instance.meshletLocalVertexRefBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletPrimitiveIndicesSuffix,
        instance.meshletPrimitiveIndices,
        false,
        __hidden_runtime_cache_resources::s_MeshletPrimitiveIndexLabel,
        m_graphics,
        scratchArena,
        instance.meshletPrimitiveIndexBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
        instance,
        s_AttributeSkinsSuffix,
        instance.attributeSkins,
        false,
        __hidden_runtime_cache_resources::s_AttributeSkinLabel,
        m_graphics,
        instance.attributeSkinBuffer
    ) && uploaded;

    // Both shadow backends trace triangles; always build the reconstructed index buffer.
    if(uploaded){
        const usize indexCount = instance.meshletPrimitiveIndices.size();
        const auto triangleIndices = BuildMeshletTriangleIndices(instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletPositionRefDeltas, instance.meshletPrimitiveIndices, instance.restPositions.size(), scratchArena);
        if(!triangleIndices){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct ray tracing triangle indices for runtime mesh '{}'"), instance.handle.value);
            return false;
        }
        if(triangleIndices->size() != indexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: reconstructed ray tracing index count {} does not match expected {} for runtime mesh '{}'"), static_cast<u64>(triangleIndices->size()), static_cast<u64>(indexCount), instance.handle.value);
            return false;
        }

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
            instance,
            s_RtTriangleIndicesBufferName,
            *triangleIndices,
            false,
            __hidden_runtime_cache_resources::s_RtTriangleIndexLabel,
            m_graphics,
            instance.triangleIndexBuffer,
            true,
            rtSupported,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            true
        ) && uploaded;
    }

    // Per-corner trace attributes seeded from bind-pose; repack overwrites normals per frame.
    if(uploaded){
        const auto triangleAttributes = BuildMeshletTriangleAttributes(instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletAttributeRefDeltas, instance.meshletPrimitiveIndices, instance.restNormals, instance.uv0, scratchArena);
        if(!triangleAttributes){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct shadow trace triangle attributes for runtime mesh '{}'"), instance.handle.value);
            return false;
        }

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<AttribGpu>(
            instance,
            s_RtTriangleAttributesBufferName,
            *triangleAttributes,
            true, // canHaveUavs: the per-frame skinned-normal repack pass writes this buffer as a raw UAV in place
            __hidden_runtime_cache_resources::s_RtTriangleAttributeLabel,
            m_graphics,
            instance.attributeBuffer,
            true
        ) && uploaded;
    }

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

