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


[[nodiscard]] b8 ValidateRuntimeMeshUploadPayload(const MeshSkinningRuntimeInstance& instance){
    if(!ValidateRuntimeMeshUploadPayloadStreams(
        instance.sourceName,
        instance.hasCompleteSplitMeshPayload(),
        instance.restPositions.size(),
        instance.restNormals.size(),
        instance.restTangents.size(),
        instance.uv0.size(),
        instance.colors.size(),
        instance.skin.size(),
        instance.meshlets,
        instance.meshletPositionRefDeltas,
        instance.meshletAttributeRefDeltas,
        instance.meshletLocalVertexRefs.size(),
        instance.meshletPrimitiveIndices.size(),
        instance.attributeSkins
    ))
        return false;
    // Skin::loadBinary already admitted the copied skin and inverse bind data.
    NWB_ASSERT(instance.skeletonJointCount > 0u && instance.skeletonJointCount <= static_cast<u32>(Limit<u16>::s_Max) + 1u);
    NWB_ASSERT(ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount));
#if defined(NWB_DEBUG)
    for(const SkeletonJointMatrix& storedInverseBind : instance.inverseBindMatrices)
        NWB_ASSERT(MatrixIsInvertibleAffine(LoadFloat(storedInverseBind), s_SkinValidationEpsilon, s_SkinValidationEpsilon));
    for(const SkinInfluence4& skin : instance.skin)
        NWB_ASSERT(ValidSkinInfluenceWeights(LoadFloat(skin.weight)) && SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount));
#endif
    return true;
}

[[nodiscard]] Name DeriveNamedRuntimeBuffer(const MeshSkinningRuntimeInstance& instance, const AStringView suffix, const AStringView label){
    return DeriveNamedRuntimeBufferName(instance.sourceName, instance.handle.value, instance.editRevision, suffix, label);
}

[[nodiscard]] b8 FinalizeRuntimeBufferUpload(
    Expected<Core::BufferHandle, BufferSetupFailure::Enum>&& buffer,
    const MeshSkinningRuntimeInstance& instance,
    const AStringView label,
    Core::BufferHandle& outBuffer
){
    if(buffer){
        outBuffer = Move(*buffer);
        return true;
    }
    outBuffer.reset();
    ReportRuntimeBufferUploadFailure(buffer.error(), label, instance.handle.value);
    return false;
}

template<typename PayloadT>
[[nodiscard]] b8 AssignRuntimeBuffer(
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const Span<const PayloadT> payload,
    const b8 canHaveUavs,
    const AStringView label,
    Core::GraphicsRuntime& graphics,
    const b8 canHaveRawViews,
    const b8 accelStructBuildInput,
    const Core::ResourceQueueSharing::Mask queueSharing,
    const b8 isIndexBuffer,
    Core::BufferHandle& outBuffer
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
    const AStringView label,
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
        s_RuntimeCacheRestPositionLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.restPositionBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_RestNormalsSuffix,
        instance.restNormals,
        false,
        s_RuntimeCacheRestNormalLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.restNormalBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_RestTangentsSuffix,
        instance.restTangents,
        false,
        s_RuntimeCacheRestTangentLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.restTangentBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        instance,
        s_SkinnedPositionsSuffix,
        instance.restPositions,
        true,
        s_RuntimeCacheSkinnedPositionLabel,
        m_graphics,
        true,
        rtSupported,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.skinnedPositionBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_SkinnedNormalsSuffix,
        instance.restNormals,
        true,
        s_RuntimeCacheSkinnedNormalLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.skinnedNormalBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_SkinnedTangentsSuffix,
        instance.restTangents,
        true,
        s_RuntimeCacheSkinnedTangentLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.skinnedTangentBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float2U>(
        instance,
        s_Uv0Suffix,
        instance.uv0,
        false,
        s_RuntimeCacheUv0Label,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.uv0Buffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        instance,
        s_ColorsSuffix,
        instance.colors,
        false,
        s_RuntimeCacheColorLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.colorBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletDesc>(
        instance,
        s_MeshletsSuffix,
        instance.meshlets,
        false,
        s_RuntimeCacheMeshletDescriptorLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.meshletDescBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletBounds>(
        instance,
        s_MeshletBoundsSuffix,
        instance.meshletBounds,
        true,
        s_RuntimeCacheMeshletBoundsLabel,
        m_graphics,
        true,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.meshletBoundsBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletPositionRefDeltasSuffix,
        instance.meshletPositionRefDeltas,
        false,
        s_RuntimeCacheMeshletPositionRefDeltaLabel,
        m_graphics,
        scratchArena,
        instance.meshletPositionRefDeltaBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletAttributeRefDeltasSuffix,
        instance.meshletAttributeRefDeltas,
        false,
        s_RuntimeCacheMeshletAttributeRefDeltaLabel,
        m_graphics,
        scratchArena,
        instance.meshletAttributeRefDeltaBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletLocalVertexRef>(
        instance,
        s_MeshletLocalVertexRefsSuffix,
        instance.meshletLocalVertexRefs,
        false,
        s_RuntimeCacheMeshletLocalVertexRefLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
        instance.meshletLocalVertexRefBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        instance,
        s_MeshletPrimitiveIndicesSuffix,
        instance.meshletPrimitiveIndices,
        false,
        s_RuntimeCacheMeshletPrimitiveIndexLabel,
        m_graphics,
        scratchArena,
        instance.meshletPrimitiveIndexBuffer
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
        instance,
        s_AttributeSkinsSuffix,
        instance.attributeSkins,
        false,
        s_RuntimeCacheAttributeSkinLabel,
        m_graphics,
        false,
        false,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
        false,
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
        if(!ValidateReconstructedRuntimeTriangleIndexCount(triangleIndices->size(), indexCount, instance.handle.value))
            return false;

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
            instance,
            s_RtTriangleIndicesBufferName,
            *triangleIndices,
            false,
            s_RuntimeCacheRtTriangleIndexLabel,
            m_graphics,
            true,
            rtSupported,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            true,
            instance.triangleIndexBuffer
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
            s_RuntimeCacheRtTriangleAttributeLabel,
            m_graphics,
            true,
            false,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            false,
            instance.attributeBuffer
        ) && uploaded;
    }

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

