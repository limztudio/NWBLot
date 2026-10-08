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
        const auto boundResources = m_meshSystem.ensureRayTracingInputHeapHandles(meshResources);
        if(!boundResources){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH input heap registration failed for mesh '{}'"), StringConvert(meshResources.meshName.resolvedText()));
            allResourcesReady = false;
            continue;
        }
        ECSRenderDetail::MeshRayTracingResourceSnapshot preparedResources = *boundResources;
        if(!ensureMeshSwBvhResources(
            primitiveCount,
            preparedResources.swBvhNodeBuffer,
            preparedResources.swBvhParentBuffer,
            preparedResources.swBvhNodeHeapHandle,
            preparedResources.swBvhParentHeapHandle
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preparation failed for mesh '{}'"), StringConvert(meshResources.meshName.resolvedText()));
            allResourcesReady = false;
        }
        else if(!m_meshSystem.commitRayTracingResourceSnapshot(*boundResources, preparedResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software BVH resource preflight lost mesh '{}'"), StringConvert(meshResources.meshName.resolvedText()));
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
            && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(state.m_bvhSortKeysHeapHandle)
            && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(state.m_bvhSortPayloadHeapHandle)
            && RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(state.m_bvhVisitCounterHeapHandle)
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
            || !RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(mesh.swBvhPositionHeapHandle)
            || !RayTracingSoftwareBvhDetail::IsStorageBufferHeapHandle(mesh.swBvhTriangleIndexHeapHandle)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software BVH build for mesh '{}'"), StringConvert(mesh.meshName.resolvedText()));
            clearPreparedMeshSwBvhBuilds();
            return false;
        }

        const u32 primitiveCount = mesh.meshletPrimitiveIndexCount / s_RayTracingTriangleIndexCount;
        const bool firstBuild = !mesh.swBvhTopologyBuilt || !mesh.swBvhBuildAccepted;
        const bool performRefit =
            mesh.runtimeMesh
            && !firstBuild
            && mesh.swBvhRefitsSinceRebuild < AdaptiveRefitsBeforeRebuild(primitiveCount)
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
    auto meshResult = m_meshSystem.findRayTracingResourceSnapshot(build.meshName);
    if(!meshResult)
        return false;

    const auto& state = m_rayTracingState;
    if(
        meshResult->meshName != build.meshName
        || meshResult->runtimeMesh != build.runtimeMesh
        || meshResult->runtimeMeshVersion != build.runtimeMeshVersion
        || meshResult->runtimeGeometryContentRevision != build.geometryContentRevision
        || meshResult->swBvhGeometryContentRevision != build.acceptedGeometryContentRevision
        || meshResult->positionBuffer.get() != build.positionBuffer.get()
        || meshResult->triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
        || meshResult->swBvhNodeBuffer.get() != build.nodeBuffer.get()
        || meshResult->swBvhParentBuffer.get() != build.parentBuffer.get()
        || meshResult->swBvhPositionHeapHandle != build.positionHeapHandle
        || meshResult->swBvhTriangleIndexHeapHandle != build.triangleIndexHeapHandle
        || meshResult->swBvhNodeHeapHandle != build.nodeHeapHandle
        || meshResult->swBvhParentHeapHandle != build.parentHeapHandle
        || state.m_bvhSortKeysBuffer.get() != build.sortKeysBuffer.get()
        || state.m_bvhSortPayloadBuffer.get() != build.sortPayloadBuffer.get()
        || state.m_bvhVisitCounterBuffer.get() != build.visitCounterBuffer.get()
        || state.m_bvhSortKeysHeapHandle != build.sortKeysHeapHandle
        || state.m_bvhSortPayloadHeapHandle != build.sortPayloadHeapHandle
        || state.m_bvhVisitCounterHeapHandle != build.visitCounterHeapHandle
        || meshResult->meshletPrimitiveIndexCount != build.primitiveCount * s_RayTracingTriangleIndexCount
        || meshResult->swBvhRefitsSinceRebuild != build.refitsBeforeBuild
        || meshResult->swBvhBuildPending != build.buildPending
        || (!meshResult->swBvhTopologyBuilt || !meshResult->swBvhBuildAccepted) != build.firstBuild
        || meshResult->csgLocalBounds.minBounds != build.aabbMin
        || meshResult->csgLocalBounds.maxBounds != build.aabbMax
        || !meshSwBvhResourcesReady(
            meshResult->swBvhNodeBuffer,
            meshResult->swBvhParentBuffer,
            meshResult->swBvhNodeHeapHandle,
            meshResult->swBvhParentHeapHandle
        )
    )
        return false;
    return
        meshResult->positionBuffer->getCreationDescription().byteSize == build.positionByteSize
        && meshResult->triangleIndexBuffer->getCreationDescription().byteSize == build.indexByteSize
        && meshResult->swBvhNodeBuffer->getCreationDescription().byteSize == build.nodeByteSize
        && meshResult->swBvhParentBuffer->getCreationDescription().byteSize == build.parentByteSize
        && state.m_bvhSortKeysBuffer->getCreationDescription().byteSize == build.sortKeysByteSize
        && state.m_bvhSortPayloadBuffer->getCreationDescription().byteSize == build.sortPayloadByteSize
        && state.m_bvhVisitCounterBuffer->getCreationDescription().byteSize == build.visitCounterByteSize
    ;
}

bool RendererRayTracingSystem::recordPreparedMeshSwBvhBuildAfterGraphClears(
    Core::CommandList& commandList,
    const PreparedMeshSwBvhBuild& build
){
    if(!preparedMeshSwBvhBuildMatchesCurrent(build)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software BVH build no longer matches mesh '{}'"), StringConvert(build.meshName.resolvedText()));
        return false;
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
            build.parentHeapHandle
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
            build.parentHeapHandle
        )
    ;
    if(!recorded){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen software BVH build for mesh '{}'"), StringConvert(build.meshName.resolvedText()));
    }
    return recorded;
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
        auto meshResult = m_meshSystem.findRayTracingResourceSnapshot(build.meshName);
        if(
            !meshResult
            || meshResult->runtimeMesh != build.runtimeMesh
            || meshResult->runtimeMeshVersion != build.runtimeMeshVersion
            || meshResult->runtimeGeometryContentRevision != build.geometryContentRevision
            || meshResult->swBvhGeometryContentRevision != build.acceptedGeometryContentRevision
            || meshResult->positionBuffer.get() != build.positionBuffer.get()
            || meshResult->triangleIndexBuffer.get() != build.triangleIndexBuffer.get()
            || meshResult->swBvhNodeBuffer.get() != build.nodeBuffer.get()
            || meshResult->swBvhParentBuffer.get() != build.parentBuffer.get()
            || meshResult->swBvhRefitsSinceRebuild != build.refitsBeforeBuild
            || meshResult->swBvhBuildPending != build.buildPending
            || (!meshResult->swBvhTopologyBuilt || !meshResult->swBvhBuildAccepted) != build.firstBuild
        ){
            allPlansCurrent = false;
            continue;
        }

        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = (*meshResult);
        meshResult->swBvhBuildPending = false;
        meshResult->swBvhBuildAccepted = true;
        meshResult->swBvhGeometryContentRevision = build.geometryContentRevision;
        if(!build.performRefit)
            meshResult->swBvhTopologyBuilt = true;
        meshResult->swBvhRefitsSinceRebuild = build.refitsAfterBuild;
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, (*meshResult))){
            allPlansCurrent = false;
            continue;
        }
        if(build.firstBuild){
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
                , StringConvert(build.meshName.resolvedText())
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

