// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include "runtime_mesh_pruning.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/assets_mesh/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_raytracing_snapshots{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CaptureRayTracingResourceSnapshot(
    const MeshResources& mesh,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
){
    outSnapshot = {
        .meshName = mesh.meshName,
        .positionBuffer = mesh.positionBuffer,
        .triangleIndexBuffer = mesh.triangleIndexBuffer,
        .attributeBuffer = mesh.attributeBuffer,
        .blas = mesh.blas,
        .swBvhPositionHeapHandle = mesh.swBvhPositionHeapHandle,
        .swBvhTriangleIndexHeapHandle = mesh.swBvhTriangleIndexHeapHandle,
        .swBvhNodeBuffer = mesh.swBvhNodeBuffer,
        .swBvhParentBuffer = mesh.swBvhParentBuffer,
        .swBvhNodeHeapHandle = mesh.swBvhNodeHeapHandle,
        .swBvhParentHeapHandle = mesh.swBvhParentHeapHandle,
        .meshletPrimitiveIndexCount = mesh.meshletPrimitiveIndexCount,
        .blasRefitsSinceRebuild = mesh.blasRefitsSinceRebuild,
        .swBvhRefitsSinceRebuild = mesh.swBvhRefitsSinceRebuild,
        .runtimeMesh = mesh.runtimeMesh,
        .blasBuildPending = mesh.blasBuildPending,
        .blasBackingFresh = mesh.blasBackingFresh,
        .blasBackingStateHandoffPending = mesh.blasBackingStateHandoffPending,
        .swBvhBuildPending = mesh.swBvhBuildPending,
        .swBvhTopologyBuilt = mesh.swBvhTopologyBuilt,
        .runtimeMeshVersion = mesh.runtimeMeshVersion,
        .csgLocalBounds = mesh.csgLocalBounds,
    };
}

[[nodiscard]] bool RayTracingResourceSnapshotMatches(
    const MeshResources& mesh,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot
){
    return
        mesh.meshName == snapshot.meshName
        && mesh.positionBuffer.get() == snapshot.positionBuffer.get()
        && mesh.triangleIndexBuffer.get() == snapshot.triangleIndexBuffer.get()
        && mesh.attributeBuffer.get() == snapshot.attributeBuffer.get()
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
        && mesh.blasBackingStateHandoffPending == snapshot.blasBackingStateHandoffPending
        && mesh.swBvhBuildPending == snapshot.swBvhBuildPending
        && mesh.swBvhTopologyBuilt == snapshot.swBvhTopologyBuilt
        && mesh.runtimeMeshVersion == snapshot.runtimeMeshVersion
        && mesh.csgLocalBounds.minBounds == snapshot.csgLocalBounds.minBounds
        && mesh.csgLocalBounds.maxBounds == snapshot.csgLocalBounds.maxBounds
    ;
}

[[nodiscard]] bool RayTracingResourceIdentityMatches(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& lhs,
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& rhs
){
    return
        lhs.meshName == rhs.meshName
        && lhs.positionBuffer.get() == rhs.positionBuffer.get()
        && lhs.triangleIndexBuffer.get() == rhs.triangleIndexBuffer.get()
        && lhs.attributeBuffer.get() == rhs.attributeBuffer.get()
        && lhs.swBvhPositionHeapHandle == rhs.swBvhPositionHeapHandle
        && lhs.swBvhTriangleIndexHeapHandle == rhs.swBvhTriangleIndexHeapHandle
        && lhs.meshletPrimitiveIndexCount == rhs.meshletPrimitiveIndexCount
        && lhs.runtimeMesh == rhs.runtimeMesh
        && lhs.runtimeMeshVersion == rhs.runtimeMeshVersion
        && lhs.csgLocalBounds.minBounds == rhs.csgLocalBounds.minBounds
        && lhs.csgLocalBounds.maxBounds == rhs.csgLocalBounds.maxBounds
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshSystem::collectRayTracingResourceSnapshots(
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector& outSnapshots
)const{
    outSnapshots.clear();
    outSnapshots.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        ECSRenderDetail::MeshRayTracingResourceSnapshot snapshot;
        __hidden_mesh_raytracing_snapshots::CaptureRayTracingResourceSnapshot(meshIt.value(), snapshot);
        outSnapshots.push_back(Move(snapshot));
    }
}

bool RendererMeshSystem::findRayTracingResourceSnapshot(
    const Name& meshName,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
)const{
    outSnapshot = {};
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return false;

    __hidden_mesh_raytracing_snapshots::CaptureRayTracingResourceSnapshot(found.value(), outSnapshot);
    return true;
}

bool RendererMeshSystem::findRenderableRayTracingResourceSnapshot(
    const RenderableMeshDesc& desc,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
)const{
    outSnapshot = {};
    if(!desc.valid())
        return false;

    const Name meshName = desc.runtime ? desc.runtimeMesh.meshKey : desc.mesh.name();
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return false;

    const MeshResources& mesh = found.value();
    if(
        (desc.runtime && (!mesh.runtimeMesh || mesh.runtimeMeshVersion != desc.runtimeMesh.version))
        || !meshRenderBindingsReady(mesh)
    )
        return false;

    __hidden_mesh_raytracing_snapshots::CaptureRayTracingResourceSnapshot(mesh, outSnapshot);
    return true;
}

bool RendererMeshSystem::commitRayTracingResourceSnapshot(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& desired
){
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    const bool currentMatches = found != m_meshState.m_meshes.end()
        && __hidden_mesh_raytracing_snapshots::RayTracingResourceSnapshotMatches(found.value(), expected)
    ;
    if(!currentMatches || !__hidden_mesh_raytracing_snapshots::RayTracingResourceIdentityMatches(expected, desired)){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::GpuDescriptorHandle currentNodeHandle = found != m_meshState.m_meshes.end()
            ? found.value().swBvhNodeHeapHandle
            : Core::GpuDescriptorHandle::invalid()
        ;
        const Core::GpuDescriptorHandle currentParentHandle = found != m_meshState.m_meshes.end()
            ? found.value().swBvhParentHeapHandle
            : Core::GpuDescriptorHandle::invalid()
        ;
        const Core::GpuDescriptorHandle candidateNodeHandle = desired.swBvhNodeHeapHandle;
        if(
            heap.isInitialized()
            && candidateNodeHandle.valid()
            && candidateNodeHandle != expected.swBvhNodeHeapHandle
            && candidateNodeHandle != expected.swBvhParentHeapHandle
            && candidateNodeHandle != currentNodeHandle
            && candidateNodeHandle != currentParentHandle
        ){
            heap.free(candidateNodeHandle);
            desired.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
            if(desired.swBvhParentHeapHandle == candidateNodeHandle)
                desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        const Core::GpuDescriptorHandle candidateParentHandle = desired.swBvhParentHeapHandle;
        if(
            heap.isInitialized()
            && candidateParentHandle.valid()
            && candidateParentHandle != expected.swBvhNodeHeapHandle
            && candidateParentHandle != expected.swBvhParentHeapHandle
            && candidateParentHandle != currentNodeHandle
            && candidateParentHandle != currentParentHandle
        ){
            heap.free(candidateParentHandle);
            desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::invalid();
        }
        return false;
    }

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(
        heap.isInitialized()
        && expected.swBvhNodeHeapHandle.valid()
        && expected.swBvhNodeHeapHandle != desired.swBvhNodeHeapHandle
        && expected.swBvhNodeHeapHandle != desired.swBvhParentHeapHandle
    )
        heap.free(expected.swBvhNodeHeapHandle);
    if(
        heap.isInitialized()
        && expected.swBvhParentHeapHandle.valid()
        && expected.swBvhParentHeapHandle != expected.swBvhNodeHeapHandle
        && expected.swBvhParentHeapHandle != desired.swBvhNodeHeapHandle
        && expected.swBvhParentHeapHandle != desired.swBvhParentHeapHandle
    )
        heap.free(expected.swBvhParentHeapHandle);

    MeshResources& mesh = found.value();
    mesh.blas = desired.blas;
    mesh.swBvhNodeBuffer = desired.swBvhNodeBuffer;
    mesh.swBvhParentBuffer = desired.swBvhParentBuffer;
    mesh.swBvhNodeHeapHandle = desired.swBvhNodeHeapHandle;
    mesh.swBvhParentHeapHandle = desired.swBvhParentHeapHandle;
    mesh.blasRefitsSinceRebuild = desired.blasRefitsSinceRebuild;
    mesh.swBvhRefitsSinceRebuild = desired.swBvhRefitsSinceRebuild;
    mesh.blasBuildPending = desired.blasBuildPending;
    mesh.blasBackingFresh = desired.blasBackingFresh;
    mesh.blasBackingStateHandoffPending = desired.blasBackingStateHandoffPending;
    mesh.swBvhBuildPending = desired.swBvhBuildPending;
    mesh.swBvhTopologyBuilt = desired.swBvhTopologyBuilt;
    return true;
}

bool RendererMeshSystem::ensureRayTracingInputHeapHandles(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected,
    ECSRenderDetail::MeshRayTracingResourceSnapshot& outSnapshot
){
    outSnapshot = {};
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    if(
        found == m_meshState.m_meshes.end()
        || !__hidden_mesh_raytracing_snapshots::RayTracingResourceSnapshotMatches(found.value(), expected)
        || !ensureMeshSwBvhInputHeapHandles(found.value())
    )
        return false;

    __hidden_mesh_raytracing_snapshots::CaptureRayTracingResourceSnapshot(found.value(), outSnapshot);
    return true;
}

bool RendererMeshSystem::collectSoftwareBvhParentBuildStates(ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector& outStates)const{
    outStates.clear();
    outStates.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        if(!mesh.swBvhNodeBuffer && !mesh.swBvhParentBuffer)
            continue;
        if(!mesh.swBvhNodeBuffer || !mesh.swBvhParentBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererMeshSystem: incomplete software BVH state for a live mesh"));
            return false;
        }
        outStates.push_back({
            .buffer = mesh.swBvhParentBuffer,
            .identity = DeriveName(mesh.meshName, AStringView(":shadow_trace_sw_parent")),
        });
    }
    return true;
}

void RendererMeshSystem::collectRetainedAccelerationStateBuffers(ECSRenderDetail::MeshRetainedAccelerationStateBufferVector& outBuffers)const{
    outBuffers.clear();
    outBuffers.reserve(m_meshState.m_meshes.size() * 5u);
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // Frozen BLAS may fall back to native builds; retain every live source and backing buffer.
        if(mesh.blas){
            outBuffers.push_back(mesh.positionBuffer);
            outBuffers.push_back(mesh.triangleIndexBuffer);
            outBuffers.push_back(mesh.blas->getBackingBufferHandle());
        }
        // Preserve mesh-local SW buffers across route changes for hybrid-to-software switches.
        outBuffers.push_back(mesh.swBvhNodeBuffer);
        outBuffers.push_back(mesh.swBvhParentBuffer);
    }
}

void RendererMeshSystem::collectBlasGraphStates(ECSRenderDetail::MeshBlasGraphStateVector& outStates)const{
    outStates.clear();
    outStates.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        if(!mesh.blas)
            continue;
        outStates.push_back({
            .meshName = mesh.meshName,
            .blas = mesh.blas,
            .backingFresh = mesh.blasBackingFresh,
            .nativeBuildsBlas = mesh.runtimeMesh || mesh.blasBuildPending,
        });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

