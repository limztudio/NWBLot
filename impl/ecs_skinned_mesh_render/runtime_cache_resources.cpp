// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include "resource_names.h"

#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/assets_mesh/meshlet_ref_encoding.h>
#include <impl/assets_mesh/payload_validation.h>
#include <impl/assets_mesh/skinned_validation.h>
#include <impl/ecs_mesh_runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshPayloadValidation::CountFitsU32;

#include <impl/assets_mesh/meshlet_ref_range_validation.inl>

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(Core::Alloc::GlobalArena& arena, const SkinnedMeshRuntimeMeshInstance& instance){
    TString<Core::Alloc::GlobalArena> sourceText{arena};
    if(instance.source.name())
        sourceText = StringConvert(arena, instance.source.name().c_str());
    else
        sourceText.assign(NWB_TEXT("<unnamed>"));

    if(!Core::Mesh::MeshClassUsesSkinning(instance.meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' has invalid mesh class")
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
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' has incomplete split mesh payload")
            , TStringView(sourceText)
        );
        return false;
    }
    if(
        !CountFitsU32(instance.restPositions.size())
        || !CountFitsU32(instance.uv0.size())
        || !CountFitsU32(instance.colors.size())
        || !CountFitsU32(instance.skin.size())
        || !CountFitsU32(instance.meshlets.size())
        || !CountFitsU32(instance.meshletPositionRefDeltas.size())
        || !CountFitsU32(instance.meshletAttributeRefDeltas.size())
        || !CountFitsU32(instance.meshletLocalVertexRefs.size())
        || !CountFitsU32(instance.meshletPrimitiveIndices.size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' exceeds u32 payload limits")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' has skin but no skeleton joint count")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits")
            , TStringView(sourceText)
            , instance.skeletonJointCount
        );
        return false;
    }
    if(!SkinnedMeshValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' inverse bind matrices are invalid")
            , TStringView(sourceText)
        );
        return false;
    }

    for(usize skinIndex = 0u; skinIndex < instance.skin.size(); ++skinIndex){
        const SkinInfluence4& skin = instance.skin[skinIndex];
        const SIMDVector weights = VectorSet(
            skin.weight[0u],
            skin.weight[1u],
            skin.weight[2u],
            skin.weight[3u]
        );
        u32 failedJoint = 0u;
        if(
            SkinnedMeshValidation::ValidSkinInfluenceWeights(weights)
            && SkinnedMeshValidation::SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount, failedJoint)
        )
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' skin influence {} is invalid")
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
                && MeshletPositionRefInRange(ref, instance.restPositions.size(), instance.skin.size(), true)
            )
                continue;

            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' meshlet {} position ref {} is out of range")
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
                && MeshletAttributeRefInRange(
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

            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' meshlet {} attribute ref {} is out of range")
                , TStringView(sourceText)
                , meshletIndex
                , localAttributeIndex
            );
            return false;
        }
    }
    if(logicalAttributeRefIndex != instance.attributeSkins.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' has mismatched attribute skin payload")
            , TStringView(sourceText)
        );
        return false;
    }

    return true;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] Core::BufferHandle SetupRuntimeBuffer(
    Core::Graphics& graphics,
    const SkinnedMeshRuntimeMeshInstance& instance,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const tchar* label,
    const bool canHaveRawViews = false
){
    const Name bufferName = DeriveRuntimeResourceName(
        instance.source.name(),
        instance.entity.id,
        instance.editRevision,
        suffix
    );
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: failed to derive {} buffer name for runtime mesh '{}'")
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
        { canHaveUavs, canHaveRawViews },
        buffer
    );
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return buffer;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: {} payload is empty"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: {} payload byte size overflows"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: failed to create {} buffer for runtime mesh '{}'")
        , label
        , instance.handle.value
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] bool AssignRuntimeBuffer(
    Core::Graphics& graphics,
    SkinnedMeshRuntimeMeshInstance& instance,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const tchar* label,
    const bool canHaveRawViews = false
){
    outBuffer = SetupRuntimeBuffer<PayloadT>(
        graphics,
        instance,
        suffix,
        payload,
        canHaveUavs,
        label,
        canHaveRawViews
    );
    return outBuffer != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SkinnedMeshRuntimeMeshCache::uploadRuntimeMeshBuffers(SkinnedMeshRuntimeMeshInstance& instance){
    if(!__hidden_runtime_cache_resources::ValidateRuntimeMeshUploadPayload(m_arena, instance))
        return false;

    bool uploaded = true;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.restPositionBuffer,
        AStringView("rest_positions"),
        instance.restPositions,
        false,
        NWB_TEXT("rest position")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restNormalBuffer,
        AStringView("rest_normals"),
        instance.restNormals,
        false,
        NWB_TEXT("rest normal")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restTangentBuffer,
        AStringView("rest_tangents"),
        instance.restTangents,
        false,
        NWB_TEXT("rest tangent")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.skinnedPositionBuffer,
        AStringView("skinned_positions"),
        instance.restPositions,
        true,
        NWB_TEXT("skinned position")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedNormalBuffer,
        AStringView("skinned_normals"),
        instance.restNormals,
        true,
        NWB_TEXT("skinned normal")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedTangentBuffer,
        AStringView("skinned_tangents"),
        instance.restTangents,
        true,
        NWB_TEXT("skinned tangent")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Float2U>(
        m_graphics,
        instance,
        instance.uv0Buffer,
        AStringView("uv0"),
        instance.uv0,
        false,
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.colorBuffer,
        AStringView("colors"),
        instance.colors,
        false,
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletDesc>(
        m_graphics,
        instance,
        instance.meshletDescBuffer,
        AStringView("meshlets"),
        instance.meshlets,
        false,
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletBounds>(
        m_graphics,
        instance,
        instance.meshletBoundsBuffer,
        AStringView("meshlet_bounds"),
        instance.meshletBounds,
        true,
        NWB_TEXT("meshlet bounds"),
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u8>(
        m_graphics,
        instance,
        instance.meshletPositionRefDeltaBuffer,
        AStringView("meshlet_position_ref_deltas"),
        instance.meshletPositionRefDeltas,
        false,
        NWB_TEXT("meshlet position ref delta"),
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u8>(
        m_graphics,
        instance,
        instance.meshletAttributeRefDeltaBuffer,
        AStringView("meshlet_attribute_ref_deltas"),
        instance.meshletAttributeRefDeltas,
        false,
        NWB_TEXT("meshlet attribute ref delta"),
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<MeshletLocalVertexRef>(
        m_graphics,
        instance,
        instance.meshletLocalVertexRefBuffer,
        AStringView("meshlet_local_vertex_refs"),
        instance.meshletLocalVertexRefs,
        false,
        NWB_TEXT("meshlet local vertex ref")
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u8>(
        m_graphics,
        instance,
        instance.meshletPrimitiveIndexBuffer,
        AStringView("meshlet_primitive_indices"),
        instance.meshletPrimitiveIndices,
        false,
        NWB_TEXT("meshlet primitive index"),
        true
    ) && uploaded;
    uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<u32>(
        m_graphics,
        instance,
        instance.attributeSkinBuffer,
        AStringView("attribute_skins"),
        instance.attributeSkins,
        false,
        NWB_TEXT("attribute skin")
    ) && uploaded;

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

