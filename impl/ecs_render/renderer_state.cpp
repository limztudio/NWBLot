// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_state.h"


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


RendererCsgState::RendererCsgState(Core::Alloc::GlobalArena& arena){
    static_cast<void>(arena);
}

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
    m_occupancyPixelShader.reset();
    m_depthWarpComputeShader.reset();
    m_extinctionPixelShader.reset();
    m_integrateComputeShader.reset();
    m_accumulatePixelShader.reset();
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
    m_shadowShader.reset();
    m_shadowPipeline.reset();
    m_shadowBindingLayout.reset();
    m_shadowBindingSet.reset();
    m_shadowBindingSetTlas = nullptr;
    m_shadowBindingSetInstanceMaterial = nullptr;
    m_shadowBindingSetMaterialTyped = nullptr;
    m_shadowBindingSetMeshInstances = nullptr;
    m_shadowBindingSetMeshCount = 0u;
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
    m_swShadowShader.reset();
    m_swShadowPipeline.reset();
    m_swShadowBindingSet.reset();
    m_swShadowBindingSetSceneNodes = nullptr;
    m_swShadowBindingSetInstances = nullptr;
    m_swShadowBindingSetInstanceMaterial = nullptr;
    m_swShadowBindingSetMaterialTyped = nullptr;
    m_swShadowBindingSetMeshInstances = nullptr;
    m_swShadowBindingSetVisibility = nullptr;
    m_swShadowBindingSetMeshCount = 0u;
    m_swShadowMeshCount = 0u;
    m_swShadowMeshCapReported = false;
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
    // The accumulator target is released on invalidation (deferred targets are recreated), so its splat-space history is
    // gone -- re-seed the EMA (the next enabled frame clears instead of decaying).
    m_causticAccumulatorInitialized = false;
    m_shadowPipelineFailed = false;
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

