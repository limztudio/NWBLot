// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/common.h>
#include <core/geometry/geometry_class.h>
#include <impl/assets_geometry/skinned_geometry_types.h>
#include <impl/ecs_skinned_geometry/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryRuntimeMeshInstance{
    using VertexVector = Vector<SkinnedGeometryVertex, Core::Alloc::GlobalArena>;
    using IndexVector = Vector<u32, Core::Alloc::GlobalArena>;
    using SkinVector = Vector<SkinInfluence4, Core::Alloc::GlobalArena>;
    using JointVector = Vector<SkinnedGeometryJointMatrix, Core::Alloc::GlobalArena>;

    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<SkinnedGeometry> source;
    u32 geometryClass = Core::Geometry::GeometryClass::Skinned;
    VertexVector restVertices;
    IndexVector indices;
    u32 skeletonJointCount = 0;
    SkinVector skin;
    JointVector inverseBindMatrices;
    Core::BufferHandle restVertexBuffer;
    Core::BufferHandle indexBuffer;
    Core::BufferHandle skinnedVertexBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    explicit SkinnedGeometryRuntimeMeshInstance(Core::Alloc::GlobalArena& arena)
        : restVertices(arena)
        , indices(arena)
        , skin(arena)
        , inverseBindMatrices(arena)
    {}

    [[nodiscard]] bool valid()const{
        return
            entity.valid()
            && handle.valid()
            && source.valid()
            && Core::Geometry::GeometryClassUsesSkinning(geometryClass)
            && !restVertices.empty()
            && !indices.empty()
            && (dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u
            && restVertexBuffer != nullptr
            && indexBuffer != nullptr
            && skinnedVertexBuffer != nullptr
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

