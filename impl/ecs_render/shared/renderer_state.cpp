// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shared/renderer_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    ::HashCombine(seed, static_cast<u32>(key.pass));
    ::HashCombine(seed, key.twoSided ? 1u : 0u);
    ::HashCombine(seed, static_cast<u32>(key.csgMode));
    ::HashCombine(seed, Hasher<Name>{}(key.csgEvaluatorVariant));
    ::HashCombine(seed, key.framebufferInfo.depthFormat);
    ::HashCombine(seed, key.framebufferInfo.sampleCount);
    ::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        ::HashCombine(seed, format);

    return seed;
}

bool MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return
        lhs.material == rhs.material
        && lhs.pass == rhs.pass
        && lhs.twoSided == rhs.twoSided
        && lhs.csgMode == rhs.csgMode
        && lhs.csgEvaluatorVariant == rhs.csgEvaluatorVariant
        && lhs.framebufferInfo == rhs.framebufferInfo
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMeshState::RendererMeshState(Core::Alloc::GlobalArena& arena)
    : m_meshes(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


void RendererMeshState::invalidateResources(){
    m_meshes.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererMaterialState::RendererMaterialState(Core::Alloc::GlobalArena& arena)
    : m_surfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), arena)
    , m_pipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), arena)
    , m_instanceMutableCache(0, Hasher<Core::ECS::EntityID>(), EqualTo<Core::ECS::EntityID>(), arena)
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


void RendererMaterialState::invalidateResources(){
    m_pipelines.clear();
    m_instanceMutableCache.clear();
    m_loggedMaterialPaths.clear();
    m_instanceMutableCacheComponentMutationVersion = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererCsgState::invalidateResources(){
    m_clipBindingLayout.reset();
    m_clipBindingSet.reset();
    m_intervalPeelBindingLayout.reset();
    m_intervalPeelBindingSet.reset();
    m_receiverSpanBuildBindingLayout.reset();
    m_receiverSpanBuildBindingSet.reset();
    m_intervalCombineBindingLayout.reset();
    m_intervalCombineBindingSet.reset();
    m_receiverSurfaceBindingLayout.reset();
    m_receiverSurfaceBindingSet.reset();
    m_intervalSampleBindingLayout.reset();
    m_intervalSampleBindingSet.reset();
    m_intervalCapFillMaterialBindingLayout.reset();
    m_intervalCapFillMaterialBindingSet.reset();
    m_intervalPeelComputeShader.reset();
    m_receiverSpanBuildComputeShader.reset();
    m_intervalCombineComputeShader.reset();
    m_intervalCapFillPixelShader.reset();
    m_intervalPeelPipeline.reset();
    m_receiverSpanBuildPipeline.reset();
    m_intervalCombinePipeline.reset();
    m_intervalCapFillPipeline.reset();
    m_receiverRangeBuffer.reset();
    m_cutterBuffer.reset();
    m_intervalSampleStateBuffer.reset();
    m_frameStateCacheSignature = CsgFrameStateCacheSignature{};
    m_frameStateCache = CsgFrameState{};
    m_receiverRangeBufferCapacity = 0u;
    m_cutterBufferCapacity = 0u;
    m_frameStateCacheValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDrawState::invalidateResources(){
    m_meshBindingLayout.reset();
    m_computeBindingLayout.reset();
    m_emulationViewBindingLayout.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_meshViewBuffer.reset();
    m_emulationViewBindingSet.reset();
    m_emulationVertexShader.reset();
    m_emulationInputLayout.reset();
    m_meshViewGpuDataValid = false;
    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredState::invalidateResources(){
    m_lightingBindingLayout.reset();
    m_sceneShadingBuffer.reset();
    m_lightBuffer.reset();
    m_compositeVertexShader.reset();
    m_lightingPixelShader.reset();
    m_lightingPipeline.reset();
    m_compositeBindingLayout.reset();
    m_sampler.reset();
    m_compositePixelShader.reset();
    m_compositePipeline.reset();
    m_sceneShadingGpuDataValid = false;
    m_targets = DeferredFrameTargets{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererAvboitState::invalidateResources(){
    m_emptyBindingLayout.reset();
    m_occupancyBindingLayout.reset();
    m_depthWarpBindingLayout.reset();
    m_extinctionBindingLayout.reset();
    m_integrateBindingLayout.reset();
    m_accumulateBindingLayout.reset();
    m_linearSampler.reset();
    m_depthWarpComputeShader.reset();
    m_integrateComputeShader.reset();
    m_depthWarpPipeline.reset();
    m_integratePipeline.reset();
    m_targetsNeedClear = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingState::invalidateResources(){
    // The scene TLAS is GPU state and must be released on device/resource teardown; per-mesh BLAS
    // handles live on MeshResources and are released with the mesh cache. Ray tracing capability
    // persists across resource invalidation.
    m_tlas.reset();
    m_tlasMaxInstances = 0u;
    m_tlasDeviceAddress = 0u;
    m_tlasInstanceCount = 0u;
    m_shadowShader.reset();
    m_shadowPipeline.reset();
    m_shadowBindingLayout.reset();
    m_shadowBindingSet.reset();
    m_shadowBindingSetTlas = nullptr;
    m_shadowBindingSetInstanceMaterial = nullptr;
    m_shadowBindingSetMaterialTyped = nullptr;
    m_shadowBindingSetMeshInstances = nullptr;
    m_shadowBindingSetMeshCount = 0u;
    m_shadowSoftShader.reset();
    m_shadowSoftPipeline.reset();
    m_shadowSoftBindingSet.reset();
    m_shadowSoftBindingSetTlas = nullptr;
    m_shadowSoftBindingSetInstanceMaterial = nullptr;
    m_shadowSoftBindingSetMaterialTyped = nullptr;
    m_shadowSoftBindingSetMeshInstances = nullptr;
    m_shadowSoftBindingSetMeshCount = 0u;
    m_shadowSlotCount = 0u;
    m_shadowMeshCount = 0u;
    m_shadowMeshCapReported = false;
    m_shadowInstanceMaterialBuffer.reset();
    m_shadowInstanceMaterialCapacity = 0u;
    m_shadowInstanceBuffer.reset();
    m_shadowMaterialTypedBuffer.reset();
    m_shadowInstanceCapacity = 0u;
    m_shadowMaterialTypedCapacity = 0u;
    m_bvhSortBindingLayout.reset();
    m_bvhSortShader.reset();
    m_bvhSortPipeline.reset();
    m_bvhSortKeysBuffer.reset();
    m_bvhSortPayloadBuffer.reset();
    m_bvhSortBindingSet.reset();
    m_bvhSortBindingSetKeys = nullptr;
    m_bvhSortCapacity = 0u;
    m_bvhBuildBindingLayout.reset();
    m_bvhMortonShader.reset();
    m_bvhMortonPipeline.reset();
    m_bvhTopologyShader.reset();
    m_bvhTopologyPipeline.reset();
    m_bvhFitShader.reset();
    m_bvhFitPipeline.reset();
    m_bvhVisitCounterBuffer.reset();
    m_bvhBuildCapacity = 0u;
    m_sceneBvhNodeBuffer.reset();
    m_sceneInstanceBuffer.reset();
    m_sceneBvhNodeCapacity = 0u;
    m_sceneInstanceCapacity = 0u;
    m_sceneBvhInstanceCount = 0u;
    m_causticEmissionTargetBuffer.reset();
    m_causticEmissionTargetCapacity = 0u;
    m_causticRefractiveInstanceCount = 0u;
    m_causticLightCount = 0u;
    m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
    m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
    m_causticEmissionGateLogged = false;
    m_swShadowBindingLayout.reset();
    m_swShadowBindingSet.reset();
    m_swShadowOpaquePrepassShader.reset();
    m_swShadowOpaquePrepassPipeline.reset();
    m_swShadowSoftOpaqueShader.reset();
    m_swShadowSoftOpaquePipeline.reset();
    m_swShadowTransparentCoarseShader.reset();
    m_swShadowTransparentCoarsePipeline.reset();
    m_swShadowTransparentResolveShader.reset();
    m_swShadowTransparentResolvePipeline.reset();
    m_swShadowTransparentClassifyShader.reset();
    m_swShadowTransparentClassifyPipeline.reset();
    m_swShadowTransparentBuildArgsShader.reset();
    m_swShadowTransparentBuildArgsPipeline.reset();
    m_swShadowTransparentIndirectShader.reset();
    m_swShadowTransparentIndirectPipeline.reset();
    m_swShadowTransparentUniformShader.reset();
    m_swShadowTransparentUniformPipeline.reset();
    m_swShadowBindingSetSceneNodes = nullptr;
    m_swShadowBindingSetInstances = nullptr;
    m_swShadowBindingSetInstanceMaterial = nullptr;
    m_swShadowBindingSetMaterialTyped = nullptr;
    m_swShadowBindingSetMeshInstances = nullptr;
    m_swShadowBindingSetVisibility = nullptr;
    m_swShadowBindingSetMeshCount = 0u;
    m_swShadowMeshCount = 0u;
    m_swShadowMeshNodeBuffers.clear();
    m_swShadowMeshPositionBuffers.clear();
    m_swShadowMeshIndexBuffers.clear();
    m_swShadowMeshAttributeBuffers.clear();
    m_swShadowMeshNodeHandles.clear();
    m_swShadowMeshPositionHandles.clear();
    m_swShadowMeshIndexHandles.clear();
    m_swShadowMeshAttributeHandles.clear();
    m_swShadowTransparentSoftShader.reset();
    m_swShadowTransparentSoftPipeline.reset();
    m_swShadowEdgeStatsBuffer.reset();
    m_swShadowEdgeStatsReadback.reset();
    m_swShadowEdgeStatsTick = 0u;
    m_swShadowEdgeStatsPending = false;
    m_softShadowFrameIndex = 0u;
    m_softShadowSlotMask = 0u;
    m_shadowResolveBindingLayout.reset();
    m_shadowResolveShader.reset();
    m_shadowResolvePipeline.reset();
    m_shadowResolvePipelineFailed = false;
    m_shadowResolveBindingSetOutputHalfA.reset();
    m_shadowResolveBindingSetOutputHalfB.reset();
    m_shadowResolveBindingSetUpsample.reset();
    m_shadowResolveBindingSetSoftHalfA = nullptr;
    m_shadowResolveBindingSetSoftHalfB = nullptr;
    m_shadowResolveBindingSetGeometry = nullptr;
    m_shadowResolveBindingSetDepth = nullptr;
    m_shadowResolveBindingSetVisibility = nullptr;
    m_shadowResolveBindingSetMomentsA = nullptr;
    m_shadowResolveBindingSetMomentsB = nullptr;
    m_shadowResolveBindingSetWorldPos = nullptr;
    m_shadowResolveBindingSetNormal = nullptr;
    m_shadowGeometryDownsampleBindingLayout.reset();
    m_shadowGeometryDownsampleShader.reset();
    m_shadowGeometryDownsamplePipeline.reset();
    m_shadowGeometryDownsamplePipelineFailed = false;
    m_shadowGeometryDownsampleBindingSet.reset();
    m_shadowGeometryDownsampleWorldPosition = nullptr;
    m_shadowGeometryDownsampleNormal = nullptr;
    m_shadowGeometryDownsampleDepth = nullptr;
    m_shadowGeometryDownsampleGeometry = nullptr;
    m_softShadowReady = false;
    m_prevWorldToClip = {};
    m_prevWorldToClipValid = false;
    m_softShadowHistoryFrontIsA = 1u;
    m_softShadowTemporalSeeded = false;
    m_softShadowTemporalReady = false;
    m_shadowReprojectMergeBindingLayout.reset();
    m_shadowReprojectMergeShader.reset();
    m_shadowReprojectMergePipeline.reset();
    m_shadowReprojectMergePipelineFailed = false;
    m_shadowReprojectMergeBindingSetAtoB.reset();
    m_shadowReprojectMergeBindingSetBtoA.reset();
    m_shadowReprojectMergeHistA = nullptr;
    m_shadowReprojectMergeHistB = nullptr;
    m_shadowReprojectMergeMomentsA = nullptr;
    m_shadowReprojectMergeMomentsB = nullptr;
    m_shadowReprojectMergeSoftTrace = nullptr;
    m_shadowReprojectMergeGeometryCurr = nullptr;
    m_shadowReprojectMergeGeometryPrev = nullptr;
    m_shadowReprojectMergeWorldPosition = nullptr;
    m_shadowResolveBindingSetTemporalHistA.reset();
    m_shadowResolveBindingSetTemporalHistB.reset();
    m_shadowResolveBindingSetTemporalHistATex = nullptr;
    m_shadowResolveBindingSetTemporalHistBTex = nullptr;
    m_shadowResolveRgbShader.reset();
    m_shadowResolveRgbPipeline.reset();
    m_shadowResolveRgbPipelineFailed = false;
    m_transparentResolveBindingSetOutputHalfA.reset();
    m_transparentResolveBindingSetOutputHalfB.reset();
    m_transparentResolveBindingSetUpsample.reset();
    m_transparentResolveBindingSetTemporalHistA.reset();
    m_transparentResolveBindingSetTemporalHistB.reset();
    m_transparentResolveBindingSetSoftHalf = nullptr;
    m_transparentResolveBindingSetScratchA = nullptr;
    m_transparentResolveBindingSetScratchB = nullptr;
    m_transparentResolveBindingSetGeometry = nullptr;
    m_transparentResolveBindingSetDepth = nullptr;
    m_transparentResolveBindingSetVisibility = nullptr;
    m_transparentResolveBindingSetHistA = nullptr;
    m_transparentResolveBindingSetHistB = nullptr;
    m_transparentResolveBindingSetMomentsA = nullptr;
    m_transparentResolveBindingSetMomentsB = nullptr;
    m_transparentResolveBindingSetWorldPos = nullptr;
    m_transparentResolveBindingSetNormal = nullptr;
    m_transparentReprojectMergeBindingSetAtoB.reset();
    m_transparentReprojectMergeBindingSetBtoA.reset();
    m_transparentReprojectMergeSoftTrace = nullptr;
    m_transparentReprojectMergeHistA = nullptr;
    m_transparentReprojectMergeHistB = nullptr;
    m_transparentReprojectMergeMomentsA = nullptr;
    m_transparentReprojectMergeMomentsB = nullptr;
    m_transparentReprojectMergeGeometryCurr = nullptr;
    m_transparentReprojectMergeGeometryPrev = nullptr;
    m_transparentReprojectMergeWorldPosition = nullptr;
    m_softTransparentReady = false;
    m_softTransparentTemporalReady = false;
    m_swShadowEdgeStatsPendingTick = 0u;
    m_swShadowEdgeCounterBuffer.reset();
    m_swShadowEdgeListBuffer.reset();
    m_swShadowEdgeListCapacity = 0u;
    m_swShadowIndirectArgsBuffer.reset();
    m_sceneHasTransparentOccluder = false;
    m_hybridTransparentShadowReady = false;
    m_swCausticBindingLayout.reset();
    m_swCausticShader.reset();
    m_swCausticPipeline.reset();
    m_swCausticBindingSet.reset();
    m_swCausticBindingSetSceneNodes = nullptr;
    m_swCausticBindingSetInstances = nullptr;
    m_swCausticBindingSetInstanceMaterial = nullptr;
    m_swCausticBindingSetMaterialTyped = nullptr;
    m_swCausticBindingSetMeshInstances = nullptr;
    m_swCausticBindingSetEmissionTargets = nullptr;
    m_swCausticBindingSetView = nullptr;
    m_swCausticBindingSetDepth = nullptr;
    m_swCausticBindingSetWorldPosition = nullptr;
    m_swCausticBindingSetAccumulator = nullptr;
    m_swCausticBindingSetMeshCount = 0u;
    m_hwCausticBindingLayout.reset();
    m_hwCausticPipeline.reset();
    m_hwCausticShaderTable.reset();
    m_hwCausticBindingSet.reset();
    m_hwCausticBindingSetTlas = nullptr;
    m_hwCausticBindingSetInstanceMaterial = nullptr;
    m_hwCausticBindingSetMaterialTyped = nullptr;
    m_hwCausticBindingSetMeshInstances = nullptr;
    m_hwCausticBindingSetEmissionTargets = nullptr;
    m_hwCausticBindingSetView = nullptr;
    m_hwCausticBindingSetDepth = nullptr;
    m_hwCausticBindingSetWorldPosition = nullptr;
    m_hwCausticBindingSetAccumulator = nullptr;
    m_hwCausticBindingSetMeshCount = 0u;
    m_causticResolveBindingLayout.reset();
    m_causticResolveShader.reset();
    m_causticResolvePipeline.reset();
    m_causticResolveBindingSetOutputHalfA.reset();
    m_causticResolveBindingSetOutputHalfB.reset();
    m_causticResolveBindingSetUpsample.reset();
    m_causticResolveBindingSetAccumulator = nullptr;
    m_causticResolveBindingSetWorldPosition = nullptr;
    m_causticResolveBindingSetDepth = nullptr;
    m_causticResolveBindingSetIrradiance = nullptr;
    m_causticResolveBindingSetHalfA = nullptr;
    m_causticResolveBindingSetHalfB = nullptr;
    m_causticResolveBindingSetGeometry = nullptr;
    m_causticGeometryDownsampleBindingLayout.reset();
    m_causticGeometryDownsampleShader.reset();
    m_causticGeometryDownsamplePipeline.reset();
    m_causticGeometryDownsampleBindingSet.reset();
    m_causticGeometryDownsampleWorldPosition = nullptr;
    m_causticGeometryDownsampleDepth = nullptr;
    m_causticGeometryDownsampleGeometry = nullptr;
    m_causticGeometryDownsamplePipelineFailed = false;
    m_causticAccumulatorDecayBindingLayout.reset();
    m_causticAccumulatorDecayShader.reset();
    m_causticAccumulatorDecayPipeline.reset();
    m_causticAccumulatorDecayBindingSet.reset();
    m_causticAccumulatorDecayAccumulator = nullptr;
    m_causticAccumulatorDecayPipelineFailed = false;
    // The accumulator target is released on invalidation (deferred targets are recreated), so re-seed the EMA; the next
    // enabled frame clears instead of decaying.
    m_causticAccumulatorInitialized = false;
    // Reset the SW temporal-reuse phase so the checkerboard sequence restarts deterministically after a device reset.
    m_swCausticFrameIndex = 0u;
    // Reset the HW temporal-reuse phase likewise (byte-parallel HW scheme).
    m_hwCausticFrameIndex = 0u;
    // Surfel GI. The persistent pool/cell-head/counter/params buffers live on this state (not DeferredFrameTargets), so
    // a resize does not reset convergence -- but a full invalidate (device reset) does release + re-seed them.
    m_surfelSpawnBindingLayout.reset();
    m_surfelHashBuildBindingLayout.reset();
    m_surfelTraceBindingLayout.reset();
    m_surfelSpawnShader.reset();
    m_surfelSpawnPipeline.reset();
    m_surfelHashBuildShader.reset();
    m_surfelHashBuildPipeline.reset();
    m_surfelTraceShader.reset();
    m_surfelTracePipeline.reset();
    m_surfelSpawnBindingSet.reset();
    m_surfelHashBuildBindingSet.reset();
    m_surfelTraceBindingSet.reset();
    m_surfelResolveBindingLayout.reset();
    m_surfelResolveShader.reset();
    m_surfelResolvePipeline.reset();
    m_surfelResolveBindingSet.reset();
    m_surfelResolveBindingSetWorldPosition = nullptr;
    m_surfelResolveBindingSetNormal = nullptr;
    m_surfelResolveBindingSetOutput = nullptr;
    m_surfelUpsampleBindingLayout.reset();
    m_surfelUpsampleShader.reset();
    m_surfelUpsamplePipeline.reset();
    m_surfelUpsampleBindingSet.reset();
    m_surfelUpsampleBindingSetHalfIrradiance = nullptr;
    m_surfelUpsampleBindingSetNormal = nullptr;
    m_surfelUpsampleBindingSetWorldPosition = nullptr;
    m_surfelUpsampleBindingSetOutput = nullptr;
    m_surfelTraceBuildArgsBindingLayout.reset();
    m_surfelTraceBuildArgsShader.reset();
    m_surfelTraceBuildArgsPipeline.reset();
    m_surfelTraceBuildArgsBindingSet.reset();
    m_surfelSpawnBindingSetWorldPosition = nullptr;
    m_surfelSpawnBindingSetNormal = nullptr;
    m_surfelTraceBindingSetSceneNodes = nullptr;
    m_surfelTraceBindingSetInstances = nullptr;
    m_surfelTraceBindingSetMaterialTyped = nullptr;
    m_surfelTraceBindingSetMeshInstances = nullptr;
    m_surfelTraceBindingSetMeshCount = 0u;
    m_surfelTraceHwBindingLayout.reset();
    m_surfelTraceHwShader.reset();
    m_surfelTraceHwPipeline.reset();
    m_surfelTraceHwBindingSet.reset();
    m_surfelTraceHwBindingSetTlas = nullptr;
    m_surfelTraceHwBindingSetInstanceMaterial = nullptr;
    m_surfelTraceHwBindingSetMaterialTyped = nullptr;
    m_surfelTraceHwBindingSetMeshInstances = nullptr;
    m_surfelTraceHwBindingSetMeshCount = 0u;
    m_surfelUseHwTrace = false;
    m_surfelPoolBuffer.reset();
    m_surfelCellHeadBuffer.reset();
    m_surfelCounterBuffer.reset();
    m_surfelTraceIndirectArgsBuffer.reset();
    m_surfelConstants.reset();
    m_surfelPoolCapacity = NWB_SURFEL_POOL_CAPACITY;
    m_surfelHashCellCount = NWB_SURFEL_HASH_CELL_COUNT;
    m_surfelFrameIndex = 0u;
    m_surfelSeeded = false;
    m_surfelResourcesNeedClear = false;
    m_surfelEnabled = false;
    m_surfelSpawnPipelineFailed = false;
    m_surfelHashBuildPipelineFailed = false;
    m_surfelTracePipelineFailed = false;
    m_surfelTraceHwPipelineFailed = false;
    m_surfelResolvePipelineFailed = false;
    m_surfelUpsamplePipelineFailed = false;
    m_surfelTraceBuildArgsPipelineFailed = false;
    m_surfelDispatchLogged = false;
    m_shadowPipelineFailed = false;
    m_shadowSoftPipelineFailed = false;
    m_bvhSortPipelineFailed = false;
    m_bvhBuildPipelineFailed = false;
    m_swShadowPipelineFailed = false;
    m_swShadowDispatchLogged = false;
    m_swCausticPipelineFailed = false;
    m_causticResolvePipelineFailed = false;
    m_swCausticDispatchLogged = false;
    m_hwCausticPipelineFailed = false;
    m_hwCausticDispatchLogged = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

