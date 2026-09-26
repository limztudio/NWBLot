// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/mesh_acceleration_update.h>
#include <impl/ecs_csg/components.h>
#include <global/algorithm.h>
#include <global/hash_utils.h>
#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::buildPendingMeshSwBvh(
    Core::CommandList& commandList,
    Core::Alloc::ScratchArena& scratchArena
){
    // Record only prepared SW-BVH work; callers decide when software tracing is needed.
    bool allBuildsReady = true;
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = meshResources;

        if(!RequiresMeshSwBvhUpdate(meshResources))
            continue;
        if(!updateMeshSwBvh(commandList, meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh '{}' software BVH build failed"), StringConvert(meshResources.meshName.c_str()));
            allBuildsReady = false;
            continue;
        }
        meshResources.swBvhBuildPending = false;
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, meshResources)){
            m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
            allBuildsReady = false;
        }
    }
    return allBuildsReady;
}

bool RendererRayTracingSystem::preparePendingMeshSwBvhResources(Core::Alloc::ScratchArena& scratchArena){
    // Prepare storage only for geometry requiring an acceleration update.
    bool allResourcesReady = true;
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        if(!RequiresMeshSwBvhUpdate(meshResources))
            continue;

        if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer){
            allResourcesReady = false;
            continue;
        }
        if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % s_RayTracingTriangleIndexCount) != 0u){
            allResourcesReady = false;
            continue;
        }

        const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
        ECSRenderDetail::MeshRayTracingResourceSnapshot boundResources;
        if(!m_meshSystem.ensureRayTracingInputHeapHandles(meshResources, boundResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH input heap registration failed for mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            allResourcesReady = false;
            continue;
        }
        ECSRenderDetail::MeshRayTracingResourceSnapshot preparedResources = boundResources;
        if(!ensureMeshSwBvhResources(
            primitiveCount,
            preparedResources.swBvhNodeBuffer,
            preparedResources.swBvhParentBuffer,
            preparedResources.swBvhNodeHeapHandle,
            preparedResources.swBvhParentHeapHandle
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preparation failed for mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            allResourcesReady = false;
        }
        else if(!m_meshSystem.commitRayTracingResourceSnapshot(boundResources, preparedResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preflight lost mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}

void RendererRayTracingSystem::clearPreparedMeshSwBvhBuilds()noexcept{
    m_preparedMeshSwBvhBuilds.clear();
    m_preparedMeshSwBvhBuildsReady = false;
    m_preparedMeshSwBvhBuildPlanFrozen = false;
}

bool RendererRayTracingSystem::capturePreparedMeshSwBvhBuilds(Core::Alloc::ScratchArena& scratchArena){
    clearPreparedMeshSwBvhBuilds();
    auto& state = m_rayTracingState;
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    bool hasCandidate = false;
    for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
        if(RequiresMeshSwBvhUpdate(mesh)){
            hasCandidate = true;
            break;
        }
    }
    // An unchanged scene can have no mesh work or allocated scratch; freeze an authoritative empty plan.
    if(!hasCandidate){
        m_preparedMeshSwBvhBuildPlanFrozen = true;
        return true;
    }
    const auto sharedResourcesReady = [&]{
        return
            state.m_bvhSortKeysBuffer
            && state.m_bvhSortPayloadBuffer
            && state.m_bvhVisitCounterBuffer
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhSortKeysHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhSortPayloadHeapHandle)
            && __hidden_rt_swbvh::IsStorageBufferHeapHandle(state.m_bvhVisitCounterHeapHandle)
        ;
    };
    if(!sharedResourcesReady())
        return false;

    for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh : meshes){
        // Match the direct update policy, including off-screen mesh resources.
        if(!RequiresMeshSwBvhUpdate(mesh))
            continue;
        if(
            !mesh.meshName
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || mesh.meshletPrimitiveIndexCount == 0u
            || (mesh.meshletPrimitiveIndexCount % s_RayTracingTriangleIndexCount) != 0u
            || !meshSwBvhResourcesReady(
                mesh.swBvhNodeBuffer,
                mesh.swBvhParentBuffer,
                mesh.swBvhNodeHeapHandle,
                mesh.swBvhParentHeapHandle
            )
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh.swBvhPositionHeapHandle)
            || !__hidden_rt_swbvh::IsStorageBufferHeapHandle(mesh.swBvhTriangleIndexHeapHandle)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software BVH build for mesh '{}'"), StringConvert(mesh.meshName.c_str()));
            clearPreparedMeshSwBvhBuilds();
            return false;
        }

        const u32 primitiveCount = mesh.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
        const bool firstBuild = !mesh.swBvhTopologyBuilt || !mesh.swBvhBuildAccepted;
        const bool performRefit =
            mesh.runtimeMesh
            && !firstBuild
            && mesh.swBvhRefitsSinceRebuild < adaptiveRefitsBeforeRebuild(primitiveCount)
        ;
        const Core::BufferDesc& positionDesc = mesh.positionBuffer->getCreationDescription();
        const Core::BufferDesc& indexDesc = mesh.triangleIndexBuffer->getCreationDescription();
        const Core::BufferDesc& nodeDesc = mesh.swBvhNodeBuffer->getCreationDescription();
        const Core::BufferDesc& parentDesc = mesh.swBvhParentBuffer->getCreationDescription();
        const Core::BufferDesc& keysDesc = state.m_bvhSortKeysBuffer->getCreationDescription();
        const Core::BufferDesc& payloadDesc = state.m_bvhSortPayloadBuffer->getCreationDescription();
        const Core::BufferDesc& counterDesc = state.m_bvhVisitCounterBuffer->getCreationDescription();
        m_preparedMeshSwBvhBuilds.push_back(PreparedMeshSwBvhBuild{
            .meshName = mesh.meshName,
            .positionBuffer = mesh.positionBuffer,
            .triangleIndexBuffer = mesh.triangleIndexBuffer,
            .nodeBuffer = mesh.swBvhNodeBuffer,
            .parentBuffer = mesh.swBvhParentBuffer,
            .sortKeysBuffer = state.m_bvhSortKeysBuffer,
            .sortPayloadBuffer = state.m_bvhSortPayloadBuffer,
            .visitCounterBuffer = state.m_bvhVisitCounterBuffer,
            .positionHeapHandle = mesh.swBvhPositionHeapHandle,
            .triangleIndexHeapHandle = mesh.swBvhTriangleIndexHeapHandle,
            .nodeHeapHandle = mesh.swBvhNodeHeapHandle,
            .parentHeapHandle = mesh.swBvhParentHeapHandle,
            .sortKeysHeapHandle = state.m_bvhSortKeysHeapHandle,
            .sortPayloadHeapHandle = state.m_bvhSortPayloadHeapHandle,
            .visitCounterHeapHandle = state.m_bvhVisitCounterHeapHandle,
            .aabbMin = mesh.csgLocalBounds.minBounds,
            .aabbMax = mesh.csgLocalBounds.maxBounds,
            .runtimeMeshVersion = mesh.runtimeMeshVersion,
            .geometryContentRevision = mesh.runtimeGeometryContentRevision,
            .acceptedGeometryContentRevision = mesh.swBvhGeometryContentRevision,
            .positionByteSize = positionDesc.byteSize,
            .indexByteSize = indexDesc.byteSize,
            .nodeByteSize = nodeDesc.byteSize,
            .parentByteSize = parentDesc.byteSize,
            .sortKeysByteSize = keysDesc.byteSize,
            .sortPayloadByteSize = payloadDesc.byteSize,
            .visitCounterByteSize = counterDesc.byteSize,
            .primitiveCount = primitiveCount,
            .refitsBeforeBuild = mesh.swBvhRefitsSinceRebuild,
            .refitsAfterBuild = performRefit ? (mesh.swBvhRefitsSinceRebuild + 1u) : 0u,
            .runtimeMesh = mesh.runtimeMesh,
            .buildPending = mesh.swBvhBuildPending,
            .firstBuild = firstBuild,
            .performRefit = performRefit,
        });
    }
    m_preparedMeshSwBvhBuildsReady = !m_preparedMeshSwBvhBuilds.empty();
    m_preparedMeshSwBvhBuildPlanFrozen = true;
    return true;
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildMatchesCurrent(const PreparedMeshSwBvhBuild& build){
    ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
    if(!m_meshSystem.findRayTracingResourceSnapshot(build.meshName, mesh))
        return false;

    const auto& state = m_rayTracingState;
    if(
        mesh.meshName != build.meshName
        || mesh.runtimeMesh != build.runtimeMesh
        || mesh.runtimeMeshVersion != build.runtimeMeshVersion
        || mesh.runtimeGeometryContentRevision != build.geometryContentRevision
        || mesh.swBvhGeometryContentRevision != build.acceptedGeometryContentRevision
        || mesh.positionBuffer.get() != build.positionBuffer.get()
        || mesh.triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
        || mesh.swBvhNodeBuffer.get() != build.nodeBuffer.get()
        || mesh.swBvhParentBuffer.get() != build.parentBuffer.get()
        || mesh.swBvhPositionHeapHandle != build.positionHeapHandle
        || mesh.swBvhTriangleIndexHeapHandle != build.triangleIndexHeapHandle
        || mesh.swBvhNodeHeapHandle != build.nodeHeapHandle
        || mesh.swBvhParentHeapHandle != build.parentHeapHandle
        || state.m_bvhSortKeysBuffer.get() != build.sortKeysBuffer.get()
        || state.m_bvhSortPayloadBuffer.get() != build.sortPayloadBuffer.get()
        || state.m_bvhVisitCounterBuffer.get() != build.visitCounterBuffer.get()
        || state.m_bvhSortKeysHeapHandle != build.sortKeysHeapHandle
        || state.m_bvhSortPayloadHeapHandle != build.sortPayloadHeapHandle
        || state.m_bvhVisitCounterHeapHandle != build.visitCounterHeapHandle
        || mesh.meshletPrimitiveIndexCount != build.primitiveCount * s_RayTracingTriangleIndexCount
        || mesh.swBvhRefitsSinceRebuild != build.refitsBeforeBuild
        || mesh.swBvhBuildPending != build.buildPending
        || (!mesh.swBvhTopologyBuilt || !mesh.swBvhBuildAccepted) != build.firstBuild
        || mesh.csgLocalBounds.minBounds != build.aabbMin
        || mesh.csgLocalBounds.maxBounds != build.aabbMax
        || !meshSwBvhResourcesReady(
            mesh.swBvhNodeBuffer,
            mesh.swBvhParentBuffer,
            mesh.swBvhNodeHeapHandle,
            mesh.swBvhParentHeapHandle
        )
    )
        return false;
    return
        mesh.positionBuffer->getCreationDescription().byteSize == build.positionByteSize
        && mesh.triangleIndexBuffer->getCreationDescription().byteSize == build.indexByteSize
        && mesh.swBvhNodeBuffer->getCreationDescription().byteSize == build.nodeByteSize
        && mesh.swBvhParentBuffer->getCreationDescription().byteSize == build.parentByteSize
        && state.m_bvhSortKeysBuffer->getCreationDescription().byteSize == build.sortKeysByteSize
        && state.m_bvhSortPayloadBuffer->getCreationDescription().byteSize == build.sortPayloadByteSize
        && state.m_bvhVisitCounterBuffer->getCreationDescription().byteSize == build.visitCounterByteSize
    ;
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuild(
    Core::CommandList& commandList,
    const PreparedMeshSwBvhBuild& build,
    const bool meshSwBvhInputStatesGraphOwned,
    const bool sentinelClearsGraphOwned,
    const bool graphBoundaryStatesOwned
){
    if(!preparedMeshSwBvhBuildMatchesCurrent(build)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build no longer matches mesh '{}'"), StringConvert(build.meshName.c_str()));
        return false;
    }

    // Direct/retry and incomplete-plan callers retain the native AccelStructBuildInput -> ShaderResource bridge.
    if(!meshSwBvhInputStatesGraphOwned){
        commandList.setBufferState(build.positionBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(build.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    Core::BufferHandle nodeBuffer = build.nodeBuffer;
    Core::BufferHandle parentBuffer = build.parentBuffer;
    const bool recorded = build.performRefit
        ? refitMeshSwBvhPrepared(
            commandList,
            build.positionHeapHandle.slot(),
            build.triangleIndexHeapHandle.slot(),
            build.primitiveCount,
            nodeBuffer,
            parentBuffer,
            build.nodeHeapHandle,
            build.parentHeapHandle,
            sentinelClearsGraphOwned,
            graphBoundaryStatesOwned
        )
        : buildMeshSwBvhPrepared(
            commandList,
            build.positionHeapHandle.slot(),
            build.triangleIndexHeapHandle.slot(),
            build.primitiveCount,
            LoadFloatInt(build.aabbMin),
            LoadFloatInt(build.aabbMax),
            nodeBuffer,
            parentBuffer,
            build.nodeHeapHandle,
            build.parentHeapHandle,
            sentinelClearsGraphOwned,
            graphBoundaryStatesOwned
        )
    ;
    if(!recorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen software BVH build for mesh '{}'"), StringConvert(build.meshName.c_str()));
    }
    return recorded;
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuildAfterGraphClears(
    Core::CommandList& commandList,
    const PreparedMeshSwBvhBuild& build
){
    // The graph callback has exact SRV/UAV uses and a typed clear predecessor, so it owns both the entry and successor state boundaries. The native recorder keeps only dispatch-internal UAV fences.
    return recordPreparedMeshSwBvhBuild(commandList, build, true, true, true);
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuilds(
    Core::CommandList& commandList,
    const bool meshSwBvhInputStatesGraphOwned
){
    if(!m_preparedMeshSwBvhBuildsReady || m_preparedMeshSwBvhBuilds.empty())
        return false;

    // Preserve the aggregate recorder's all-plan validation before it emits any direct commands.
    // The graph-split pure-software route instead rejects its one shared packet if a later individual snapshot no longer matches.
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(!preparedMeshSwBvhBuildMatchesCurrent(build)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build no longer matches mesh '{}'"), StringConvert(build.meshName.c_str()));
            return false;
        }
    }

    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(!recordPreparedMeshSwBvhBuild(commandList, build, meshSwBvhInputStatesGraphOwned, false, false))
            return false;
    }
    return true;
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildsReady()const noexcept{
    return m_preparedMeshSwBvhBuildsReady;
}

const PreparedMeshSwBvhBuildVector& RendererRayTracingSystem::preparedMeshSwBvhBuilds()const noexcept{
    return m_preparedMeshSwBvhBuilds;
}

void RendererRayTracingSystem::confirmPreparedMeshSwBvhBuilds(){
    if(!m_preparedMeshSwBvhBuildsReady)
        return;

    bool allPlansCurrent = true;
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        ECSRenderDetail::MeshRayTracingResourceSnapshot mesh;
        if(
            !m_meshSystem.findRayTracingResourceSnapshot(build.meshName, mesh)
            || mesh.runtimeMesh != build.runtimeMesh
            || mesh.runtimeMeshVersion != build.runtimeMeshVersion
            || mesh.runtimeGeometryContentRevision != build.geometryContentRevision
            || mesh.swBvhGeometryContentRevision != build.acceptedGeometryContentRevision
            || mesh.positionBuffer.get() != build.positionBuffer.get()
            || mesh.triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
            || mesh.swBvhNodeBuffer.get() != build.nodeBuffer.get()
            || mesh.swBvhParentBuffer.get() != build.parentBuffer.get()
            || mesh.swBvhRefitsSinceRebuild != build.refitsBeforeBuild
            || mesh.swBvhBuildPending != build.buildPending
            || (!mesh.swBvhTopologyBuilt || !mesh.swBvhBuildAccepted) != build.firstBuild
        ){
            allPlansCurrent = false;
            continue;
        }

        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = mesh;
        mesh.swBvhBuildPending = false;
        mesh.swBvhBuildAccepted = true;
        mesh.swBvhGeometryContentRevision = build.geometryContentRevision;
        if(!build.performRefit)
            mesh.swBvhTopologyBuilt = true;
        mesh.swBvhRefitsSinceRebuild = build.refitsAfterBuild;
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, mesh)){
            allPlansCurrent = false;
            continue;
        }
        if(build.firstBuild){
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
                , StringConvert(build.meshName.c_str())
                , build.runtimeMesh
                , static_cast<u64>(build.primitiveCount)
            );
        }
    }
    if(!allPlansCurrent)
        m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
    clearPreparedMeshSwBvhBuilds();
}

bool RendererRayTracingSystem::preparedMeshSwBvhBuildProducesTopology(
    const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh
)const noexcept{
    if(!m_preparedMeshSwBvhBuildsReady)
        return false;
    for(const PreparedMeshSwBvhBuild& build : m_preparedMeshSwBvhBuilds){
        if(
            build.meshName == mesh.meshName
            && build.runtimeMesh == mesh.runtimeMesh
            && build.runtimeMeshVersion == mesh.runtimeMeshVersion
            && build.geometryContentRevision == mesh.runtimeGeometryContentRevision
            && build.acceptedGeometryContentRevision == mesh.swBvhGeometryContentRevision
            && build.positionBuffer.get() == mesh.positionBuffer.get()
            && build.triangleIndexBuffer.get() == mesh.triangleIndexBuffer.get()
            && build.nodeBuffer.get() == mesh.swBvhNodeBuffer.get()
            && build.parentBuffer.get() == mesh.swBvhParentBuffer.get()
            && build.buildPending == mesh.swBvhBuildPending
            && build.refitsBeforeBuild == mesh.swBvhRefitsSinceRebuild
        )
            return !build.performRefit;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

