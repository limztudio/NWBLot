// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/common.h>
#include <core/geometry/geometry_class.h>
#include <impl/assets_geometry/geometry_payload_types.h>
#include <impl/assets_geometry/skinned_geometry_types.h>
#include <impl/ecs_skinned_geometry/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryRuntimeMeshInstance{
    using PositionVector = Vector<Float3U, Core::Alloc::GlobalArena>;
    using Float2Vector = Vector<Float2U, Core::Alloc::GlobalArena>;
    using Half4Vector = Vector<Half4U, Core::Alloc::GlobalArena>;
    using SkinVector = Vector<SkinInfluence4, Core::Alloc::GlobalArena>;
    using JointVector = Vector<SkinnedGeometryJointMatrix, Core::Alloc::GlobalArena>;
    using VertexRefVector = Vector<GeometryVertexRef, Core::Alloc::GlobalArena>;
    using MeshletVector = Vector<GeometryMeshletDesc, Core::Alloc::GlobalArena>;
    using MeshletBoundsVector = Vector<GeometryMeshletBounds, Core::Alloc::GlobalArena>;
    using MeshletVertexRefVector = Vector<u32, Core::Alloc::GlobalArena>;
    using MeshletPrimitiveIndexVector = Vector<u8, Core::Alloc::GlobalArena>;

    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<SkinnedGeometry> source;
    u32 geometryClass = Core::Geometry::GeometryClass::Skinned;
    PositionVector restPositions;
    Half4Vector restNormals;
    Half4Vector restTangents;
    Float2Vector uv0;
    Half4Vector colors;
    VertexRefVector vertexRefs;
    MeshletVector meshlets;
    MeshletBoundsVector meshletBounds;
    MeshletVertexRefVector meshletVertexRefs;
    MeshletPrimitiveIndexVector meshletPrimitiveIndices;
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
    Core::BufferHandle vertexRefBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    Core::BufferHandle meshletVertexRefBuffer;
    Core::BufferHandle meshletPrimitiveIndexBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    explicit SkinnedGeometryRuntimeMeshInstance(Core::Alloc::GlobalArena& arena)
        : restPositions(arena)
        , restNormals(arena)
        , restTangents(arena)
        , uv0(arena)
        , colors(arena)
        , vertexRefs(arena)
        , meshlets(arena)
        , meshletBounds(arena)
        , meshletVertexRefs(arena)
        , meshletPrimitiveIndices(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
    {}

    [[nodiscard]] bool valid()const{
        return
            entity.valid()
            && handle.valid()
            && source.valid()
            && Core::Geometry::GeometryClassUsesSkinning(geometryClass)
            && !restPositions.empty()
            && restNormals.size() == restPositions.size()
            && restTangents.size() == restPositions.size()
            && !uv0.empty()
            && !colors.empty()
            && !skin.empty()
            && !vertexRefs.empty()
            && !meshlets.empty()
            && meshletBounds.size() == meshlets.size()
            && !meshletVertexRefs.empty()
            && !meshletPrimitiveIndices.empty()
            && (dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u
            && restPositionBuffer != nullptr
            && restNormalBuffer != nullptr
            && restTangentBuffer != nullptr
            && skinnedPositionBuffer != nullptr
            && skinnedNormalBuffer != nullptr
            && skinnedTangentBuffer != nullptr
            && uv0Buffer != nullptr
            && colorBuffer != nullptr
            && vertexRefBuffer != nullptr
            && meshletDescBuffer != nullptr
            && meshletBoundsBuffer != nullptr
            && meshletVertexRefBuffer != nullptr
            && meshletPrimitiveIndexBuffer != nullptr
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

