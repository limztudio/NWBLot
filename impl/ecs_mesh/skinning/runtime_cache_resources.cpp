// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"
#include "local_bounds.h"
#include "resource_names.h"

#include <impl/ecs_mesh/runtime/resource_names.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/assets_mesh/skin_validation.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>
#include <impl/assets_mesh/meshlet_ref_validation.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_RestPositionsSuffix = "rest_positions";
static constexpr AStringView s_RestNormalsSuffix = "rest_normals";
static constexpr AStringView s_RestTangentsSuffix = "rest_tangents";
static constexpr AStringView s_SkinnedPositionsSuffix = "skinned_positions";
static constexpr AStringView s_SkinnedNormalsSuffix = "skinned_normals";
static constexpr AStringView s_SkinnedTangentsSuffix = "skinned_tangents";
static constexpr AStringView s_Uv0Suffix = "uv0";
static constexpr AStringView s_ColorsSuffix = "colors";
static constexpr AStringView s_MeshletsSuffix = "meshlets";
static constexpr AStringView s_MeshletBoundsSuffix = "meshlet_bounds";
static constexpr AStringView s_MeshletPositionRefDeltasSuffix = "meshlet_position_ref_deltas";
static constexpr AStringView s_MeshletAttributeRefDeltasSuffix = "meshlet_attribute_ref_deltas";
static constexpr AStringView s_MeshletLocalVertexRefsSuffix = "meshlet_local_vertex_refs";
static constexpr AStringView s_MeshletPrimitiveIndicesSuffix = "meshlet_primitive_indices";
static constexpr AStringView s_AttributeSkinsSuffix = "attribute_skins";
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

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(const MeshSkinningRuntimeInstance& instance){
    if(!Core::Mesh::MeshClassUsesSkinning(instance.meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has invalid mesh class"), StringConvert(RuntimeMeshSourceText(instance)));
        return false;
    }
    if(
        instance.restPositions.empty()
        || instance.restNormals.empty()
        || instance.restTangents.size() != instance.restNormals.size()
        || instance.uv0.empty()
        || instance.colors.empty()
        || instance.skin.empty()
        || instance.meshlets.empty()
        || instance.meshletBounds.size() != instance.meshlets.size()
        || instance.meshletPositionRefDeltas.empty()
        || instance.meshletAttributeRefDeltas.empty()
        || instance.meshletLocalVertexRefs.empty()
        || instance.meshletPrimitiveIndices.empty()
        || instance.meshletPositionRefCount == 0u
        || instance.meshletAttributeRefCount == 0u
        || instance.attributeSkins.size() != instance.meshletAttributeRefCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has incomplete split mesh payload"), StringConvert(RuntimeMeshSourceText(instance)));
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
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' exceeds u32 payload limits"), StringConvert(RuntimeMeshSourceText(instance)));
        return false;
    }
    // Skin::loadBinary already admitted the copied skin and inverse bind data.
    NWB_ASSERT(instance.skeletonJointCount > 0u && instance.skeletonJointCount <= static_cast<u32>(Limit<u16>::s_Max) + 1u);
    NWB_ASSERT(SkinValidation::ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount));
#if defined(NWB_DEBUG)
    for(const SkeletonJointMatrix& storedInverseBind : instance.inverseBindMatrices)
        NWB_ASSERT(MatrixIsInvertibleAffine(LoadFloat(storedInverseBind), SkinValidation::s_Epsilon, SkinValidation::s_Epsilon));
    for(const SkinInfluence4& skin : instance.skin)
        NWB_ASSERT(SkinValidation::ValidSkinInfluenceWeights(LoadFloat(skin.weight)) && SkinValidation::SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount));
#endif

    usize logicalAttributeRefIndex = 0u;
    for(usize meshletIndex = 0u; meshletIndex < instance.meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = instance.meshlets[meshletIndex];
        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            const auto ref = DecodeMeshletPositionRef(instance.meshletPositionRefDeltas.data(), instance.meshletPositionRefDeltas.size(), meshlet, localPositionIndex, true);
            if(ref && MeshMeshletRefValidation::MeshletPositionRefInRange(*ref, instance.restPositions.size(), instance.skin.size(), true))
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} position ref {} is out of range"), StringConvert(RuntimeMeshSourceText(instance)), meshletIndex, localPositionIndex);
            return false;
        }
        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            const auto ref = DecodeMeshletAttributeRef(instance.meshletAttributeRefDeltas.data(), instance.meshletAttributeRefDeltas.size(), meshlet, localAttributeIndex);
            if(
                ref
                && MeshMeshletRefValidation::MeshletAttributeRefInRange(*ref, instance.restNormals.size(), instance.restTangents.size(), instance.uv0.size(), instance.colors.size())
                && logicalAttributeRefIndex < instance.attributeSkins.size()
                && instance.attributeSkins[logicalAttributeRefIndex] < instance.skin.size()
            ){
                ++logicalAttributeRefIndex;
                continue;
            }

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} attribute ref {} is out of range"), StringConvert(RuntimeMeshSourceText(instance)), meshletIndex, localAttributeIndex);
            return false;
        }
    }
    if(logicalAttributeRefIndex != instance.attributeSkins.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has mismatched attribute skin payload"), StringConvert(RuntimeMeshSourceText(instance)));
        return false;
    }

    return true;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] Expected<Core::BufferHandle, RuntimeMeshBufferUpload::BufferSetupFailure::Enum> SetupRuntimeBuffer(
    Core::GraphicsRuntime& graphics,
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const TStringView label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
    const bool isIndexBuffer = false
){
    const auto bufferName = DeriveRuntimeResourceName(instance.sourceName, instance.entity.id, instance.editRevision, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'"), label, instance.handle.value);
        return MakeUnexpected(RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed);
    }

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(graphics, *bufferName, payload, { canHaveUavs, canHaveRawViews, accelStructBuildInput, queueSharing, isIndexBuffer });
    if(buffer)
        return Move(*buffer);
    switch(buffer.error()){
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return MakeUnexpected(buffer.error());
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return MakeUnexpected(buffer.error());
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'"), label, instance.handle.value);
    return MakeUnexpected(buffer.error());
}

template<typename PayloadVector>
[[nodiscard]] Expected<Core::BufferHandle, RuntimeMeshBufferUpload::BufferSetupFailure::Enum> SetupPaddedRawRuntimeBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const TStringView label,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute
){
    const auto bufferName = DeriveRuntimeResourceName(instance.sourceName, instance.entity.id, instance.editRevision, suffix);
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'"), label, instance.handle.value);
        return MakeUnexpected(RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed);
    }

    auto buffer = RuntimeMeshBufferUpload::SetupRequiredPaddedRawByteBuffer(graphics, scratchArena, *bufferName, payload, { canHaveUavs, true, false, queueSharing });
    if(buffer)
        return Move(*buffer);
    switch(buffer.error()){
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return MakeUnexpected(buffer.error());
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return MakeUnexpected(buffer.error());
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'"), label, instance.handle.value);
        return MakeUnexpected(buffer.error());
    }

    NWB_ASSERT(false);
    return MakeUnexpected(buffer.error());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningRuntimeCache::uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena){
    if(!__hidden_runtime_cache_resources::ValidateRuntimeMeshUploadPayload(instance))
        return false;

    const bool rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Software and hybrid tails read raw position/index buffers; keep views even with HWRT.

    bool uploaded = CreateMeshSkinningLocalBoundsBuffers(m_graphics, instance);
    auto restPositionBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_RestPositionsSuffix,
        instance.restPositions,
        false,
        __hidden_runtime_cache_resources::s_RestPositionLabel
    );
    if(!restPositionBufferResult){
        instance.restPositionBuffer.reset();
        uploaded = false;
    }
    else
        instance.restPositionBuffer = Move(*restPositionBufferResult);
    auto restNormalBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Half4U>(m_graphics, instance, __hidden_runtime_cache_resources::s_RestNormalsSuffix, instance.restNormals, false, __hidden_runtime_cache_resources::s_RestNormalLabel);
    if(!restNormalBufferResult){
        instance.restNormalBuffer.reset();
        uploaded = false;
    }
    else
        instance.restNormalBuffer = Move(*restNormalBufferResult);
    auto restTangentBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Half4U>(m_graphics, instance, __hidden_runtime_cache_resources::s_RestTangentsSuffix, instance.restTangents, false, __hidden_runtime_cache_resources::s_RestTangentLabel);
    if(!restTangentBufferResult){
        instance.restTangentBuffer.reset();
        uploaded = false;
    }
    else
        instance.restTangentBuffer = Move(*restTangentBufferResult);
    auto skinnedPositionBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_SkinnedPositionsSuffix,
        instance.restPositions,
        true,
        __hidden_runtime_cache_resources::s_SkinnedPositionLabel,
        true,
        rtSupported,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute
    );
    if(!skinnedPositionBufferResult){
        instance.skinnedPositionBuffer.reset();
        uploaded = false;
    }
    else
        instance.skinnedPositionBuffer = Move(*skinnedPositionBufferResult);
    auto skinnedNormalBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_SkinnedNormalsSuffix,
        instance.restNormals,
        true,
        __hidden_runtime_cache_resources::s_SkinnedNormalLabel
    );
    if(!skinnedNormalBufferResult){
        instance.skinnedNormalBuffer.reset();
        uploaded = false;
    }
    else
        instance.skinnedNormalBuffer = Move(*skinnedNormalBufferResult);
    auto skinnedTangentBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_SkinnedTangentsSuffix,
        instance.restTangents,
        true,
        __hidden_runtime_cache_resources::s_SkinnedTangentLabel
    );
    if(!skinnedTangentBufferResult){
        instance.skinnedTangentBuffer.reset();
        uploaded = false;
    }
    else
        instance.skinnedTangentBuffer = Move(*skinnedTangentBufferResult);
    auto uv0BufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Float2U>(m_graphics, instance, __hidden_runtime_cache_resources::s_Uv0Suffix, instance.uv0, false, __hidden_runtime_cache_resources::s_Uv0Label);
    if(!uv0BufferResult){
        instance.uv0Buffer.reset();
        uploaded = false;
    }
    else
        instance.uv0Buffer = Move(*uv0BufferResult);
    auto colorBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<Half4U>(m_graphics, instance, __hidden_runtime_cache_resources::s_ColorsSuffix, instance.colors, false, __hidden_runtime_cache_resources::s_ColorLabel);
    if(!colorBufferResult){
        instance.colorBuffer.reset();
        uploaded = false;
    }
    else
        instance.colorBuffer = Move(*colorBufferResult);
    auto meshletDescBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<MeshletDesc>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_MeshletsSuffix,
        instance.meshlets,
        false,
        __hidden_runtime_cache_resources::s_MeshletDescriptorLabel
    );
    if(!meshletDescBufferResult){
        instance.meshletDescBuffer.reset();
        uploaded = false;
    }
    else
        instance.meshletDescBuffer = Move(*meshletDescBufferResult);
    auto meshletBoundsBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<MeshletBounds>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_MeshletBoundsSuffix,
        instance.meshletBounds,
        true,
        __hidden_runtime_cache_resources::s_MeshletBoundsLabel,
        true
    );
    if(!meshletBoundsBufferResult){
        instance.meshletBoundsBuffer.reset();
        uploaded = false;
    }
    else
        instance.meshletBoundsBuffer = Move(*meshletBoundsBufferResult);
    instance.meshletPositionRefDeltaBuffer.reset();
    auto meshletPositionRefDeltaBufferResult = __hidden_runtime_cache_resources::SetupPaddedRawRuntimeBuffer(
        m_graphics,
        scratchArena,
        instance,
        __hidden_runtime_cache_resources::s_MeshletPositionRefDeltasSuffix,
        instance.meshletPositionRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletPositionRefDeltaLabel
    );
    if(!meshletPositionRefDeltaBufferResult)
        uploaded = false;
    else
        instance.meshletPositionRefDeltaBuffer = Move(*meshletPositionRefDeltaBufferResult);
    instance.meshletAttributeRefDeltaBuffer.reset();
    auto meshletAttributeRefDeltaBufferResult = __hidden_runtime_cache_resources::SetupPaddedRawRuntimeBuffer(
        m_graphics,
        scratchArena,
        instance,
        __hidden_runtime_cache_resources::s_MeshletAttributeRefDeltasSuffix,
        instance.meshletAttributeRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletAttributeRefDeltaLabel
    );
    if(!meshletAttributeRefDeltaBufferResult)
        uploaded = false;
    else
        instance.meshletAttributeRefDeltaBuffer = Move(*meshletAttributeRefDeltaBufferResult);
    auto meshletLocalVertexRefBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<MeshletLocalVertexRef>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_MeshletLocalVertexRefsSuffix,
        instance.meshletLocalVertexRefs,
        false,
        __hidden_runtime_cache_resources::s_MeshletLocalVertexRefLabel
    );
    if(!meshletLocalVertexRefBufferResult){
        instance.meshletLocalVertexRefBuffer.reset();
        uploaded = false;
    }
    else
        instance.meshletLocalVertexRefBuffer = Move(*meshletLocalVertexRefBufferResult);
    instance.meshletPrimitiveIndexBuffer.reset();
    auto meshletPrimitiveIndexBufferResult = __hidden_runtime_cache_resources::SetupPaddedRawRuntimeBuffer(
        m_graphics,
        scratchArena,
        instance,
        __hidden_runtime_cache_resources::s_MeshletPrimitiveIndicesSuffix,
        instance.meshletPrimitiveIndices,
        false,
        __hidden_runtime_cache_resources::s_MeshletPrimitiveIndexLabel
    );
    if(!meshletPrimitiveIndexBufferResult)
        uploaded = false;
    else
        instance.meshletPrimitiveIndexBuffer = Move(*meshletPrimitiveIndexBufferResult);
    auto attributeSkinBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<u32>(
        m_graphics,
        instance,
        __hidden_runtime_cache_resources::s_AttributeSkinsSuffix,
        instance.attributeSkins,
        false,
        __hidden_runtime_cache_resources::s_AttributeSkinLabel
    );
    if(!attributeSkinBufferResult){
        instance.attributeSkinBuffer.reset();
        uploaded = false;
    }
    else
        instance.attributeSkinBuffer = Move(*attributeSkinBufferResult);

    // Both shadow backends trace triangles; always build the reconstructed index buffer.
    if(uploaded){
        const usize indexCount = instance.meshletPrimitiveIndices.size();
        const auto triangleIndices = BuildMeshletTriangleIndices(scratchArena, instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletPositionRefDeltas, instance.meshletPrimitiveIndices, instance.restPositions.size());
        if(!triangleIndices){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct ray tracing triangle indices for runtime mesh '{}'"), instance.handle.value);
            return false;
        }
        if(triangleIndices->size() != indexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: reconstructed ray tracing index count {} does not match expected {} for runtime mesh '{}'"), static_cast<u64>(triangleIndices->size()), static_cast<u64>(indexCount), instance.handle.value);
            return false;
        }

        auto triangleIndexBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<u32>(
            m_graphics,
            instance,
            MeshResourceNames::s_RtTriangleIndicesBufferName,
            *triangleIndices,
            false,
            __hidden_runtime_cache_resources::s_RtTriangleIndexLabel,
            true,
            rtSupported,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            true
        );
        if(!triangleIndexBufferResult){
            instance.triangleIndexBuffer.reset();
            uploaded = false;
        }
        else
            instance.triangleIndexBuffer = Move(*triangleIndexBufferResult);
    }

    // Per-corner trace attributes seeded from bind-pose; repack overwrites normals per frame.
    if(uploaded){
        const auto triangleAttributes = BuildMeshletTriangleAttributes(scratchArena, instance.meshlets, instance.meshletLocalVertexRefs, instance.meshletAttributeRefDeltas, instance.meshletPrimitiveIndices, instance.restNormals, instance.uv0);
        if(!triangleAttributes){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct shadow trace triangle attributes for runtime mesh '{}'"), instance.handle.value);
            return false;
        }

        auto attributeBufferResult = __hidden_runtime_cache_resources::SetupRuntimeBuffer<AttribGpu>(
            m_graphics,
            instance,
            MeshResourceNames::s_RtTriangleAttributesBufferName,
            *triangleAttributes,
            true, // canHaveUavs: the per-frame skinned-normal repack pass writes this buffer as a raw UAV in place
            __hidden_runtime_cache_resources::s_RtTriangleAttributeLabel, true, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute
        );
        if(!attributeBufferResult){
            instance.attributeBuffer.reset();
            uploaded = false;
        }
        else
            instance.attributeBuffer = Move(*attributeBufferResult);
    }

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

