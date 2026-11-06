// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_swbvh_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::confirmPreparedMeshBlasBuilds(){
    if(!m_preparedMeshBlasBuildsReady)
        return;

    b8 allPlansCurrent = true;
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        auto meshResourcesResult = m_meshSystem.findRayTracingResourceSnapshot(build.meshName);
        if(!meshResourcesResult || !RayTracingSoftwareBvhDetail::MatchesPreparedMeshBlasBuild((*meshResourcesResult), build)){
            allPlansCurrent = false;
            continue;
        }

        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = (*meshResourcesResult);
        u64 firstBuilds = m_blasLedgerFirstBuilds;
        u64 refits = m_blasLedgerRefits;
        u64 rebuilds = m_blasLedgerRebuilds;
        u64 uploadedBytes = m_blasLedgerUploadedBytes;
        ApplyConfirmedMeshBlasBuildState(
            build.firstBuild,
            build.performRefit,
            build.refitsAfterBuild,
            build.geometryContentRevision,
            build.positionByteSize,
            meshResourcesResult->blasBuildPending,
            meshResourcesResult->blasBackingFresh,
            meshResourcesResult->blasRefitsSinceRebuild,
            meshResourcesResult->blasBuildAccepted,
            meshResourcesResult->blasGeometryContentRevision,
            firstBuilds,
            refits,
            rebuilds,
            uploadedBytes
        );
        if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, (*meshResourcesResult))){
            allPlansCurrent = false;
            continue;
        }
        m_blasLedgerFirstBuilds = firstBuilds;
        m_blasLedgerRefits = refits;
        m_blasLedgerRebuilds = rebuilds;
        m_blasLedgerUploadedBytes = uploadedBytes;
        if(build.firstBuild)
            NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)"), ConvertString(build.meshName.resolvedText()), build.runtimeMesh, static_cast<u64>(build.vertexCount), static_cast<u64>(build.indexCount));
    }
    // A replacement after recording never joins the accepted cache. Force a future TLAS rebuild, not a retired hash.
    if(!allPlansCurrent)
        m_rayTracingState.m_tlasStaticSceneHashValid = false;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BLAS ownership staticSkipped={} firstBuilds={} refits={} rebuilds={} uploadedBytes={}"), m_blasLedgerStaticSkipped, m_blasLedgerFirstBuilds, m_blasLedgerRefits, m_blasLedgerRebuilds, m_blasLedgerUploadedBytes);
    clearPreparedMeshBlasBuilds();
}

b8 RendererRayTracingSystem::preparePendingMeshBlasResources(Core::Alloc::ScratchArena& scratchArena){
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    b8 allResourcesReady = true;
    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        if(!RequiresMeshBlasUpdateState(meshResources.blasBuildPending, meshResources.blasBuildAccepted, meshResources.runtimeMesh, meshResources.runtimeGeometryContentRevision, meshResources.blasGeometryContentRevision))
            continue;
        const ECSRenderDetail::MeshRayTracingResourceSnapshot expected = meshResources;
        if(!prepareMeshBlasResources(meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: BLAS resource preflight failed for mesh '{}'"), ConvertString(meshResources.meshName.resolvedText()));
            allResourcesReady = false;
        }
        else if(!m_meshSystem.commitRayTracingResourceSnapshot(expected, meshResources)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: BLAS resource preflight lost mesh '{}'"), ConvertString(meshResources.meshName.resolvedText()));
            allResourcesReady = false;
        }
    }
    return allResourcesReady;
}

b8 RendererRayTracingSystem::capturePreparedMeshBlasBuilds(Core::Alloc::ScratchArena& scratchArena){
    clearPreparedMeshBlasBuilds();
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);
    for(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources : meshes){
        // Reuse only accepted, unchanged object-space geometry, including off-screen mesh resources.
        if(!RequiresMeshBlasUpdateState(meshResources.blasBuildPending, meshResources.blasBuildAccepted, meshResources.runtimeMesh, meshResources.runtimeGeometryContentRevision, meshResources.blasGeometryContentRevision)){
            if(!meshResources.runtimeMesh)
                ++m_blasLedgerStaticSkipped;
            continue;
        }

        auto build = RayTracingSoftwareBvhDetail::ResolvePreparedMeshBlasBuild(meshResources);
        if(!build){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze BLAS build for mesh '{}'"), ConvertString(meshResources.meshName.resolvedText()));
            clearPreparedMeshBlasBuilds();
            return false;
        }
        m_preparedMeshBlasBuilds.push_back(Move(*build));
    }
    m_preparedMeshBlasBuildsReady = !m_preparedMeshBlasBuilds.empty();
    return true;
}

b8 RendererRayTracingSystem::recordPreparedMeshBlasBuilds(Core::CommandList& commandList, const b8 meshBlasGeometryBuildInputStatesGraphOwned){
    if(!m_preparedMeshBlasBuildsReady || m_preparedMeshBlasBuilds.empty())
        return false;

    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        auto meshResourcesResult = m_meshSystem.findRayTracingResourceSnapshot(build.meshName);
        if(!meshResourcesResult || !RayTracingSoftwareBvhDetail::MatchesPreparedMeshBlasBuild((*meshResourcesResult), build)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS build no longer matches mesh '{}'"), ConvertString(build.meshName.resolvedText()));
            return false;
        }
    }
    for(const PreparedMeshBlasBuild& build : m_preparedMeshBlasBuilds){
        if(!RayTracingSoftwareBvhDetail::RecordPreparedMeshBlasBuild(build, meshBlasGeometryBuildInputStatesGraphOwned, commandList)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to record frozen BLAS build for mesh '{}'"), ConvertString(build.meshName.resolvedText()));
            return false;
        }
    }
    return true;
}

void RendererRayTracingSystem::clearPreparedMeshBlasBuilds()noexcept{
    m_preparedMeshBlasBuilds.clear();
    m_preparedMeshBlasBuildsReady = false;
}

b8 RendererRayTracingSystem::prepareMeshBlasResources(ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getCreationDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    if(meshResources.blas)
        return true;

    const u32 vertexStride = static_cast<u32>(positionDesc.structStride);
    const u32 vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);
    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(meshResources.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(vertexStride)
        .setVertexCount(vertexCount)
        .setIndexBuffer(meshResources.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(meshResources.meshletPrimitiveIndexCount)
    ;
    Core::RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles).setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation);
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(meshResources.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;
    Core::RayTracingAccelStructDesc accelStructDesc(m_arena);
    accelStructDesc.addBottomLevelGeometry(geometry);
    accelStructDesc.setBuildFlags(buildFlags);
    accelStructDesc.setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    const auto blasName = DeriveName(meshResources.meshName, s_BlasNameSuffix);
    if(!blasName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'"), ConvertString(meshResources.meshName.resolvedText()));
        return false;
    }
    accelStructDesc.setDebugName(*blasName);
    auto blasResult = m_graphics.getDevice().createAccelStruct(accelStructDesc);
    if(!blasResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'"), ConvertString(meshResources.meshName.resolvedText()));
        return false;
    }
    Core::RayTracingAccelStructHandle blas = Move(*blasResult);
    meshResources.blas = Move(blas);
    InitializeCreatedMeshBlasState(
        meshResources.blasBuildPending,
        meshResources.blasBuildAccepted,
        meshResources.blasGeometryContentRevision,
        meshResources.blasBackingFresh,
        meshResources.blasRefitsSinceRebuild
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

