// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/material/sampled_texture_collection.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::GraphicsRuntime& graphics,
    RendererShaderSystem& shaderSystem,
    RendererMeshSystem& meshSystem,
    RendererMaterialSystem& materialSystem,
    RendererRayTracingState& rayTracingState,
    const RendererOpticalVolumeSelection& opticalVolumes,
    const CsgShapeRegistry& csgShapeRegistry
)noexcept
    : m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_shaderSystem(shaderSystem)
    , m_meshSystem(meshSystem)
    , m_materialSystem(materialSystem)
    , m_rayTracingState(rayTracingState)
    , m_opticalVolumes(opticalVolumes)
    , m_csgShapeRegistry(csgShapeRegistry)
    , m_hardwareOpticalScene(arena, graphics, Name("raytrace_optical_scene_hw"))
    , m_softwareOpticalScene(arena, graphics, Name("raytrace_optical_scene_sw"))
    , m_sceneSwBvhRefit(arena, graphics)
    , m_lightSpaceShadow(arena)
    , m_preparedShadowTraceGeometryBuffers(arena)
    , m_acceptedShadowTraceGeometryBuffers(arena)
    , m_preparedShadowTraceMaterialSampledTextures(arena)
    , m_preparedCausticEmissionTargetBytes(arena)
    , m_preparedShadowMaterialContext(arena)
    , m_preparedSceneBvh(arena)
    , m_preparedSceneTlas(arena)
    , m_preparedSceneSwBvhMeshes(arena)
    , m_preparedMeshBlasBuilds(arena)
    , m_preparedMeshSwBvhBuilds(arena)
{}

RendererRayTracingSystem::~RendererRayTracingSystem()noexcept = default;


bool RendererRayTracingSystem::preflightShadowVisibilityResources(DeferredFrameTargets& targets, Core::Alloc::ScratchArena& scratchArena){
    m_rayTracingState.m_softwareTransparentSampling.m_history.prepareScene({});
    m_lightSpaceShadow.m_captureHistory.beginFrame();
    m_lightSpaceShadow.m_captureSceneTrusted = false;
    m_lightSpaceShadow.m_sceneBuffers.clear();
    m_lightSpaceShadow.m_sceneTextures.clear();
    m_lightSpaceShadow.m_snapshot.captureTicket = {};
    m_lightSpaceShadow.m_snapshot.ready = false;
    m_lightSpaceShadow.m_resourcesPrepared = false;
    m_lightSpaceShadow.m_sceneEligible = false;
    m_lightSpaceShadow.m_casters.clear();
    // A new frame replaces the previous preflight plan, but does not invalidate retained acceleration data.
    // Full invalidation is reserved for a rejected packet or resource teardown, where recorded work may not submit.
    m_shadowVisibilityPreparedTargets = nullptr;
    m_preparedSceneContentStamp = {};
    m_hardwareOpticalScene.resetPrepared();
    m_softwareOpticalScene.resetPrepared();
    m_shadowVisibilityResourcesPreflighted = false;
    m_shadowVisibilityHardwareSupported = false;
    m_shadowVisibilityTraceResourcesPreflighted = false;
    m_shadowVisibilityBackendPipelinePreflighted = false;
    clearPreparedShadowTraceMaterialSampledTextures();
    clearPreparedShadowMaterialContext();
    clearPreparedSceneBvh();
    m_preparedSceneTlas.clear();
    clearPreparedMeshBlasBuilds();
    clearPreparedMeshSwBvhBuilds();
    // Surfel GI is a per-frame consumer of the scene selected below. Never let an empty or rejected preflight reuse the preceding frame's backend selection and dispatch against stale trace inputs.
    m_rayTracingState.m_hardwareTransparentShadow.m_ready = false;
    m_rayTracingState.m_softTransparentReady = false;
    m_rayTracingState.m_softTransparentTemporalReady = false;
    m_rayTracingState.m_surfelEnabled = false;
    m_rayTracingState.m_surfelUseHwTrace = false;
    if(!targets.shadowVisibility)
        return false;
    // The trace material-context selector is itself a global UniformBuffer heap entry. Establish it before gathering the deferred graph imports the final backing handle rather than a recording-time replacement.
    if(!ensureRayTraceMaterialContextSlotsBuffer())
        return false;

    // Caustic target storage is selected before graph compilation. Uploading the gathered payload remains in the graph-owned preparation packet.
    if(!prepareCausticEmissionTargetResources(scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target gather failed"));

    m_shadowVisibilityHardwareSupported = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && m_graphics.queryFeatureSupport(Core::Feature::RayQuery);
    if(m_shadowVisibilityHardwareSupported){
        if(!preparePendingMeshBlasResources(scratchArena)){
            if(!ensureRayTraceMaterialContextSlotsHeapHandle())
                return false;
            m_shadowVisibilityPreparedTargets = &targets;
            m_shadowVisibilityResourcesPreflighted = true;
            return true;
        }

        const bool sceneReady = prepareSceneTlasResources(scratchArena);
        if(!sceneReady){
            if(!ensureRayTraceMaterialContextSlotsHeapHandle())
                return false;
            m_shadowVisibilityPreparedTargets = &targets;
            m_shadowVisibilityResourcesPreflighted = true;
            return true;
        }
        if(!ensureCsgTraceContextResources())
            return false;

        // Hardware shadows and optical effects share one immutable TLAS/material context.
        if(!capturePreparedMeshBlasBuilds(scratchArena)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze hardware BLAS build plan"));
            m_preparedSceneTlas.clear();
            return false;
        }
        m_shadowVisibilityTraceResourcesPreflighted = true;
        const bool backendReady = ensureShadowPipeline();
        m_shadowVisibilityBackendPipelinePreflighted = backendReady;

        // Route the HW opaque shadow through the same half-res soft denoise chain the SW path uses: half-res jittered trace -> temporal reproject-merge -> a-trous resolve -> upsample.
        // The HW opaque-soft trace writes shadowSoftHalfA, then the shared soft block denoises it through the same m_softShadow* state as the SW branch.
        // Non-fatal: a failure leaves m_softShadowReady false and the HW render falls back to the full-res 1-spp trace.
        m_rayTracingState.m_softShadowReady = backendReady && ensureShadowSoftPipeline() && ensureShadowGeometryDownsamplePipeline() && ensureSoftShadowResolvePipeline();
        if(backendReady && !m_rayTracingState.m_softShadowReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow resource preparation failed; HW shadows fall back to the full-res trace this frame"));

        // Soft opaque shadow TEMPORAL accumulation (shared reproject-merge): same as the SW branch.
        // Non-fatal: a failure leaves m_softShadowTemporalReady false and the soft path feeds the raw trace straight into the a-trous.
        m_rayTracingState.m_softShadowTemporalReady = m_rayTracingState.m_softShadowReady && ensureShadowReprojectMergePipeline();
        if(m_rayTracingState.m_softShadowReady && !m_rayTracingState.m_softShadowTemporalReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

        // Hardware transparent tracing retains the same half-resolution denoise and multiplicative resolve.
        // Its bounded gather and hardware overflow route never prepare a second software acceleration structure.
        m_rayTracingState.m_softTransparentReady = m_rayTracingState.m_softShadowReady && m_rayTracingState.m_sceneHasTransparentOccluder && prepareHardwareTransparentShadowResources(targets) && ensureSoftTransparentResolvePipeline();
        if(m_rayTracingState.m_softShadowReady && m_rayTracingState.m_sceneHasTransparentOccluder && !m_rayTracingState.m_softTransparentReady)
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware transparent shadow resource preparation failed; colored shadows unavailable this frame"));

        m_rayTracingState.m_softTransparentTemporalReady = m_rayTracingState.m_softTransparentReady && m_rayTracingState.m_softShadowTemporalReady;

        prepareSoftCombinedResolvePipelines();

        // Build the hardware caustic producer resources alongside the shadow ones. Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op), mirroring the SW-branch prepareGpuBvhCausticResources call below.
        if(!prepareHwCausticResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic producer resource preparation failed"));

        // Enable surfel GI on the HW path: the HW RayQuery trace twin reuses the TLAS + the HW-resident per-mesh geometry + InstanceID-material record the shadow/caustic path already built -- this is the ONLY place surfels run on real RT hardware.
        // Gated on the HW shadow backend being ready + a non-empty TLAS.
        // m_tlasInstanceCount is the HW instance count.
        // Non-fatal: a failure leaves GI off this frame (the lighting uses hemiAmbient).
        if(backendReady && m_rayTracingState.m_tlasInstanceCount > 0u && m_rayTracingState.m_shadowMeshCount > 0u){
            m_rayTracingState.m_surfelEnabled = true;
            m_rayTracingState.m_surfelUseHwTrace = true;
            if(!prepareSurfelResources(targets))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: HW surfel GI resource preparation failed"));
        }

        if(!ensureRayTraceMaterialContextSlotsHeapHandle())
            return false;
        m_shadowVisibilityPreparedTargets = &targets;
        preflightLightSpaceShadowResources();
        if(m_lightSpaceShadow.m_csgRequired && !m_lightSpaceShadow.m_resourcesPrepared){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG shadow map preparation failed"));
            return false;
        }
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }

    // No hardware ray tracing: build/refit the per-mesh software BVHs from the already skinned geometry, then build the per-frame software scene/instance BVH over them before the render pass consumes it.
    const bool meshResourcesReady = preparePendingMeshSwBvhResources(scratchArena);
    if(!meshResourcesReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH resource preparation failed"));
    if(!meshResourcesReady || !prepareSceneSwBvhResources(scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene BVH build failed"));
        m_shadowVisibilityPreparedTargets = &targets;
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }
    if(!ensureCsgTraceContextResources())
        return false;

    // Enable surfel GI on the SW path and create its resources in the prepare phase right after the scene BVH is resident.
    // The prepared GI stages can then spawn, hash, and trace on the same frame surfels become active.
    // The pool/hash/pipeline resources live on RendererRayTracingState so a resize does not reset convergence.
    if(m_rayTracingState.m_sceneBvhInstanceCount > 0u && m_rayTracingState.m_swShadowMeshCount > 0u){
        m_rayTracingState.m_surfelEnabled = true;
        m_rayTracingState.m_surfelUseHwTrace = false;   // SW branch: the surfel trace walks the SW scene BVH
        if(!prepareSurfelResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel GI resource preparation failed"));
    }

    if(m_rayTracingState.m_sceneBvhInstanceCount == 0u){
        if(!ensureRayTraceMaterialContextSlotsHeapHandle())
            return false;
        m_shadowVisibilityPreparedTargets = &targets;
        m_shadowVisibilityResourcesPreflighted = true;
        return true;
    }
    // The software-only route can freeze every selected per-mesh build/refit because it has no later non-fatal hardware fallback.
    if(!capturePreparedMeshSwBvhBuilds(scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not freeze software BVH mesh build plan"));
        return false;
    }
    m_shadowVisibilityTraceResourcesPreflighted = true;

    const bool backendReady = ensureSwShadowPipeline();
    m_shadowVisibilityBackendPipelinePreflighted = backendReady;

    // Soft opaque shadow: downsample + a-trous pipelines denoise half-res trace; non-fatal.
    m_rayTracingState.m_softShadowReady = backendReady && ensureShadowGeometryDownsamplePipeline() && ensureSoftShadowResolvePipeline();
    if(backendReady && !m_rayTracingState.m_softShadowReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow resource preparation failed; shadows hard this frame"));

    // Temporal accumulation: reproject-merge plus SOFT_HALF variants; falls back to spatial.
    m_rayTracingState.m_softShadowTemporalReady = m_rayTracingState.m_softShadowReady && ensureShadowReprojectMergePipeline();
    if(m_rayTracingState.m_softShadowReady && !m_rayTracingState.m_softShadowTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

    // Colored transparent shadow: RGB resolve gated on opaque path; falls back to coarse/adaptive.
    m_rayTracingState.m_softTransparentReady = m_rayTracingState.m_softShadowReady && ensureSoftTransparentResolvePipeline();
    if(m_rayTracingState.m_softShadowReady && !m_rayTracingState.m_softTransparentReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow resource preparation failed; colored shadows fall back to the hard-ish path this frame"));

    m_rayTracingState.m_softTransparentTemporalReady = m_rayTracingState.m_softTransparentReady && m_rayTracingState.m_softShadowTemporalReady;

    prepareSoftCombinedResolvePipelines();

    // Build the software caustic producer + resolve resources alongside the SW shadow resources (same SW scene BVH + per-mesh geometry).
    // Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op).
    if(!prepareGpuBvhCausticResources(targets))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic producer resource preparation failed"));

    if(!ensureRayTraceMaterialContextSlotsHeapHandle())
        return false;
    m_shadowVisibilityPreparedTargets = &targets;
    preflightLightSpaceShadowResources();
    if(m_lightSpaceShadow.m_csgRequired && !m_lightSpaceShadow.m_resourcesPrepared){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: CSG shadow map preparation failed"));
        return false;
    }
    m_shadowVisibilityResourcesPreflighted = true;
    return true;
}

Expected<bool> RendererRayTracingSystem::recordPreflightShadowVisibilityResources(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool sceneTlasBuildGraphOwned,
    const bool meshBlasBuildsGraphOwned,
    const bool meshBlasGeometryBuildInputStatesGraphOwned,
    const bool meshSwBvhBuildsGraphOwned,
    const bool preparedMeshSwBvhBuildsRecordedByGraph
){
    if(!m_shadowVisibilityResourcesPreflighted || m_shadowVisibilityPreparedTargets != &targets)
        return MakeUnexpected(Failure{});

    // A non-fatal preflight miss intentionally leaves tracing unavailable for this frame. Do not retry capacity growth recording: the shared graph has already frozen its imported resource identities.
    if(!m_shadowVisibilityTraceResourcesPreflighted)
        return false;

    if(m_shadowVisibilityHardwareSupported){
        if(meshBlasBuildsGraphOwned && !recordPreparedMeshBlasBuilds(commandList, meshBlasGeometryBuildInputStatesGraphOwned))
            return MakeUnexpected(Failure{});
        if(sceneTlasBuildGraphOwned && !m_preparedSceneTlas.record(m_rayTracingState, commandList))
            return MakeUnexpected(Failure{});
        return m_shadowVisibilityBackendPipelinePreflighted;
    }

    if(!m_preparedMeshSwBvhBuildPlanFrozen || (meshSwBvhBuildsGraphOwned && !preparedMeshSwBvhBuildsRecordedByGraph)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow mesh builds have no frozen graph completion"));
        return MakeUnexpected(Failure{});
    }
    // Pure software frames have no opaque-HW fallback.
    // Fresh uploads and accepted cache reuse both retain their matching traversal snapshot, recording validates that exact plan without regathering scene/material data.
    // A missing or stale plan rejects the packet
    if(!m_preparedSceneSwBvhReady){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene has no frozen preflight traversal"));
        return MakeUnexpected(Failure{});
    }
    if(!recordPreparedSceneSwBvhTraversal())
        return MakeUnexpected(Failure{});
    if(m_rayTracingState.m_sceneBvhInstanceCount == 0u){
        m_rayTracingState.m_surfelEnabled = false;
        m_rayTracingState.m_surfelUseHwTrace = false;
        return false;
    }

    return m_shadowVisibilityBackendPipelinePreflighted;
}

void RendererRayTracingSystem::discardPreflightShadowVisibilityResources()noexcept{
    invalidateLightSpaceShadowCapture();
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_preparedShadowTraceGeometryBuffers.clear();
    clearPreparedShadowTraceMaterialSampledTextures();
    m_preparedCausticEmissionTargetBytes.clear();
    clearPreparedShadowMaterialContext();
    clearPreparedSceneBvh();
    m_preparedSceneTlas.clear();
    clearPreparedMeshBlasBuilds();
    clearPreparedMeshSwBvhBuilds();
    m_shadowVisibilityPreparedTargets = nullptr;
    m_preparedSceneContentStamp = {};
    m_hardwareOpticalScene.resetPrepared();
    m_softwareOpticalScene.resetPrepared();
    m_shadowVisibilityResourcesPreflighted = false;
    m_shadowVisibilityHardwareSupported = false;
    m_shadowVisibilityTraceResourcesPreflighted = false;
    m_shadowVisibilityBackendPipelinePreflighted = false;

    // A rejected preparation packet may have selected newly grown storage but never uploaded its contents.
    // Keep the allocations, invalidate every semantic cache, and force both static and runtime meshes through a safe rebuild on the next preflight.
    m_rayTracingState.m_tlasStaticSceneHashValid = false;
    m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
    m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
    m_rayTracingState.m_swShadowMaterialContextHashValid = false;
    m_rayTracingState.m_surfelEnabled = false;
    m_rayTracingState.m_surfelUseHwTrace = false;

    // Recording updates these flags optimistically.  If the shared packet never submits, make every retained acceleration structure rebuildable instead of allowing a later frame to consume its unrecorded contents.
    m_meshSystem.discardRayTracingBuildState();
}

bool RendererRayTracingSystem::freezePreparedShadowTraceGeometryBuffers(Core::Alloc::ScratchArena& scratchArena){
    m_preparedShadowTraceGeometryBuffers.clear();
    if(!m_shadowVisibilityResourcesPreflighted)
        return false;

    ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ scratchArena };
    m_meshSystem.collectRayTracingResourceSnapshots(meshes);

    // Raw tables are populated by preflight. Only the selected backend contributes trace geometry.
    const ShadowTraceGeometrySelection selection{
        .hardwarePositions = m_rayTracingState.m_shadowMeshPositionBuffers,
        .hardwareIndices = m_rayTracingState.m_shadowMeshIndexBuffers,
        .hardwareAttributes = m_rayTracingState.m_shadowMeshAttributeBuffers,
        .softwareNodes = m_rayTracingState.m_swShadowMeshNodeBuffers,
        .softwarePositions = m_rayTracingState.m_swShadowMeshPositionBuffers,
        .softwareIndices = m_rayTracingState.m_swShadowMeshIndexBuffers,
        .softwareAttributes = m_rayTracingState.m_swShadowMeshAttributeBuffers,
        .includeHardware = m_shadowVisibilityHardwareSupported && m_shadowVisibilityTraceResourcesPreflighted,
        .includeSoftware = m_shadowVisibilityTraceResourcesPreflighted
            && !m_shadowVisibilityHardwareSupported,
    };
    return FreezePreparedShadowTraceGeometryBuffers(meshes, selection, scratchArena, m_acceptedShadowTraceGeometryBuffers, m_preparedShadowTraceGeometryBuffers);
}

void RendererRayTracingSystem::confirmPreparedShadowTraceGeometryNormalization()noexcept{
    for(PreparedShadowTraceGeometryBuffer& resource : m_preparedShadowTraceGeometryBuffers){
        if(resource.buffer && resource.normalizationPending){
            NWB_ASSERT(m_acceptedShadowTraceGeometryBuffers.size() < m_acceptedShadowTraceGeometryBuffers.capacity());
            m_acceptedShadowTraceGeometryBuffers.push_back(resource.buffer);
            resource.normalizationPending = false;
        }
    }
}

void RendererRayTracingSystem::invalidatePreparedShadowTraceGeometryBuffers()noexcept{
    m_preparedShadowTraceGeometryBuffers.clear();
    m_acceptedShadowTraceGeometryBuffers.clear();
    clearPreparedShadowTraceMaterialSampledTextures();
}

Expected<PreparedSceneBvhUploads> RendererRayTracingSystem::retainPreparedSceneBvhUploads(Core::GpuTaskGraph& graph)const{
    return m_preparedSceneBvh.retainUploads(m_rayTracingState, graph);
}

void RendererRayTracingSystem::confirmPreparedSceneBvhUploads()noexcept{
    if(m_preparedSceneBvh.publishAcceptedUpload(m_rayTracingState))
        clearPreparedSceneBvh();
}

Core::ResourceStates::Mask RendererRayTracingSystem::sceneTlasBackingInitialState()const noexcept{
    const auto& state = m_rayTracingState;
    return state.m_tlasBackingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
}

void RendererRayTracingSystem::confirmPreparedSceneTlasBuild()noexcept{
    m_preparedSceneTlas.confirm(m_rayTracingState);
}

bool RendererRayTracingSystem::preparedTraversalStorageMatchesCurrent()const noexcept{
    return m_preparedSceneBvh.storageMatches(m_rayTracingState) && m_preparedShadowMaterialContext.storageMatches(m_rayTracingState);
}

bool RendererRayTracingSystem::appendPreparedShadowTraceMaterialSampledTextures(const MaterialSurfaceInfo& materialInfo, ShadowMaterialSampledTextureCollector& collector){
    return collector.collect(materialInfo, [this](const MaterialSurfaceInfo& material, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
        return m_materialSystem.appendPreparedMaterialSurfaceSampledTextures(material, pending);
    }
    );
}

void RendererRayTracingSystem::clearPreparedShadowTraceMaterialSampledTextures()noexcept{
    m_preparedShadowTraceMaterialSampledTextures.clear();
}

bool RendererRayTracingSystem::capturePreparedSceneBvh(
    const bool staticScene,
    const u64 staticSceneHash,
    const void* const nodeData,
    const usize nodeCount,
    const usize nodeByteCount,
    const void* const instanceData,
    const usize instanceCount,
    const usize instanceByteCount
){
    clearPreparedSceneBvh();
    return m_preparedSceneBvh.capture(m_rayTracingState, staticScene, staticSceneHash, nodeData, nodeCount, nodeByteCount, instanceData, instanceCount, instanceByteCount);
}

bool RendererRayTracingSystem::capturePreparedSceneBvhCacheReuse(const u64 staticSceneHash, const u32 instanceCount)noexcept{
    clearPreparedSceneBvh();
    return m_preparedSceneBvh.captureCacheReuse(m_rayTracingState, staticSceneHash, instanceCount);
}

void RendererRayTracingSystem::clearPreparedSceneBvh()noexcept{
    m_preparedSceneSwBvhRefit.reset();
    clearPreparedSceneSwBvhTraversal();
    m_preparedSceneBvh.clear();
}

bool RendererRayTracingSystem::capturePreparedSceneSwBvhTraversal(const PreparedSceneSwBvhMesh* const meshes, const usize meshCount, const u32 instanceCount){
    clearPreparedSceneSwBvhTraversal();
    const auto validStorageHandle = [](const Core::GpuDescriptorHandle handle)noexcept{
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    if(
        !meshes
        || meshCount == 0u
        || meshCount > static_cast<usize>(Limit<u32>::s_Max)
        || instanceCount == 0u
        || !m_preparedSceneBvh.covers(instanceCount)
        || !m_preparedShadowMaterialContext.softwareSnapshotCovers(instanceCount)
        || !preparedTraversalStorageMatchesCurrent()
    )
        return false;

    m_preparedSceneSwBvhMeshes.reserve(meshCount);
    for(usize index = 0u; index < meshCount; ++index){
        const PreparedSceneSwBvhMesh& mesh = meshes[index];
        if(
            mesh.meshName == s_NameNone
            || !mesh.nodeBuffer
            || !mesh.positionBuffer
            || !mesh.triangleIndexBuffer
            || !mesh.attributeBuffer
            || !validStorageHandle(mesh.nodeHeapHandle)
            || !validStorageHandle(mesh.positionHeapHandle)
            || !validStorageHandle(mesh.triangleIndexHeapHandle)
            || !validStorageHandle(mesh.attributeHeapHandle)
            || mesh.primitiveCount == 0u
            || mesh.nodeByteSize == 0u
            || mesh.positionByteSize == 0u
            || mesh.triangleIndexByteSize == 0u
            || mesh.attributeByteSize == 0u
        ){
            clearPreparedSceneSwBvhTraversal();
            return false;
        }
        m_preparedSceneSwBvhMeshes.push_back(mesh);
    }
    m_preparedSceneSwBvhInstanceCount = instanceCount;
    m_preparedSceneSwBvhRendererMutationVersion = m_world.componentMutationVersion<RendererComponent>();
    m_preparedSceneSwBvhTransformMutationVersion = m_world.componentMutationVersion<NWB::Impl::Scene::TransformComponent>();
    m_preparedSceneSwBvhMaterialMutationVersion = m_world.componentMutationVersion<MaterialInstanceComponent>();
    m_preparedSceneSwBvhReady = true;
    return true;
}

bool RendererRayTracingSystem::recordPreparedSceneSwBvhTraversal(){
    const auto validStorageHandle = [](const Core::GpuDescriptorHandle handle)noexcept{
        return handle.valid() && handle.descriptorClass() == Core::GpuDescriptorClass::StorageBuffer;
    };
    auto& state = m_rayTracingState;
    const bool sceneSnapshotValid = m_preparedSceneBvh.snapshotAccepted(state);
    const bool materialSnapshotValid = m_preparedShadowMaterialContext.softwareSnapshotAccepted(state);
    const auto rejectPreparedTraversal = [&]()noexcept{
        if(!m_preparedSceneBvh.uploadRequired())
            state.m_sceneSwBvhStaticSceneHashValid = false;
        if(!m_preparedShadowMaterialContext.uploadRequired())
            state.m_swShadowMaterialContextHashValid = false;
        return false;
    };
    if(
        !m_preparedSceneSwBvhReady
        || m_preparedSceneSwBvhMeshes.empty()
        || m_preparedSceneSwBvhInstanceCount == 0u
        || !m_preparedSceneBvh.covers(m_preparedSceneSwBvhInstanceCount)
        || !m_preparedShadowMaterialContext.softwareSnapshotCovers(m_preparedSceneSwBvhInstanceCount)
        || !sceneSnapshotValid
        || !materialSnapshotValid
        || !preparedTraversalStorageMatchesCurrent()
        || !validStorageHandle(state.m_sceneBvhNodeHeapHandle)
        || !validStorageHandle(state.m_sceneInstanceHeapHandle)
        || !validStorageHandle(state.m_shadowInstanceMaterialHeapHandle)
        || !validStorageHandle(state.m_shadowInstanceHeapHandle)
        || !validStorageHandle(state.m_shadowMaterialTypedHeapHandle)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal no longer matches preflight storage"));
        return rejectPreparedTraversal();
    }
    if(
        m_world.componentMutationVersion<RendererComponent>() != m_preparedSceneSwBvhRendererMutationVersion
        || m_world.componentMutationVersion<NWB::Impl::Scene::TransformComponent>() != m_preparedSceneSwBvhTransformMutationVersion
        || m_world.componentMutationVersion<MaterialInstanceComponent>() != m_preparedSceneSwBvhMaterialMutationVersion
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene inputs changed after graph preflight"));
        return rejectPreparedTraversal();
    }

    const auto matchesMesh = [this, &validStorageHandle](const ECSRenderDetail::MeshRayTracingResourceSnapshot& mesh, const PreparedSceneSwBvhMesh& prepared)noexcept{
        const auto attributeCache = m_rayTracingState.m_swMeshHeapHandleCache.find(NotNull<const Core::Buffer*>(prepared.attributeBuffer.get()));
        const bool topologyReady = mesh.swBvhTopologyBuilt || preparedMeshSwBvhBuildProducesTopology(mesh);
        return
            mesh.meshName == prepared.meshName
            && mesh.runtimeMesh == prepared.runtimeMesh
            && mesh.runtimeMeshVersion == prepared.runtimeMeshVersion
            && mesh.runtimeGeometryContentRevision == prepared.geometryContentRevision
            && mesh.swBvhNodeBuffer.get() == prepared.nodeBuffer.get()
            && mesh.positionBuffer.get() == prepared.positionBuffer.get()
            && mesh.triangleIndexBuffer.get() == prepared.triangleIndexBuffer.get()
            && mesh.attributeBuffer.get() == prepared.attributeBuffer.get()
            && mesh.swBvhNodeHeapHandle == prepared.nodeHeapHandle
            && mesh.swBvhPositionHeapHandle == prepared.positionHeapHandle
            && mesh.swBvhTriangleIndexHeapHandle == prepared.triangleIndexHeapHandle
            && attributeCache != m_rayTracingState.m_swMeshHeapHandleCache.end()
            && attributeCache.value().handle == prepared.attributeHeapHandle
            && topologyReady
            && mesh.meshletPrimitiveIndexCount == prepared.primitiveCount * s_RayTracingTriangleIndexCount
            && mesh.swBvhNodeBuffer->getCreationDescription().byteSize == prepared.nodeByteSize
            && mesh.positionBuffer->getCreationDescription().byteSize == prepared.positionByteSize
            && mesh.triangleIndexBuffer->getCreationDescription().byteSize == prepared.triangleIndexByteSize
            && mesh.attributeBuffer->getCreationDescription().byteSize == prepared.attributeByteSize
            && validStorageHandle(prepared.nodeHeapHandle)
            && validStorageHandle(prepared.positionHeapHandle)
            && validStorageHandle(prepared.triangleIndexHeapHandle)
            && validStorageHandle(prepared.attributeHeapHandle)
        ;
    };

    for(const PreparedSceneSwBvhMesh& prepared : m_preparedSceneSwBvhMeshes){
        auto meshResult = m_meshSystem.findRayTracingResourceSnapshot(prepared.meshName);
        if(!meshResult || !matchesMesh((*meshResult), prepared)){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene lost mesh '{}'"), StringConvert(prepared.meshName.resolvedText()));
            return rejectPreparedTraversal();
        }
    }

    const bool tablesMatch =
        state.m_swShadowMeshCount == static_cast<u32>(m_preparedSceneSwBvhMeshes.size())
        && state.m_sceneBvhInstanceCount == m_preparedSceneSwBvhInstanceCount
        && state.m_swShadowMeshNodeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshPositionBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshIndexBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshAttributeBuffers.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshNodeHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshPositionHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshIndexHandles.size() == m_preparedSceneSwBvhMeshes.size()
        && state.m_swShadowMeshAttributeHandles.size() == m_preparedSceneSwBvhMeshes.size()
    ;
    if(!tablesMatch){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal table changed after graph preflight"));
        return rejectPreparedTraversal();
    }
    for(usize index = 0u; index < m_preparedSceneSwBvhMeshes.size(); ++index){
        const PreparedSceneSwBvhMesh& prepared = m_preparedSceneSwBvhMeshes[index];
        if(
            state.m_swShadowMeshNodeBuffers[index].get() != prepared.nodeBuffer.get()
            || state.m_swShadowMeshPositionBuffers[index].get() != prepared.positionBuffer.get()
            || state.m_swShadowMeshIndexBuffers[index].get() != prepared.triangleIndexBuffer.get()
            || state.m_swShadowMeshAttributeBuffers[index].get() != prepared.attributeBuffer.get()
            || state.m_swShadowMeshNodeHandles[index] != prepared.nodeHeapHandle
            || state.m_swShadowMeshPositionHandles[index] != prepared.positionHeapHandle
            || state.m_swShadowMeshIndexHandles[index] != prepared.triangleIndexHeapHandle
            || state.m_swShadowMeshAttributeHandles[index] != prepared.attributeHeapHandle
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene traversal table no longer matches preflight"));
            return rejectPreparedTraversal();
        }
    }
    return true;
}

void RendererRayTracingSystem::clearPreparedSceneSwBvhTraversal()noexcept{
    m_preparedSceneSwBvhMeshes.clear();
    m_preparedSceneSwBvhInstanceCount = 0u;
    m_preparedSceneSwBvhRendererMutationVersion = 0u;
    m_preparedSceneSwBvhTransformMutationVersion = 0u;
    m_preparedSceneSwBvhMaterialMutationVersion = 0u;
    m_preparedSceneSwBvhReady = false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

