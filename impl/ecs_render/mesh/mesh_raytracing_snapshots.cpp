// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"
#include "mesh_raytracing_snapshot_policy.h"
#include <impl/ecs_render/mesh/mesh_acceleration_state_policy.adhd.h>

#include <impl/ecs_render/mesh/renderer_mesh_state.h>
#include <impl/assets_mesh/asset.h>
#include <impl/ecs_mesh/runtime/types.adhd.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshSystem::collectRayTracingResourceSnapshots(ECSRenderDetail::MeshRayTracingResourceSnapshotVector& outSnapshots)const{
    outSnapshots.clear();
    outSnapshots.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        auto snapshot = MeshRayTracingSnapshotPolicy::capture(meshIt.value());
        outSnapshots.push_back(Move(snapshot));
    }
}

Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> RendererMeshSystem::findRayTracingResourceSnapshot(const Name& meshName)const noexcept{
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return MakeUnexpected(Failure{});

    return MeshRayTracingSnapshotPolicy::capture(found.value());
}

Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> RendererMeshSystem::findRenderableRayTracingResourceSnapshot(const RenderableMeshDesc& desc)const noexcept{
    if(!desc.valid())
        return MakeUnexpected(Failure{});

    const Name meshName = desc.runtime ? desc.runtimeMesh.meshKey : desc.mesh.name();
    const auto found = m_meshState.m_meshes.find(meshName);
    if(found == m_meshState.m_meshes.end())
        return MakeUnexpected(Failure{});

    const MeshResources& mesh = found.value();
    if(
        (desc.runtime && (
            !mesh.runtimeMesh || mesh.runtimeMeshVersion != desc.runtimeMesh.version
            || mesh.runtimeGeometryContentRevision != desc.runtimeMesh.geometryContentRevision
            || mesh.positionBuffer != desc.runtimeMesh.positionBuffer
            || mesh.triangleIndexBuffer != desc.runtimeMesh.triangleIndexBuffer
            || mesh.attributeBuffer != desc.runtimeMesh.attributeBuffer
            || mesh.runtimeLocalBoundsBuffer != desc.runtimeMesh.localBoundsBuffer
            || mesh.runtimeMeshletLocalBoundsBuffer != desc.runtimeMesh.meshletLocalBoundsBuffer
            || mesh.meshletPrimitiveIndexCount != desc.runtimeMesh.meshletPrimitiveIndexCount
        ))
        || !meshRenderBindingsReady(mesh)
    )
        return MakeUnexpected(Failure{});

    return MeshRayTracingSnapshotPolicy::capture(mesh);
}

b8 RendererMeshSystem::commitRayTracingResourceSnapshot(const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected, ECSRenderDetail::MeshRayTracingResourceSnapshot& desired){
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    const b8 currentMatches = found != m_meshState.m_meshes.end() && MeshRayTracingSnapshotPolicy::matches(found.value(), expected);
    if(!currentMatches || !MeshRayTracingSnapshotPolicy::sameIdentity(expected, desired)){
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        const Core::GpuDescriptorHandle currentNodeHandle = found != m_meshState.m_meshes.end() ? found.value().swBvhNodeHeapHandle : Core::GpuDescriptorHandle::Invalid();
        const Core::GpuDescriptorHandle currentParentHandle = found != m_meshState.m_meshes.end() ? found.value().swBvhParentHeapHandle : Core::GpuDescriptorHandle::Invalid();
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
            desired.swBvhNodeHeapHandle = Core::GpuDescriptorHandle::Invalid();
            if(desired.swBvhParentHeapHandle == candidateNodeHandle)
                desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
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
            desired.swBvhParentHeapHandle = Core::GpuDescriptorHandle::Invalid();
        }
        return false;
    }

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized() && expected.swBvhNodeHeapHandle.valid() && expected.swBvhNodeHeapHandle != desired.swBvhNodeHeapHandle && expected.swBvhNodeHeapHandle != desired.swBvhParentHeapHandle)
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
    const b8 blasReplaced = mesh.blas.get() != desired.blas.get();
    const b8 swBvhReplaced = mesh.swBvhNodeBuffer.get() != desired.swBvhNodeBuffer.get() || mesh.swBvhParentBuffer.get() != desired.swBvhParentBuffer.get();
    mesh.blas = desired.blas;
    mesh.swBvhNodeBuffer = desired.swBvhNodeBuffer;
    mesh.swBvhParentBuffer = desired.swBvhParentBuffer;
    mesh.swBvhNodeHeapHandle = desired.swBvhNodeHeapHandle;
    mesh.swBvhParentHeapHandle = desired.swBvhParentHeapHandle;
    MeshAccelerationStatePolicy().commitSnapshot(
        blasReplaced,
        swBvhReplaced,
        desired.blasRefitsSinceRebuild,
        desired.swBvhRefitsSinceRebuild,
        desired.blasBuildPending,
        desired.blasBackingFresh,
        desired.swBvhBuildPending,
        desired.swBvhTopologyBuilt,
        desired.blasBuildAccepted,
        desired.swBvhBuildAccepted,
        desired.blasGeometryContentRevision,
        desired.swBvhGeometryContentRevision,
        mesh.blasRefitsSinceRebuild,
        mesh.swBvhRefitsSinceRebuild,
        mesh.blasBuildPending,
        mesh.blasBackingFresh,
        mesh.swBvhBuildPending,
        mesh.swBvhTopologyBuilt,
        mesh.blasBuildAccepted,
        mesh.swBvhBuildAccepted,
        mesh.blasGeometryContentRevision,
        mesh.swBvhGeometryContentRevision
    );
    return true;
}

Expected<ECSRenderDetail::MeshRayTracingResourceSnapshot> RendererMeshSystem::ensureRayTracingInputHeapHandles(const ECSRenderDetail::MeshRayTracingResourceSnapshot& expected){
    const auto found = m_meshState.m_meshes.find(expected.meshName);
    if(found == m_meshState.m_meshes.end() || !MeshRayTracingSnapshotPolicy::matches(found.value(), expected) || !ensureMeshSwBvhInputHeapHandles(found.value()))
        return MakeUnexpected(Failure{});

    return MeshRayTracingSnapshotPolicy::capture(found.value());
}

void RendererMeshSystem::discardRayTracingBuildState()noexcept{
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        MeshResources& mesh = meshIt.value();
        MeshAccelerationStatePolicy().resetBuild(
            mesh.blas != nullptr,
            mesh.swBvhNodeBuffer != nullptr || mesh.swBvhParentBuffer != nullptr,
            mesh.blasBuildPending,
            mesh.blasBuildAccepted,
            mesh.blasGeometryContentRevision,
            mesh.blasRefitsSinceRebuild,
            mesh.swBvhBuildPending,
            mesh.swBvhTopologyBuilt,
            mesh.swBvhBuildAccepted,
            mesh.swBvhGeometryContentRevision,
            mesh.swBvhRefitsSinceRebuild
        );
    }
}

Expected<ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector> RendererMeshSystem::collectSoftwareBvhParentBuildStates(Core::Alloc::ScratchArena& arena)const{
    ECSRenderDetail::MeshSoftwareBvhParentBuildStateVector states(arena);
    states.reserve(m_meshState.m_meshes.size());
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        if(!mesh.swBvhNodeBuffer && !mesh.swBvhParentBuffer)
            continue;
        if(!mesh.swBvhNodeBuffer || !mesh.swBvhParentBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererMeshSystem: incomplete software BVH state for a live mesh"));
            return MakeUnexpected(Failure{});
        }
        const auto identity = DeriveName(mesh.meshName, AStringView(":shadow_trace_sw_parent"));
        if(!identity)
            return MakeUnexpected(Failure{});
        states.push_back({ .buffer = mesh.swBvhParentBuffer, .identity = *identity, });
    }
    return states;
}

void RendererMeshSystem::collectRetainedAccelerationStateBuffers(ECSRenderDetail::MeshRetainedAccelerationStateBufferVector& outBuffers)const{
    outBuffers.clear();
    outBuffers.reserve(m_meshState.m_meshes.size() * s_RetainedAccelerationBuffersPerMesh);
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        const MeshResources& mesh = meshIt.value();
        // Retain every live BLAS source and backing buffer across preparation packets.
        if(mesh.blas){
            outBuffers.push_back(mesh.positionBuffer);
            outBuffers.push_back(mesh.triangleIndexBuffer);
            outBuffers.push_back(mesh.blas->getBackingBufferHandle());
        }
        // Preserve mesh-local software buffers across tracing-backend changes.
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
        // This predicate consumes only build/revision state; the graph output below retains the BLAS.
        const b8 nativeBuildsBlas = MeshAccelerationStatePolicy().requiresBlasUpdate(mesh.blasBuildPending, mesh.blasBuildAccepted, mesh.runtimeMesh, mesh.runtimeGeometryContentRevision, mesh.blasGeometryContentRevision);
        outStates.push_back({ .meshName = mesh.meshName, .blas = mesh.blas, .backingFresh = mesh.blasBackingFresh, .nativeBuildsBlas = nativeBuildsBlas, });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

