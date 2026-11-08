// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_raytracing_snapshot_policy.h"
#include "mesh_system.h"
#include "renderer_mesh_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


[[nodiscard]] b8 MeshRayTracingSnapshotPolicy::boundsReady(const MeshResources& mesh)noexcept{
    const Core::GpuDescriptorHandle descriptor = mesh.geometryHeapHandles[NWB_MESH_BINDING_MESHLET_DESC];
    const Core::GpuDescriptorHandle bounds = mesh.runtimeMeshletLocalBoundsHeapHandle;
    if(
        !mesh.runtimeMesh || mesh.runtimeGeometryContentRevision == 0u || mesh.meshletCount == 0u
        || !mesh.meshletDescBuffer || !mesh.runtimeMeshletLocalBoundsBuffer
        || !descriptor.valid() || descriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
        || !bounds.valid() || bounds.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer
    )
        return false;
    return true;
}

[[nodiscard]] b8 MeshRayTracingSnapshotPolicy::meshletMatches(const MeshResources& mesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot)noexcept{
    if(!boundsReady(mesh))
        return snapshot.meshletCount == 0u && !snapshot.meshletDescBuffer && !snapshot.meshletLocalBoundsBuffer && !snapshot.meshletDescHeapHandle.valid() && !snapshot.meshletLocalBoundsHeapHandle.valid();
    return
        snapshot.meshletCount == mesh.meshletCount
        && snapshot.meshletDescBuffer == mesh.meshletDescBuffer
        && snapshot.meshletLocalBoundsBuffer == mesh.runtimeMeshletLocalBoundsBuffer
        && snapshot.meshletDescHeapHandle == mesh.geometryHeapHandles[NWB_MESH_BINDING_MESHLET_DESC]
        && snapshot.meshletLocalBoundsHeapHandle == mesh.runtimeMeshletLocalBoundsHeapHandle
    ;
}

ECSRenderDetail::MeshRayTracingResourceSnapshot MeshRayTracingSnapshotPolicy::capture(const MeshResources& mesh)noexcept{
    ECSRenderDetail::MeshRayTracingResourceSnapshot snapshot = {
        .meshName = mesh.meshName,
        .runtimeMeshVersion = mesh.runtimeMeshVersion,
        .runtimeGeometryContentRevision = mesh.runtimeGeometryContentRevision,
        .blasGeometryContentRevision = mesh.blasGeometryContentRevision,
        .swBvhGeometryContentRevision = mesh.swBvhGeometryContentRevision,
        .positionBuffer = mesh.positionBuffer,
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .attributeBuffer = mesh.attributeBuffer,
        .solidTriangleWords = Span<const u32>(mesh.solidTriangleWords.get(), ExpectedMeshletSolidTriangleWordCount(mesh.meshletPrimitiveIndexCount)),
        .runtimeLocalBoundsBuffer = mesh.runtimeLocalBoundsBuffer,
        .meshletDescBuffer = {},
        .meshletLocalBoundsBuffer = {},
        .blas = mesh.blas,
        .swBvhNodeBuffer = mesh.swBvhNodeBuffer,
        .swBvhParentBuffer = mesh.swBvhParentBuffer,
        .csgLocalBounds = mesh.csgLocalBounds,
        .runtimeLocalBoundsHeapHandle = mesh.runtimeLocalBoundsHeapHandle,
        .meshletDescHeapHandle = Core::GpuDescriptorHandle::Invalid(),
        .meshletLocalBoundsHeapHandle = Core::GpuDescriptorHandle::Invalid(),
        .swBvhPositionHeapHandle = mesh.swBvhPositionHeapHandle,
        .swBvhTriangleIndexHeapHandle = mesh.swBvhTriangleIndexHeapHandle,
        .swBvhNodeHeapHandle = mesh.swBvhNodeHeapHandle,
        .swBvhParentHeapHandle = mesh.swBvhParentHeapHandle,
        .meshletCount = 0u,
        .meshletPrimitiveIndexCount = mesh.meshletPrimitiveIndexCount,
        .blasRefitsSinceRebuild = mesh.blasRefitsSinceRebuild,
        .swBvhRefitsSinceRebuild = mesh.swBvhRefitsSinceRebuild,
        .runtimeMesh = mesh.runtimeMesh,
        .blasBuildPending = mesh.blasBuildPending,
        .blasBackingFresh = mesh.blasBackingFresh,
        .swBvhBuildPending = mesh.swBvhBuildPending,
        .swBvhTopologyBuilt = mesh.swBvhTopologyBuilt,
        .blasBuildAccepted = mesh.blasBuildAccepted,
        .swBvhBuildAccepted = mesh.swBvhBuildAccepted,
    };
    if(boundsReady(mesh)){
        snapshot.meshletCount = mesh.meshletCount;
        snapshot.meshletDescBuffer = mesh.meshletDescBuffer;
        snapshot.meshletLocalBoundsBuffer = mesh.runtimeMeshletLocalBoundsBuffer;
        snapshot.meshletDescHeapHandle = mesh.geometryHeapHandles[NWB_MESH_BINDING_MESHLET_DESC];
        snapshot.meshletLocalBoundsHeapHandle = mesh.runtimeMeshletLocalBoundsHeapHandle;
    }
    return snapshot;
}

[[nodiscard]] b8 MeshRayTracingSnapshotPolicy::matches(const MeshResources& mesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot)noexcept{
    return
        mesh.meshName == snapshot.meshName
        && mesh.positionBuffer.get() == snapshot.positionBuffer.get()
        && mesh.triangleIndexBuffer.get() == snapshot.triangleIndexBuffer.get()
        && mesh.attributeBuffer.get() == snapshot.attributeBuffer.get()
        && mesh.solidTriangleWords.get() == snapshot.solidTriangleWords.data()
        && mesh.runtimeLocalBoundsBuffer == snapshot.runtimeLocalBoundsBuffer
        && mesh.runtimeLocalBoundsHeapHandle == snapshot.runtimeLocalBoundsHeapHandle
        && meshletMatches(mesh, snapshot)
        && mesh.blas.get() == snapshot.blas.get()
        && mesh.swBvhPositionHeapHandle == snapshot.swBvhPositionHeapHandle
        && mesh.swBvhTriangleIndexHeapHandle == snapshot.swBvhTriangleIndexHeapHandle
        && mesh.swBvhNodeBuffer.get() == snapshot.swBvhNodeBuffer.get()
        && mesh.swBvhParentBuffer.get() == snapshot.swBvhParentBuffer.get()
        && mesh.swBvhNodeHeapHandle == snapshot.swBvhNodeHeapHandle
        && mesh.swBvhParentHeapHandle == snapshot.swBvhParentHeapHandle
        && mesh.meshletPrimitiveIndexCount == snapshot.meshletPrimitiveIndexCount
        && mesh.blasRefitsSinceRebuild == snapshot.blasRefitsSinceRebuild
        && mesh.swBvhRefitsSinceRebuild == snapshot.swBvhRefitsSinceRebuild
        && mesh.runtimeMesh == snapshot.runtimeMesh
        && mesh.blasBuildPending == snapshot.blasBuildPending
        && mesh.blasBackingFresh == snapshot.blasBackingFresh
        && mesh.swBvhBuildPending == snapshot.swBvhBuildPending
        && mesh.swBvhTopologyBuilt == snapshot.swBvhTopologyBuilt
        && mesh.runtimeMeshVersion == snapshot.runtimeMeshVersion
        && mesh.runtimeGeometryContentRevision == snapshot.runtimeGeometryContentRevision
        && mesh.blasGeometryContentRevision == snapshot.blasGeometryContentRevision
        && mesh.swBvhGeometryContentRevision == snapshot.swBvhGeometryContentRevision
        && mesh.blasBuildAccepted == snapshot.blasBuildAccepted
        && mesh.swBvhBuildAccepted == snapshot.swBvhBuildAccepted
        && mesh.csgLocalBounds.minBounds == snapshot.csgLocalBounds.minBounds
        && mesh.csgLocalBounds.maxBounds == snapshot.csgLocalBounds.maxBounds
    ;
}

[[nodiscard]] b8 MeshRayTracingSnapshotPolicy::sameIdentity(const ECSRenderDetail::MeshRayTracingResourceSnapshot& lhs, const ECSRenderDetail::MeshRayTracingResourceSnapshot& rhs)noexcept{
    return
        lhs.meshName == rhs.meshName
        && lhs.positionBuffer.get() == rhs.positionBuffer.get()
        && lhs.triangleIndexBuffer.get() == rhs.triangleIndexBuffer.get()
        && lhs.attributeBuffer.get() == rhs.attributeBuffer.get()
        && lhs.solidTriangleWords.data() == rhs.solidTriangleWords.data()
        && lhs.solidTriangleWords.size() == rhs.solidTriangleWords.size()
        && lhs.runtimeLocalBoundsBuffer == rhs.runtimeLocalBoundsBuffer
        && lhs.runtimeLocalBoundsHeapHandle == rhs.runtimeLocalBoundsHeapHandle
        && lhs.meshletCount == rhs.meshletCount
        && lhs.meshletDescBuffer == rhs.meshletDescBuffer
        && lhs.meshletLocalBoundsBuffer == rhs.meshletLocalBoundsBuffer
        && lhs.meshletDescHeapHandle == rhs.meshletDescHeapHandle
        && lhs.meshletLocalBoundsHeapHandle == rhs.meshletLocalBoundsHeapHandle
        && lhs.swBvhPositionHeapHandle == rhs.swBvhPositionHeapHandle
        && lhs.swBvhTriangleIndexHeapHandle == rhs.swBvhTriangleIndexHeapHandle
        && lhs.meshletPrimitiveIndexCount == rhs.meshletPrimitiveIndexCount
        && lhs.runtimeMesh == rhs.runtimeMesh
        && lhs.runtimeMeshVersion == rhs.runtimeMeshVersion
        && lhs.runtimeGeometryContentRevision == rhs.runtimeGeometryContentRevision
        && lhs.csgLocalBounds.minBounds == rhs.csgLocalBounds.minBounds
        && lhs.csgLocalBounds.maxBounds == rhs.csgLocalBounds.maxBounds
    ;
}


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
