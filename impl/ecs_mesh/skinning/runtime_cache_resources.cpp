// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime_cache.h"

#include "arena_names.h"
#include "resource_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>
#include <core/graphics/module.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/assets_mesh/payload_validation.h>
#include <impl/assets_mesh/skin_validation.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_cache_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/meshlet_ref_range_validation.inl>

[[nodiscard]] bool ValidateRuntimeMeshUploadPayload(Core::Alloc::GlobalArena& arena, const MeshSkinningRuntimeInstance& instance){
    TString<Core::Alloc::GlobalArena> sourceText{arena};
    if(instance.sourceName)
        sourceText = StringConvert(arena, instance.sourceName.c_str());
    else
        sourceText.assign(NWB_TEXT("<unnamed>"));

    if(!Core::Mesh::MeshClassUsesSkinning(instance.meshClass)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has invalid mesh class")
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
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has incomplete split mesh payload")
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
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' exceeds u32 payload limits")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has skin but no skeleton joint count")
            , TStringView(sourceText)
        );
        return false;
    }
    if(instance.skeletonJointCount > static_cast<u32>(Limit<u16>::s_Max) + 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' skeleton joint count {} exceeds skin stream limits")
            , TStringView(sourceText)
            , instance.skeletonJointCount
        );
        return false;
    }
    if(!SkinValidation::ValidInverseBindMatrices(instance.inverseBindMatrices, instance.skeletonJointCount)){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' inverse bind matrices are invalid")
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
            SkinValidation::ValidSkinInfluenceWeights(weights)
            && SkinValidation::SkinInfluenceFitsSkeleton(skin, instance.skeletonJointCount, failedJoint)
        )
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' skin influence {} is invalid")
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

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} position ref {} is out of range")
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

            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' meshlet {} attribute ref {} is out of range")
                , TStringView(sourceText)
                , meshletIndex
                , localAttributeIndex
            );
            return false;
        }
    }
    if(logicalAttributeRefIndex != instance.attributeSkins.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: runtime mesh '{}' has mismatched attribute skin payload")
            , TStringView(sourceText)
        );
        return false;
    }

    return true;
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] Core::BufferHandle SetupRuntimeBuffer(
    Core::Graphics& graphics,
    const MeshSkinningRuntimeInstance& instance,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const tchar* label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    const Name bufferName = DeriveRuntimeResourceName(
        instance.sourceName,
        instance.entity.id,
        instance.editRevision,
        suffix
    );
    if(!bufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to derive {} buffer name for runtime mesh '{}'")
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
        { canHaveUavs, canHaveRawViews, accelStructBuildInput },
        buffer
    );
    switch(failure){
    case RuntimeMeshBufferUpload::BufferSetupFailure::None:
        return buffer;
    case RuntimeMeshBufferUpload::BufferSetupFailure::EmptyPayload:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload is empty"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::ByteSizeOverflow:
        NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: {} payload byte size overflows"), label);
        return {};
    case RuntimeMeshBufferUpload::BufferSetupFailure::CreateFailed:
        break;
    }
    NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to create {} buffer for runtime mesh '{}'")
        , label
        , instance.handle.value
    );
    return {};
}

template<typename PayloadT, typename PayloadVector>
[[nodiscard]] bool AssignRuntimeBuffer(
    Core::Graphics& graphics,
    MeshSkinningRuntimeInstance& instance,
    Core::BufferHandle& outBuffer,
    const AStringView suffix,
    const PayloadVector& payload,
    const bool canHaveUavs,
    const tchar* label,
    const bool canHaveRawViews = false,
    const bool accelStructBuildInput = false
){
    outBuffer = SetupRuntimeBuffer<PayloadT>(
        graphics,
        instance,
        suffix,
        payload,
        canHaveUavs,
        label,
        canHaveRawViews,
        accelStructBuildInput
    );
    return outBuffer != nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshSkinningRuntimeCache::uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance){
    if(!__hidden_runtime_cache_resources::ValidateRuntimeMeshUploadPayload(m_arena, instance))
        return false;

    const bool rtSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct);
    // Without hardware ray tracing the software BVH shadow fallback runs instead, reading the skinned
    // positions and triangle indices as raw byte buffers, so those buffers need raw views in that case.
    const bool swShadow = !rtSupported;

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
        NWB_TEXT("skinned position"),
        swShadow,
        rtSupported
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

    // Both shadow backends trace triangles, so the reconstructed index buffer is built whenever the upload
    // otherwise succeeded; the hardware path consumes it as an accel-struct input and the software fallback
    // reads it as a raw byte buffer.
    if(uploaded){
        const usize indexCount = instance.meshletPrimitiveIndices.size();
        Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_RuntimeBlasIndexArena, indexCount * sizeof(u32) + 4096u);
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
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct ray tracing triangle indices for runtime mesh '{}'")
                , instance.handle.value
            );
            return false;
        }
        if(triangleIndices.size() != indexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: reconstructed ray tracing index count {} does not match expected {} for runtime mesh '{}'")
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
            AStringView("rt_triangle_indices"),
            triangleIndices,
            false,
            NWB_TEXT("rt triangle index"),
            swShadow,
            rtSupported
        ) && uploaded;
    }

    // Flat per-triangle-corner shadow/caustic trace attribute buffer, indexed as primitive*3+corner in lockstep
    // with the reconstructed triangle index buffer above. SEEDED from the BIND-POSE streams (uv0 is pose-invariant;
    // normal is the rest normal); for an actively-skinned mesh the normal half is then OVERWRITTEN per frame from the
    // deformed normals by the repack compute pass (repack_normals_cs.slang, dispatchRepackNormals), so RT traces bend
    // on the live pose while preserving raster hard/soft edge semantics. Both shadow backends read it as a
    // ByteAddressBuffer, so it carries a raw view; canHaveUavs lets the repack pass write it as a raw UAV in place.
    if(uploaded){
        const usize attributeCount = instance.meshletPrimitiveIndices.size();
        Core::Alloc::ScratchArena scratchArena(SkinningArenaScope::s_RuntimeBlasAttributeArena, attributeCount * sizeof(AttribGpu) + 4096u);
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
            NWB_LOGGER_ERROR(NWB_TEXT("MeshSkinningRuntimeCache: failed to reconstruct shadow trace triangle attributes for runtime mesh '{}'")
                , instance.handle.value
            );
            return false;
        }

        uploaded = __hidden_runtime_cache_resources::AssignRuntimeBuffer<AttribGpu>(
            m_graphics,
            instance,
            instance.attributeBuffer,
            AStringView("rt_triangle_attributes"),
            triangleAttributes,
            true, // canHaveUavs: the per-frame skinned-normal repack pass writes this buffer as a raw UAV in place
            NWB_TEXT("rt triangle attribute"),
            true
        ) && uploaded;
    }

    return uploaded;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

