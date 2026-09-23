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


bool RendererRayTracingSystem::buildPendingMeshBlas(
    Core::CommandList& commandList,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = meshResources;

        if(!RequiresMeshBlasUpdate(meshResources))
            continue;
        if(!buildMeshBlas(commandList, meshResources))
            return false;
        meshResources.blasBuildPending = false;
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, meshResources)){
            m_rayTracingState.m_tlasStaticSceneHashValid = false;
            return false;
        }
    }
    return true;
}

bool RendererRayTracingSystem::preparePendingMeshBlasResources(Core::Alloc::ScratchArena& scratchArena){
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    bool allResourcesReady = true;
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        if(!RequiresMeshBlasUpdate(meshResources))
            continue;
        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = meshResources;
        if(!prepareMeshBlasResources(meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: BLAS resource preflight failed for mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            allResourcesReady = false;
        }
        else if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: BLAS resource preflight lost mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}

void RendererRayTracingSystem::clearPreparedMeshBlasBuilds()noexcept{
    m_preparedMeshBlasBuilds.clear();
    m_preparedMeshBlasBuildsReady = false;
}

bool RendererRayTracingSystem::capturePreparedMeshBlasBuilds(Core::Alloc::ScratchArena& scratchArena){
    clearPreparedMeshBlasBuilds();
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        // Reuse only accepted, unchanged object-space geometry, including off-screen mesh resources.
        if(!RequiresMeshBlasUpdate(meshResources)){
            if(!meshResources.runtimeMesh)
                ++m_blasLedgerStaticSkipped;
            continue;
        }

        PreparedMeshBlasBuild build;
        if(!__hidden_rt_swbvh::ResolvePreparedMeshBlasBuild(meshResources, build)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze BLAS build for mesh '{}'"), StringConvert(meshResources.meshName.c_str()));
            clearPreparedMeshBlasBuilds();
            return false;
        }
        m_preparedMeshBlasBuilds.push_back(Move(build));
    }
    m_preparedMeshBlasBuildsReady = !m_preparedMeshBlasBuilds.empty();
    return true;
}

bool RendererRayTracingSystem::recordPreparedMeshBlasBuilds(
    Core::CommandList& commandList,
    const bool meshBlasAccelStructStatesGraphOwned,
    const bool meshBlasGeometryBuildInputStatesGraphOwned
){
    if(!m_preparedMeshBlasBuildsReady || m_preparedMeshBlasBuilds.empty())
        return false;

    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        ECSRenderDetail::MeshRayTracingResourceSnapshot meshResources;
        if(
            !m_meshSystem.findRayTracingResourceSnapshot(build.meshName, meshResources)
            || !__hidden_rt_swbvh::MatchesPreparedMeshBlasBuild(meshResources, build)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build no longer matches mesh '{}'"), StringConvert(build.meshName.c_str()));
            return false;
        }
    }
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        if(!__hidden_rt_swbvh::RecordPreparedMeshBlasBuild(
            commandList,
            build,
            meshBlasAccelStructStatesGraphOwned,
            meshBlasGeometryBuildInputStatesGraphOwned
        )){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen BLAS build for mesh '{}'"), StringConvert(build.meshName.c_str()));
            return false;
        }
    }
    return true;
}

bool RendererRayTracingSystem::preparedMeshBlasBuildsReady()const noexcept{
    return m_preparedMeshBlasBuildsReady;
}

const PreparedMeshBlasBuildVector& RendererRayTracingSystem::preparedMeshBlasBuilds()const noexcept{
    return m_preparedMeshBlasBuilds;
}

void RendererRayTracingSystem::confirmPreparedMeshBlasBuilds(){
    if(!m_preparedMeshBlasBuildsReady)
        return;

    bool allPlansCurrent = true;
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        ECSRenderDetail::MeshRayTracingResourceSnapshot meshResources;
        if(
            !m_meshSystem.findRayTracingResourceSnapshot(build.meshName, meshResources)
            || !__hidden_rt_swbvh::MatchesPreparedMeshBlasBuild(meshResources, build)
        ){
            allPlansCurrent = false;
            continue;
        }

        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = meshResources;
        meshResources.blasBuildPending = false;
        // The accepted Shadow Preparation state handoff now owns this generation's native final state.
        meshResources.blasBackingFresh = false;
        meshResources.blasBackingStateHandoffPending = false;
        meshResources.blasRefitsSinceRebuild = build.refitsAfterBuild;
        meshResources.blasBuildAccepted = true;
        meshResources.blasGeometryContentRevision = build.geometryContentRevision;
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, meshResources)){
            allPlansCurrent = false;
            continue;
        }
        if(build.firstBuild){
            ++m_blasLedgerFirstBuilds;
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
                , StringConvert(build.meshName.c_str())
                , build.runtimeMesh
                , static_cast<u64>(build.vertexCount)
                , static_cast<u64>(build.indexCount)
            );
        }
        else if(build.performRefit)
            ++m_blasLedgerRefits;
        else
            ++m_blasLedgerRebuilds;
        m_blasLedgerUploadedBytes += static_cast<u64>(build.positionByteSize);
    }
    // An unexpected replacement after recording is not rolled into the accepted mesh cache. Force a future TLAS rebuild rather than retaining a static-scene hash that may describe the retired generation.
    if(!allPlansCurrent)
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BLAS ownership staticSkipped={} firstBuilds={} refits={} rebuilds={} uploadedBytes={}")
        , m_blasLedgerStaticSkipped
        , m_blasLedgerFirstBuilds
        , m_blasLedgerRefits
        , m_blasLedgerRebuilds
        , m_blasLedgerUploadedBytes
    );
    clearPreparedMeshBlasBuilds();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

