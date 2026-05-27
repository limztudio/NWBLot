// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_runtime_mesh_cache.h"

#include <core/common/log.h>
#include <core/graphics/graphics.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/mesh_payload_validation.h>
#include <impl/assets_mesh/skinned_mesh_validation.h>
#include <impl/ecs_mesh/runtime_mesh_buffer_upload.h>

#include "skinned_mesh_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_mesh_runtime_mesh_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MeshPayloadValidation::CountFitsU32;

#include <impl/assets_mesh/meshlet_ref_validation.inl>

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
        || instance.meshletPositionRefs.empty()
        || instance.meshletAttributeRefs.empty()
        || instance.meshletLocalVertexRefs.empty()
        || instance.meshletPrimitiveIndices.empty()
        || instance.attributeSkins.size() != instance.meshletAttributeRefs.size()
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
        || !CountFitsU32(instance.meshletPositionRefs.size())
        || !CountFitsU32(instance.meshletAttributeRefs.size())
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

    for(usize positionRefIndex = 0u; positionRefIndex < instance.meshletPositionRefs.size(); ++positionRefIndex){
        if(MeshletPositionRefInRange(
            instance.meshletPositionRefs[positionRefIndex],
            instance.restPositions.size(),
            instance.skin.size(),
            true
        ))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' position ref {} is out of range")
            , TStringView(sourceText)
            , positionRefIndex
        );
        return false;
    }
    for(usize attributeRefIndex = 0u; attributeRefIndex < instance.meshletAttributeRefs.size(); ++attributeRefIndex){
        if(
            MeshletAttributeRefInRange(
                instance.meshletAttributeRefs[attributeRefIndex],
                instance.restNormals.size(),
                instance.restTangents.size(),
                instance.uv0.size(),
                instance.colors.size()
            )
            && instance.attributeSkins[attributeRefIndex] < instance.skin.size()
        )
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' attribute ref {} is out of range")
            , TStringView(sourceText)
            , attributeRefIndex
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


bool SkinnedMeshRuntimeMeshCache::uploadRuntimeMeshBuffers(SkinnedMeshRuntimeMeshInstance& instance){
    if(!__hidden_skinned_mesh_runtime_mesh_cache_resources::ValidateRuntimeMeshUploadPayload(m_arena, instance))
        return false;

    bool uploaded = true;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.restPositionBuffer,
        AStringView("rest_positions"),
        instance.restPositions,
        false,
        NWB_TEXT("rest position")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restNormalBuffer,
        AStringView("rest_normals"),
        instance.restNormals,
        false,
        NWB_TEXT("rest normal")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.restTangentBuffer,
        AStringView("rest_tangents"),
        instance.restTangents,
        false,
        NWB_TEXT("rest tangent")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Float3U>(
        m_graphics,
        instance,
        instance.skinnedPositionBuffer,
        AStringView("skinned_positions"),
        instance.restPositions,
        true,
        NWB_TEXT("skinned position")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedNormalBuffer,
        AStringView("skinned_normals"),
        instance.restNormals,
        true,
        NWB_TEXT("skinned normal")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.skinnedTangentBuffer,
        AStringView("skinned_tangents"),
        instance.restTangents,
        true,
        NWB_TEXT("skinned tangent")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Float2U>(
        m_graphics,
        instance,
        instance.uv0Buffer,
        AStringView("uv0"),
        instance.uv0,
        false,
        NWB_TEXT("uv0")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<Half4U>(
        m_graphics,
        instance,
        instance.colorBuffer,
        AStringView("colors"),
        instance.colors,
        false,
        NWB_TEXT("color")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshletDesc>(
        m_graphics,
        instance,
        instance.meshletDescBuffer,
        AStringView("meshlets"),
        instance.meshlets,
        false,
        NWB_TEXT("meshlet descriptor")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshletBounds>(
        m_graphics,
        instance,
        instance.meshletBoundsBuffer,
        AStringView("meshlet_bounds"),
        instance.meshletBounds,
        false,
        NWB_TEXT("meshlet bounds"),
        true
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshletDeformedPositionRef>(
        m_graphics,
        instance,
        instance.meshletPositionRefBuffer,
        AStringView("meshlet_position_refs"),
        instance.meshletPositionRefs,
        false,
        NWB_TEXT("meshlet position ref")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshletShadingAttributeRef>(
        m_graphics,
        instance,
        instance.meshletAttributeRefBuffer,
        AStringView("meshlet_attribute_refs"),
        instance.meshletAttributeRefs,
        false,
        NWB_TEXT("meshlet attribute ref")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshletLocalVertexRef>(
        m_graphics,
        instance,
        instance.meshletLocalVertexRefBuffer,
        AStringView("meshlet_local_vertex_refs"),
        instance.meshletLocalVertexRefs,
        false,
        NWB_TEXT("meshlet local vertex ref")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<u8>(
        m_graphics,
        instance,
        instance.meshletPrimitiveIndexBuffer,
        AStringView("meshlet_primitive_indices"),
        instance.meshletPrimitiveIndices,
        false,
        NWB_TEXT("meshlet primitive index"),
        true
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<u32>(
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

