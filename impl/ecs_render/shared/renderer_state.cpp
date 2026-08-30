// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shared/renderer_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDrawState::invalidateResources(){
    m_computeBindingLayout.reset();
    m_instanceBuffer.reset();
    m_materialTypedBuffer.reset();
    m_meshViewBuffer.reset();
    m_instanceBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_materialTypedBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_meshViewBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_emulationVertexShader.reset();
    m_emulationInputLayout.reset();
    m_meshViewGpuDataValid = false;
    m_instanceBufferCapacity = 0u;
    m_materialTypedBufferCapacity = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingState::invalidateResources(){
    // The scene TLAS is GPU state and must be released on device/resource teardown; per-mesh BLAS
    // handles live on MeshResources and are released with the mesh cache. Ray tracing capability
    // persists across resource invalidation.
    m_tlas.reset();
    m_tlasBackingFresh = false;
    m_tlasBackingStateHandoffPending = false;
    m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_tlasMaxInstances = 0u;
    m_tlasDeviceAddress = 0u;
    m_tlasInstanceCount = 0u;
    m_tlasStaticSceneHash = 0u;
    m_sceneSwBvhStaticSceneHash = 0u;
    m_tlasStaticSceneHashValid = false;
    m_sceneSwBvhStaticSceneHashValid = false;
    m_hwShadowMaterialContextHash = 0u;
    m_swShadowMaterialContextHash = 0u;
    m_hwShadowMaterialContextHashValid = false;
    m_swShadowMaterialContextHashValid = false;
    m_shadowShader.reset();
    m_shadowPipeline.reset();
    m_shadowBindingLayout.reset();
    m_shadowSoftShader.reset();
    m_shadowSoftPipeline.reset();
    m_shadowMeshCount = 0u;
    m_shadowMeshIndexBuffers.clear();
    m_shadowMeshAttributeBuffers.clear();
    m_shadowMeshPositionBuffers.clear();
    m_shadowMeshIndexHandles.clear();
    m_shadowMeshAttributeHandles.clear();
    m_shadowMeshPositionHandles.clear();
    m_shadowInstanceMaterialBuffer.reset();
    m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_shadowInstanceMaterialCapacity = 0u;
    m_shadowInstanceBuffer.reset();
    m_shadowMaterialTypedBuffer.reset();
    m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_shadowInstanceCapacity = 0u;
    m_shadowMaterialTypedCapacity = 0u;
    m_bvhSortBindingLayout.reset();
    m_bvhSortShader.reset();
    m_bvhSortPipeline.reset();
    m_bvhSortKeysBuffer.reset();
    m_bvhSortPayloadBuffer.reset();
    m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_bvhSortCapacity = 0u;
    m_bvhBuildBindingLayout.reset();
    m_bvhMortonShader.reset();
    m_bvhMortonPipeline.reset();
    m_bvhTopologyShader.reset();
    m_bvhTopologyPipeline.reset();
    m_bvhFitShader.reset();
    m_bvhFitPipeline.reset();
    m_bvhVisitCounterBuffer.reset();
    m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_bvhBuildCapacity = 0u;
    m_sceneBvhNodeBuffer.reset();
    m_sceneInstanceBuffer.reset();
    m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_rayTraceMaterialContextSlotsBuffer.reset();
    m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_shadowMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_sceneBvhNodeCapacity = 0u;
    m_sceneInstanceCapacity = 0u;
    m_sceneBvhInstanceCount = 0u;
    m_causticEmissionTargetBuffer.reset();
    m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_causticEmissionTargetCapacity = 0u;
    m_causticRefractiveInstanceCount = 0u;
    m_causticLightCount = 0u;
    m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
    m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
    m_causticEmissionGateLogged = false;
    m_swShadowBindingLayout.reset();
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
    m_swShadowMeshCount = 0u;
    m_swShadowMeshNodeBuffers.clear();
    m_swShadowMeshPositionBuffers.clear();
    m_swShadowMeshIndexBuffers.clear();
    m_swShadowMeshAttributeBuffers.clear();
    m_swShadowMeshNodeHandles.clear();
    m_swShadowMeshPositionHandles.clear();
    m_swShadowMeshIndexHandles.clear();
    m_swShadowMeshAttributeHandles.clear();
    // The stable handle caches pin their buffers (BufferHandle refs) and own heap slots; drop both so a device-loss
    // / resource-invalidation teardown cannot leave dangling handles or hold buffers past teardown. The heap itself
    // is torn down by the device, so only the refs (not heap.free) need clearing here.
    m_hwMeshHeapHandleCache.clear();
    m_swMeshHeapHandleCache.clear();
    m_swShadowTransparentSoftShader.reset();
    m_swShadowTransparentSoftPipeline.reset();
    m_swShadowEdgeStatsBuffer.reset();
    m_swShadowEdgeStatsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_swShadowEdgeStatsReadback.reset();
    m_swShadowEdgeStatsTick = 0u;
    m_swShadowEdgeStatsPending = false;
    m_swShadowEdgeStatsPendingSubmissionID = 0u;
    m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = {};
    m_softShadowFrameIndex = 0u;
    m_softShadowSlotMask = 0u;
    m_shadowResolveBindingLayout.reset();
    m_shadowResolveShader.reset();
    m_shadowResolvePipeline.reset();
    m_shadowResolvePipelineFailed = false;
    m_shadowGeometryDownsampleBindingLayout.reset();
    m_shadowGeometryDownsampleShader.reset();
    m_shadowGeometryDownsamplePipeline.reset();
    m_shadowGeometryDownsamplePipelineFailed = false;
    m_softShadowReady = false;
    m_prevWorldToClip = {};
    m_prevWorldToClipValid = false;
    m_softShadowHistoryFrontIsA = 1u;
    m_softShadowTemporalSeeded = false;
    m_softShadowTemporalHistoryAdvancePending = false;
    m_softShadowTemporalReady = false;
    m_shadowReprojectMergeBindingLayout.reset();
    m_shadowReprojectMergeShader.reset();
    m_shadowReprojectMergePipeline.reset();
    m_shadowReprojectMergePipelineFailed = false;
    m_shadowResolveRgbShader.reset();
    m_shadowResolveRgbPipeline.reset();
    m_shadowResolveRgbPipelineFailed = false;
    m_softTransparentReady = false;
    m_softTransparentTemporalReady = false;
    m_swShadowEdgeStatsPendingTick = 0u;
    m_swShadowEdgeCounterBuffer.reset();
    m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_swShadowEdgeListBuffer.reset();
    m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_swShadowEdgeListCapacity = 0u;
    m_swShadowIndirectArgsBuffer.reset();
    m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_sceneHasTransparentOccluder = false;
    m_hybridTransparentShadowReady = false;
    m_swCausticBindingLayout.reset();
    m_swCausticShader.reset();
    m_swCausticPipeline.reset();
    m_hwCausticBindingLayout.reset();
    m_hwCausticPipeline.reset();
    m_hwCausticShaderTable.reset();
    m_causticResolveBindingLayout.reset();
    m_causticResolveShader.reset();
    m_causticResolvePipeline.reset();
    m_causticGeometryDownsampleBindingLayout.reset();
    m_causticGeometryDownsampleShader.reset();
    m_causticGeometryDownsamplePipeline.reset();
    m_causticGeometryDownsamplePipelineFailed = false;
    m_causticAccumulatorDecayBindingLayout.reset();
    m_causticAccumulatorDecayShader.reset();
    m_causticAccumulatorDecayPipeline.reset();
    m_causticAccumulatorDecayPipelineFailed = false;
    // The accumulator target is released on invalidation (deferred targets are recreated), so re-seed the EMA; the next
    // enabled frame clears instead of decaying.
    m_causticAccumulatorInitialized = false;
    m_causticTemporalReuseFrameCount = 0u;
    // Reset the SW temporal-reuse phase so the interleaved sequence restarts deterministically after a device reset.
    m_swCausticFrameIndex = 0u;
    // Reset the HW temporal-reuse phase likewise (byte-parallel HW scheme).
    m_hwCausticFrameIndex = 0u;
    // Surfel GI. The persistent pool/cell-head/counter/params buffers live on this state (not DeferredFrameTargets), so
    // a resize does not reset convergence -- but a full invalidate (device reset) does release + re-seed them.
    m_surfelSpawnBindingLayout.reset();
    m_surfelAgeFreeBindingLayout.reset();
    m_surfelHashBuildBindingLayout.reset();
    m_surfelTraceBindingLayout.reset();
    m_surfelSpawnShader.reset();
    m_surfelSpawnPipeline.reset();
    m_surfelAgeFreeShader.reset();
    m_surfelAgeFreePipeline.reset();
    m_surfelHashBuildShader.reset();
    m_surfelHashBuildPipeline.reset();
    m_surfelTraceShader.reset();
    m_surfelTracePipeline.reset();
    m_surfelResolveBindingLayout.reset();
    m_surfelResolveShader.reset();
    m_surfelResolvePipeline.reset();
    m_surfelUpsampleBindingLayout.reset();
    m_surfelUpsampleShader.reset();
    m_surfelUpsamplePipeline.reset();
    m_surfelTraceBuildArgsBindingLayout.reset();
    m_surfelTraceBuildArgsShader.reset();
    m_surfelTraceBuildArgsPipeline.reset();
    m_surfelTraceHwBindingLayout.reset();
    m_surfelTraceHwShader.reset();
    m_surfelTraceHwPipeline.reset();
    m_surfelUseHwTrace = false;
    m_surfelPoolBuffer.reset();
    m_surfelCellHeadBuffer.reset();
    m_surfelCounterBuffer.reset();
    m_surfelTraceIndirectArgsBuffer.reset();
    m_surfelFreeListBuffer.reset();
    m_surfelPoolSnapshotBuffer.reset();
    m_surfelCellHeadSnapshotBuffer.reset();
    m_surfelCounterReadback.reset();
    m_surfelConstants.reset();
    m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    m_surfelPoolCapacity = NWB_SURFEL_POOL_CAPACITY;
    m_surfelHashCellCount = NWB_SURFEL_HASH_CELL_COUNT;
    m_surfelFrameIndex = 0u;
    m_surfelCountReadbackFrame = 0u;
    m_surfelCountReadbackSubmissionToken = {};
    m_surfelSeeded = false;
    m_surfelResourcesNeedClear = false;
    m_surfelResourcesClearPending = false;
    m_surfelEnabled = false;
    m_surfelSpawnPipelineFailed = false;
    m_surfelAgeFreePipelineFailed = false;
    m_surfelHashBuildPipelineFailed = false;
    m_surfelTracePipelineFailed = false;
    m_surfelTraceHwPipelineFailed = false;
    m_surfelResolvePipelineFailed = false;
    m_surfelUpsamplePipelineFailed = false;
    m_surfelTraceBuildArgsPipelineFailed = false;
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

