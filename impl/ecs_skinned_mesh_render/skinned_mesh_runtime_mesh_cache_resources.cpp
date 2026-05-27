// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_runtime_mesh_cache.h"

#include <core/common/log.h>
#include <core/graphics/graphics.h>
#include <impl/assets_mesh/skinned_mesh_validation.h>
#include <impl/ecs_mesh/runtime_mesh_buffer_upload.h>

#include "skinned_mesh_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_mesh_runtime_mesh_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool CountFitsU32(const usize count){
    return count <= static_cast<usize>(Limit<u32>::s_Max);
}

[[nodiscard]] bool RuntimeVertexRefInRange(
    const MeshVertexRef& ref,
    const SkinnedMeshRuntimeMeshInstance& instance
){
    return
        ref.position < instance.restPositions.size()
        && ref.normal < instance.restNormals.size()
        && ref.tangent < instance.restTangents.size()
        && ref.uv0 < instance.uv0.size()
        && ref.color < instance.colors.size()
        && ref.skin < instance.skin.size()
    ;
}

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
        || instance.restNormals.size() != instance.restPositions.size()
        || instance.restTangents.size() != instance.restPositions.size()
        || instance.uv0.empty()
        || instance.colors.empty()
        || instance.skin.empty()
        || instance.vertexRefs.empty()
        || instance.meshlets.empty()
        || instance.meshletBounds.size() != instance.meshlets.size()
        || instance.meshletVertexRefs.empty()
        || instance.meshletPrimitiveIndices.empty()
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
        || !CountFitsU32(instance.vertexRefs.size())
        || !CountFitsU32(instance.meshlets.size())
        || !CountFitsU32(instance.meshletVertexRefs.size())
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

    for(usize vertexRefIndex = 0u; vertexRefIndex < instance.vertexRefs.size(); ++vertexRefIndex){
        if(RuntimeVertexRefInRange(instance.vertexRefs[vertexRefIndex], instance))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: runtime mesh '{}' vertex ref {} is out of range")
            , TStringView(sourceText)
            , vertexRefIndex
        );
        return false;
    }

    return true;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] Core::BufferHandle SetupRuntimeBuffer(
    Core::Graphics& graphics,
    const Name& debugName,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const tchar* label,
    const bool canHaveRawViews = false
){
    if(payload.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: {} payload is empty"), label);
        return {};
    }
    if(!RuntimeMeshBufferUpload::PayloadByteCountFits<PayloadT>(payload)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshRuntimeMeshCache: {} payload byte size overflows"), label);
        return {};
    }

    return RuntimeMeshBufferUpload::SetupBuffer<PayloadT>(
        graphics,
        debugName,
        payload,
        { canHaveUavs, canHaveRawViews }
    );
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

    Core::BufferHandle buffer = SetupRuntimeBuffer<PayloadT>(
        graphics,
        bufferName,
        payload,
        canHaveUavs,
        label,
        canHaveRawViews
    );
    if(buffer)
        return buffer;

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
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<MeshVertexRef>(
        m_graphics,
        instance,
        instance.vertexRefBuffer,
        AStringView("vertex_refs"),
        instance.vertexRefs,
        false,
        NWB_TEXT("vertex ref")
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
        NWB_TEXT("meshlet bounds")
    ) && uploaded;
    uploaded = __hidden_skinned_mesh_runtime_mesh_cache_resources::AssignRuntimeBuffer<u32>(
        m_graphics,
        instance,
        instance.meshletVertexRefBuffer,
        AStringView("meshlet_vertex_refs"),
        instance.meshletVertexRefs,
        false,
        NWB_TEXT("meshlet vertex ref")
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

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

