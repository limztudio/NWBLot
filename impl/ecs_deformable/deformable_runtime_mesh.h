// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/graphics/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableRuntimeMeshInstance{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<DeformableGeometry> source;
    u32 geometryClass = GeometryClass::StaticDeform;
    Vector<DeformableVertexRest> restVertices;
    Vector<u32> indices;
    u32 sourceTriangleCount = 0;
    u32 skeletonJointCount = 0;
    Vector<SkinInfluence4> skin;
    Vector<DeformableJointMatrix> inverseBindMatrices;
    Vector<SourceSample> sourceSamples;
    Vector<DeformableEditMaskFlags> editMaskPerTriangle;
    DeformableDisplacement displacement;
    Vector<DeformableMorph> morphs;
    Core::BufferHandle restVertexBuffer;
    Core::BufferHandle indexBuffer;
    Core::BufferHandle deformedVertexBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    [[nodiscard]] bool valid()const{
        return
            entity.valid()
            && handle.valid()
            && source.valid()
            && GeometryClassUsesDeformableRuntime(geometryClass)
            && !restVertices.empty()
            && !indices.empty()
            && (sourceSamples.empty() || sourceTriangleCount != 0u)
            && (dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u
            && restVertexBuffer != nullptr
            && indexBuffer != nullptr
            && deformedVertexBuffer != nullptr
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class IDeformableRuntimeMeshProvider{
public:
    virtual ~IDeformableRuntimeMeshProvider() = default;


public:
    [[nodiscard]] virtual RuntimeMeshHandle deformableRuntimeMeshHandle(Core::ECS::EntityID entity)const = 0;
    [[nodiscard]] virtual u32 deformableRuntimeMeshEditRevision(RuntimeMeshHandle handle)const = 0;
    [[nodiscard]] virtual bool bumpDeformableRuntimeMeshRevision(
        RuntimeMeshHandle handle,
        RuntimeMeshDirtyFlags dirtyFlags
    ) = 0;
    [[nodiscard]] virtual DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(RuntimeMeshHandle handle) = 0;
    [[nodiscard]] virtual const DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(RuntimeMeshHandle handle)const = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

