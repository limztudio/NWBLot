// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/graphics/common.h>
#include <impl/ecs_skinned_geometry/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SkinnedGeometryRuntimeMeshInstance{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<SkinnedGeometry> source;
    u32 geometryClass = GeometryClass::Skinned;
    Vector<SkinnedGeometryVertex> restVertices;
    Vector<u32> indices;
    u32 skeletonJointCount = 0;
    Vector<SkinInfluence4> skin;
    Vector<SkinnedGeometryJointMatrix> inverseBindMatrices;
    Core::BufferHandle restVertexBuffer;
    Core::BufferHandle indexBuffer;
    Core::BufferHandle skinnedVertexBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    [[nodiscard]] bool valid()const{
        return
            entity.valid()
            && handle.valid()
            && source.valid()
            && GeometryClassUsesSkinnedGeometryRuntime(geometryClass)
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

