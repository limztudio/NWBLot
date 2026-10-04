// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"
#include "arena_names.h"
#include "local_bounds.h"
#include "resource_names.h"

#include <impl/ecs_render/kernel/arena_names.h>

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/assets_mesh/payload_validation.h>
#include <impl/assets_mesh/skin_validation.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>
#include <impl/assets_mesh/meshlet_ref_validation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Each runtime-BLAS stream needs allocator overhead beyond its typed payload.
static constexpr usize s_RuntimeBlasScratchArenaOverheadBytes = 4096u;

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
static constexpr TStringView s_RestPositionLabel = GLOBAL_TEXT("rest position");
static constexpr TStringView s_RestNormalLabel = GLOBAL_TEXT("rest normal");
static constexpr TStringView s_RestTangentLabel = GLOBAL_TEXT("rest tangent");
static constexpr TStringView s_SkinnedPositionLabel = GLOBAL_TEXT("skinned position");
static constexpr TStringView s_SkinnedNormalLabel = GLOBAL_TEXT("skinned normal");
static constexpr TStringView s_SkinnedTangentLabel = GLOBAL_TEXT("skinned tangent");
static constexpr TStringView s_Uv0Label = GLOBAL_TEXT("uv0");
static constexpr TStringView s_ColorLabel = GLOBAL_TEXT("color");
static constexpr TStringView s_MeshletDescriptorLabel = GLOBAL_TEXT("meshlet descriptor");
static constexpr TStringView s_MeshletBoundsLabel = GLOBAL_TEXT("meshlet bounds");
static constexpr TStringView s_MeshletPositionRefDeltaLabel = GLOBAL_TEXT("meshlet position ref delta");
static constexpr TStringView s_MeshletAttributeRefDeltaLabel = GLOBAL_TEXT("meshlet attribute ref delta");
static constexpr TStringView s_MeshletLocalVertexRefLabel = GLOBAL_TEXT("meshlet local vertex ref");
static constexpr TStringView s_MeshletPrimitiveIndexLabel = GLOBAL_TEXT("meshlet primitive index");
static constexpr TStringView s_AttributeSkinLabel = GLOBAL_TEXT("attribute skin");
static constexpr TStringView s_RtTriangleIndexLabel = GLOBAL_TEXT("rt triangle index");
static constexpr TStringView s_RtTriangleAttributeLabel = GLOBAL_TEXT("rt triangle attribute");


[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(Core::Alloc::GlobalArena& arena, const MeshSkinningRuntimeInstance& instance){
    TString<Core::Alloc::GlobalArena> sourceText{arena};
    if(instance.sourceName)
        sourceText = StringConvert(arena, instance.sourceName.resolvedText());
    else
        sourceText.assign(GLOBAL_TEXT("<unnamed>"));

    if(!Core::Mesh::MeshClassUsesSkinning(instance.meshClass)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has invalid mesh class")
            , TStringView(sourceText)
        );
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
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has incomplete split mesh payload")
            , TStringView(sourceText)
        );
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
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' exceeds u32 payload limits")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has skin but no skeleton joint count")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits")
            , TStringView(sourceText)
            , instance.skeletonJointCount
        );
        return false;
    }
    if(!SkinValidation::ValidInverseBindMatrixCount(instance.inverseBindMatrices.size(), instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' inverse bind matrices are invalid")
            , TStringView(sourceText)
        );
        return false;
    }
    for(const SkeletonJointMatrix& storedInverseBind : instance.inverseBindMatrices){
        const SIMDMatrix inverseBind = LoadFloat(storedInverseBind);
        if(MatrixIsInvertibleAffine(inverseBind, SkinValidation::s_Epsilon, SkinValidation::s_Epsilon))
            continue;

        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' inverse bind matrices are invalid")
            , TStringView(sourceText)
        );
        return false;
    }

    for(usize skinIndex = 0u; skinIndex < instance.skin.size(); ++skinIndex){
        const SkinInfluence4& skin = instance.skin[skinIndex];
        const SIMDVector weights = LoadFloat(skin.weight);
        u32 failedJoint = 0u;
        if(
            SkinValidation::ValidSkinInfluenceWeights(weights)
            && SkinValidation::SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount, failedJoint)
        )
            continue;

        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' skin influence {} is invalid")
            , TStringView(sourceText)
            , skinIndex
        );
        return false;
    }

    usize logicalAttributeRefIndex = 0u;
    for(usize meshletIndex = 0u; meshletIndex < instance.meshlets.size(); ++meshletIndex){
        const MeshletDesc& meshlet = instance.meshlets[meshletIndex];
        for(u32 localPositionIndex = 0u; localPositionIndex < MeshletPositionCount(meshlet); ++localPositionIndex){
            MeshletPositionStreamRef ref;
            if(
                DecodeMeshletPositionRef(
                    instance.meshletPositionRefDeltas.data(),
                    instance.meshletPositionRefDeltas.size(),
                    meshlet,
                    localPositionIndex,
                    true,
                    ref
                )
                && MeshMeshletRefValidation::MeshletPositionRefInRange(ref, instance.restPositions.size(), instance.skin.size(), true)
            )
                continue;

            NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} position ref {} is out of range")
                , TStringView(sourceText)
                , meshletIndex
                , localPositionIndex
            );
            return false;
        }
        for(u32 localAttributeIndex = 0u; localAttributeIndex < MeshletAttributeCount(meshlet); ++localAttributeIndex){
            MeshletAttributeStreamRef ref;
            if(
                DecodeMeshletAttributeRef(
                    instance.meshletAttributeRefDeltas.data(),
                    instance.meshletAttributeRefDeltas.size(),
                    meshlet,
                    localAttributeIndex,
                    ref
                )
                && MeshMeshletRefValidation::MeshletAttributeRefInRange(
                    ref,
                    instance.restNormals.size(),
                    instance.restTangents.size(),
                    instance.uv0.size(),
                    instance.colors.size()
                )
                && logicalAttributeRefIndex < instance.attributeSkins.size()
                && instance.attributeSkins[logicalAttributeRefIndex] < instance.skin.size()
            ){
                ++logicalAttributeRefIndex;
                continue;
            }

            NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} attribute ref {} is out of range")
                , TStringView(sourceText)
                , meshletIndex
                , localAttributeIndex
            );
            return false;
        }
    }
    if(logicalAttributeRefIndex != instance.attributeSkins.size()){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has mismatched attribute skin payload")
            , TStringView(sourceText)
        );
        return false;
    }

    return true;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] Core::BufferHandle SetupRuntimeBuffer(
    Core::GraphicsRuntime& graphics,
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const TStringView label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
    const bool isIndexBuffer = false){
    const Name bufferName = DeriveRuntimeResourceName(
        instance.sourceName,
        instance.entity.id,
        instance.editRevision,
        suffix
    );
    if(!bufferName){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'")
            , label
            , instance.handle.value
        );
        return {};
    }

    Core::BufferHandle buffer;
    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure = RuntimeMeshBufferUpload::SetupRequiredBuffer<PayloadT>(
        graphics,
        bufferName,
        payload,
        { canHaveUavs, canHaveRawViews, accelStructBuildInput, queueSharing, isIndexBuffer },
        buffer
    );
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return buffer;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'")
        , label
        , instance.handle.value
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] bool AssignRuntimeBuffer(
    Core::GraphicsRuntime& graphics,
    MeshSkinningRuntimeInstance& instance,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const TStringView label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
    const bool isIndexBuffer = false){
    outBuffer = SetupRuntimeBuffer<PayloadT>(
        graphics,
        instance,
        suffix,
        payload,
        canHaveUavs,
        label,
        canHaveRawViews,
        accelStructBuildInput,
        queueSharing,
        isIndexBuffer
    );
    return outBuffer != nullptr;
}

template<typename PayloadVector>
[[nodiscard]] bool AssignPaddedRawRuntimeBuffer(
    Core::GraphicsRuntime& graphics,
    Core::Alloc::GlobalArena& arena,
    MeshSkinningRuntimeInstance& instance,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const TStringView label,
    const Core::ResourceQueueSharing::Mask queueSharing = Core::ResourceQueueSharing::GraphicsAndAsyncCompute
){
    outBuffer = nullptr;

    const Name bufferName = DeriveRuntimeResourceName(
        instance.sourceName,
        instance.entity.id,
        instance.editRevision,
        suffix
    );
    if(!bufferName){
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'")
            , label
            , instance.handle.value
        );
        return false;
    }

    const RuntimeMeshBufferUpload::BufferSetupFailure::Enum failure =
        RuntimeMeshBufferUpload::SetupRequiredPaddedRawByteBuffer(
            graphics,
            arena,
            bufferName,
            payload,
            { canHaveUavs, true, false, queueSharing },
            outBuffer
        )
    ;
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return true;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return false;
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'"),
            label,
            instance.handle.value
        );
        return false;
    }

    GLOBAL_ASSERT(false);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningRuntimeCache::uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance){
    if(!__hidden_runtime_cache_resources::ValidateRuntimeMeshUploadPayload(m_arena, instance))
        return false;

    const bool rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Software and hybrid tails read raw position/index buffers; keep views even with HWRT.

    bool uploaded = CreateMeshSkinningLocalBoundsBuffers(m_graphics, instance);
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.restPositionBuffer,
        __hidden_runtime_cache_resources::s_RestPositionsSuffix,
        instance.restPositions,
        false,
        __hidden_runtime_cache_resources::s_RestPositionLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restNormalBuffer,
        __hidden_runtime_cache_resources::s_RestNormalsSuffix,
        instance.restNormals,
        false,
        __hidden_runtime_cache_resources::s_RestNormalLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restTangentBuffer,
        __hidden_runtime_cache_resources::s_RestTangentsSuffix,
        instance.restTangents,
        false,
        __hidden_runtime_cache_resources::s_RestTangentLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.skinnedPositionBuffer,
        __hidden_runtime_cache_resources::s_SkinnedPositionsSuffix,
        instance.restPositions,
        true,
        __hidden_runtime_cache_resources::s_SkinnedPositionLabel,
        true,
        rtSupported,
        Core::ResourceQueueSharing::GraphicsAndAsyncCompute
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedNormalBuffer,
        __hidden_runtime_cache_resources::s_SkinnedNormalsSuffix,
        instance.restNormals,
        true,
        __hidden_runtime_cache_resources::s_SkinnedNormalLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedTangentBuffer,
        __hidden_runtime_cache_resources::s_SkinnedTangentsSuffix,
        instance.restTangents,
        true,
        __hidden_runtime_cache_resources::s_SkinnedTangentLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float2U>(
        m_graphics,
        instance,
        instance.uv0Buffer,
        __hidden_runtime_cache_resources::s_Uv0Suffix,
        instance.uv0,
        false,
        __hidden_runtime_cache_resources::s_Uv0Label
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.colorBuffer,
        __hidden_runtime_cache_resources::s_ColorsSuffix,
        instance.colors,
        false,
        __hidden_runtime_cache_resources::s_ColorLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletDesc>(
        m_graphics,
        instance,
        instance.meshletDescBuffer,
        __hidden_runtime_cache_resources::s_MeshletsSuffix,
        instance.meshlets,
        false,
        __hidden_runtime_cache_resources::s_MeshletDescriptorLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletBounds>(
        m_graphics,
        instance,
        instance.meshletBoundsBuffer,
        __hidden_runtime_cache_resources::s_MeshletBoundsSuffix,
        instance.meshletBounds,
        true,
        __hidden_runtime_cache_resources::s_MeshletBoundsLabel,
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        m_graphics,
        m_arena,
        instance,
        instance.meshletPositionRefDeltaBuffer,
        __hidden_runtime_cache_resources::s_MeshletPositionRefDeltasSuffix,
        instance.meshletPositionRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletPositionRefDeltaLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        m_graphics,
        m_arena,
        instance,
        instance.meshletAttributeRefDeltaBuffer,
        __hidden_runtime_cache_resources::s_MeshletAttributeRefDeltasSuffix,
        instance.meshletAttributeRefDeltas,
        false,
        __hidden_runtime_cache_resources::s_MeshletAttributeRefDeltaLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletLocalVertexRef>(
        m_graphics,
        instance,
        instance.meshletLocalVertexRefBuffer,
        __hidden_runtime_cache_resources::s_MeshletLocalVertexRefsSuffix,
        instance.meshletLocalVertexRefs,
        false,
        __hidden_runtime_cache_resources::s_MeshletLocalVertexRefLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignPaddedRawRuntimeBuffer(
        m_graphics,
        m_arena,
        instance,
        instance.meshletPrimitiveIndexBuffer,
        __hidden_runtime_cache_resources::s_MeshletPrimitiveIndicesSuffix,
        instance.meshletPrimitiveIndices,
        false,
        __hidden_runtime_cache_resources::s_MeshletPrimitiveIndexLabel
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
        m_graphics,
        instance,
        instance.attributeSkinBuffer,
        __hidden_runtime_cache_resources::s_AttributeSkinsSuffix,
        instance.attributeSkins,
        false,
        __hidden_runtime_cache_resources::s_AttributeSkinLabel
    ) && uploaded;

    // Both shadow backends trace triangles; always build the reconstructed index buffer.
    if(uploaded){
        const usize indexCount = instance.meshletPrimitiveIndices.size();
        Core::Alloc::ScratchArena scratchArena(
            SkinningArenaScope::s_RuntimeBlasIndexArena,
            indexCount * sizeof(u32) + __hidden_runtime_cache_resources::s_RuntimeBlasScratchArenaOverheadBytes
        );
        Vector<u32, Core::Alloc::ScratchArena> triangleIndices{ scratchArena };
        triangleIndices.reserve(indexCount);
        if(!BuildMeshletTriangleIndices(
            instance.meshlets,
            instance.meshletLocalVertexRefs,
            instance.meshletPositionRefDeltas,
            instance.meshletPrimitiveIndices,
            instance.restPositions.size(),
            triangleIndices
        )){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to reconstruct ray tracing triangle indices for runtime mesh '{}'")
                , instance.handle.value
            );
            return false;
        }
        if(triangleIndices.size() != indexCount){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: reconstructed ray tracing index count {} does not match expected {} for runtime mesh '{}'")
                , static_cast<u64>(triangleIndices.size())
                , static_cast<u64>(indexCount)
                , instance.handle.value
            );
            return false;
        }

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
            m_graphics,
            instance,
            instance.triangleIndexBuffer,
            RendererArenaScope::s_RtTriangleIndicesBufferName,
            triangleIndices,
            false,
            __hidden_runtime_cache_resources::s_RtTriangleIndexLabel,
            true,
            rtSupported,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute,
            true
        ) && uploaded;
    }

    // Per-corner trace attributes seeded from bind-pose; repack overwrites normals per frame.
    if(uploaded){
        const usize attributeCount = instance.meshletPrimitiveIndices.size();
        Core::Alloc::ScratchArena scratchArena(
            SkinningArenaScope::s_RuntimeBlasAttributeArena,
            attributeCount * sizeof(AttribGpu) + __hidden_runtime_cache_resources::s_RuntimeBlasScratchArenaOverheadBytes
        );
        Vector<AttribGpu, Core::Alloc::ScratchArena> triangleAttributes{ scratchArena };
        if(!BuildMeshletTriangleAttributes(
            instance.meshlets,
            instance.meshletLocalVertexRefs,
            instance.meshletAttributeRefDeltas,
            instance.meshletPrimitiveIndices,
            instance.restNormals,
            instance.uv0,
            triangleAttributes
        )){
            NWB_LOGGER_ERROR(GLOBAL_TEXT("MeshSkinningRuntimeCache: failed to reconstruct shadow trace triangle attributes for runtime mesh '{}'")
                , instance.handle.value
            );
            return false;
        }

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<AttribGpu>(
            m_graphics,
            instance,
            instance.attributeBuffer,
            RendererArenaScope::s_RtTriangleAttributesBufferName,
            triangleAttributes,
            true, // canHaveUavs: the per-frame skinned-normal repack pass writes this buffer as a raw UAV in place
            __hidden_runtime_cache_resources::s_RtTriangleAttributeLabel,
            true,
            false,
            Core::ResourceQueueSharing::GraphicsAndAsyncCompute
        ) && uploaded;
    }

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

