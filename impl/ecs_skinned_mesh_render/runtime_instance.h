// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/common.h>
#include <core/mesh/classification.h>
#include <impl/assets_mesh/payload_types.h>
#include <impl/assets_mesh/skinned_types.h>
#include <impl/ecs_skinned_mesh/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedMeshRuntimeMeshInstance{
    using PositionVector = Vector<Float3U, Core::Alloc::GlobalArena>;
    using Float2Vector = Vector<Float2U, Core::Alloc::GlobalArena>;
    using Half4Vector = Vector<Half4U, Core::Alloc::GlobalArena>;
    using SkinVector = Vector<SkinInfluence4, Core::Alloc::GlobalArena>;
    using JointVector = Vector<SkinnedMeshJointMatrix, Core::Alloc::GlobalArena>;
    using MeshletVector = Vector<MeshletDesc, Core::Alloc::GlobalArena>;
    using MeshletBoundsVector = Vector<MeshletBounds, Core::Alloc::GlobalArena>;
    using MeshletPositionRefVector = Vector<MeshletDeformedPositionRef, Core::Alloc::GlobalArena>;
    using MeshletAttributeRefVector = Vector<MeshletShadingAttributeRef, Core::Alloc::GlobalArena>;
    using MeshletLocalVertexRefVector = Vector<MeshletLocalVertexRef, Core::Alloc::GlobalArena>;
    using MeshletPrimitiveIndexVector = Vector<u8, Core::Alloc::GlobalArena>;
    using AttributeSkinVector = Vector<u32, Core::Alloc::GlobalArena>;

    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<SkinnedMesh> source;
    u32 meshClass = Core::Mesh::MeshClass::Skinned;
    PositionVector restPositions;
    Half4Vector restNormals;
    Half4Vector restTangents;
    Float2Vector uv0;
    Half4Vector colors;
    MeshletVector meshlets;
    MeshletBoundsVector meshletBounds;
    MeshletPositionRefVector meshletPositionRefs;
    MeshletAttributeRefVector meshletAttributeRefs;
    MeshletLocalVertexRefVector meshletLocalVertexRefs;
    MeshletPrimitiveIndexVector meshletPrimitiveIndices;
    AttributeSkinVector attributeSkins;
    u32 skeletonJointCount = 0;
    SkinVector skin;
    JointVector inverseBindMatrices;
    Core::BufferHandle restPositionBuffer;
    Core::BufferHandle restNormalBuffer;
    Core::BufferHandle restTangentBuffer;
    Core::BufferHandle skinnedPositionBuffer;
    Core::BufferHandle skinnedNormalBuffer;
    Core::BufferHandle skinnedTangentBuffer;
    Core::BufferHandle uv0Buffer;
    Core::BufferHandle colorBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    Core::BufferHandle meshletPositionRefBuffer;
    Core::BufferHandle meshletAttributeRefBuffer;
    Core::BufferHandle meshletLocalVertexRefBuffer;
    Core::BufferHandle meshletPrimitiveIndexBuffer;
    Core::BufferHandle attributeSkinBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    explicit SkinnedMeshRuntimeMeshInstance(Core::Alloc::GlobalArena& arena)
        : restPositions(arena)
        , restNormals(arena)
        , restTangents(arena)
        , uv0(arena)
        , colors(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletPositionRefs(arena)
        , meshletAttributeRefs(arena)
        , meshletLocalVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
        , attributeSkins(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
    {}

    [[nodiscard]] bool valid()const{
        return
            entity.valid()
            && handle.valid()
            && source.valid()
            && Core::Mesh::MeshClassUsesSkinning(meshClass)
            && !restPositions.empty()
            && !restNormals.empty()
            && restTangents.size() == restNormals.size()
            && !uv0.empty()
            && !colors.empty()
            && !skin.empty()
            && !meshlets.empty()
            && meshletBounds.size() == meshlets.size()
            && !meshletPositionRefs.empty()
            && !meshletAttributeRefs.empty()
            && !meshletLocalVertexRefs.empty()
            && !meshletPrimitiveIndices.empty()
            && attributeSkins.size() == meshletAttributeRefs.size()
            && (dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u
            && restPositionBuffer != nullptr
            && restNormalBuffer != nullptr
            && restTangentBuffer != nullptr
            && skinnedPositionBuffer != nullptr
            && skinnedNormalBuffer != nullptr
            && skinnedTangentBuffer != nullptr
            && uv0Buffer != nullptr
            && colorBuffer != nullptr
            && meshletDescBuffer != nullptr
            && meshletBoundsBuffer != nullptr
            && meshletPositionRefBuffer != nullptr
            && meshletAttributeRefBuffer != nullptr
            && meshletLocalVertexRefBuffer != nullptr
            && meshletPrimitiveIndexBuffer != nullptr
            && attributeSkinBuffer != nullptr
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

