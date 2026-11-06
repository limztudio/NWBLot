// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::invalidateResources(){
    discardPreflightShadowVisibilityResources();
    invalidatePreparedShadowTraceGeometryBuffers();
    m_hardwareOpticalScene.invalidate();
    m_softwareOpticalScene.invalidate();
    m_preparedSceneSwBvhRefit.reset();
    m_sceneSwBvhRefit.invalidate();
    releaseSceneTlasHeapHandle();
    releaseCausticEmissionTargetHeapHandle();
    releaseCausticResolveActivity();
    releaseRayTraceMaterialContextHeapHandles();
    releaseSwBvhScratchHeapHandles();
    releaseSurfelGiHeapHandles();
    releaseHardwareTransparentShadowResources();
    releaseLightSpaceShadowResources();
    m_refractionStatistics.invalidateResources();
    m_rayTracingState.invalidateResources();
}

void RendererRayTracingSystem::releaseSceneTlasHeapHandle(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_tlasHeapHandle);
        return;
    }
    m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
}

RayTracingLightingClassificationInput RendererRayTracingSystem::snapshotLightingClassificationInput()const noexcept{
    return { .refractiveInstanceCount = m_rayTracingState.m_causticRefractiveInstanceCount, };
}

void RendererRayTracingSystem::publishPreparedLightingClassification(const RayTracingLightingClassification& classification, const Span<const SceneLightGpuData> lights){
    m_rayTracingState.m_causticLightCount = classification.causticLightCount;
    LogUnsupportedCausticVolumesIfNeeded(
        m_shadowVisibilityTraceResourcesPreflighted,
        classification.causticLightCount,
        m_unsupportedCausticVolumeCount,
        m_unsupportedCausticVolumeHash,
        m_firstUnsupportedCausticVolumeEntity,
        m_unsupportedCausticVolumeReported,
        m_reportedUnsupportedCausticVolumeHash
    );
    m_rayTracingState.m_softShadowSlotMask = classification.softShadowSlotMask;
    m_rayTracingState.m_softwareTransparentSampling.m_history.prepareLighting(lights);
    prepareLightSpaceShadows(lights);

    // Emit the selected caustic-light and refractive-target gate once from the domain that owns the gathered bounds and diagnostic latch.
    // A successful prefix publication leaves the latch set across optional graph-build retries; final declaration failure restores the earlier snapshot.
    LogCausticEmissionGateIfNeeded(
        classification.causticLightCount,
        m_rayTracingState.m_causticRefractiveInstanceCount,
        LoadFloat(m_rayTracingState.m_causticTargetBoundsMin),
        LoadFloat(m_rayTracingState.m_causticTargetBoundsMax),
        lights,
        m_rayTracingState.m_causticEmissionGateLogged
    );
}

RayTracingShadowPreparationResourceSnapshot RendererRayTracingSystem::snapshotShadowPreparationResources()const noexcept{
    return RayTracingShadowPreparationResourceSnapshot{
        .sceneTlas = m_rayTracingState.m_tlas,
        .sceneTlasBackingBuffer = m_rayTracingState.m_tlas ? m_rayTracingState.m_tlas->getBackingBufferHandle() : nullptr,
        .bvhSortKeysBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_bvhSortKeysBuffer,
        .bvhSortPayloadBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_bvhSortPayloadBuffer,
        .bvhVisitCounterBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_bvhVisitCounterBuffer,
        .swShadowEdgeCounterBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_swShadowEdgeCounterBuffer,
        .swShadowEdgeListBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_swShadowEdgeListBuffer,
        .swShadowIndirectArgsBuffer = m_shadowVisibilityHardwareSupported ? nullptr : m_rayTracingState.m_swShadowIndirectArgsBuffer,
    };
}

RayTracingDeferredGraphResourceSnapshot RendererRayTracingSystem::snapshotDeferredGraphResources()const noexcept{
    const auto& state = m_rayTracingState;
    const b8 hasCsgTrace = m_shadowVisibilityTraceResourcesPreflighted && m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    usize contextReadTokenCount = 0u;
    for(const auto& token : state.m_rayTraceContextReadSubmissionTokens){
        if(token.valid()){
            contextReadTokenCount = state.m_rayTraceContextReadSubmissionTokens.size();
            break;
        }
    }
    return RayTracingDeferredGraphResourceSnapshot{
        .materialContextSlotsBuffer = state.m_rayTraceMaterialContextSlotsBuffer,
        .csgTraceContextBuffer = hasCsgTrace ? state.m_csgTraceContextBuffer : nullptr,
        .rayTraceContextReadSubmissionTokens = contextReadTokenCount != 0u
            ? Span<const Core::QueueSubmissionToken>(state.m_rayTraceContextReadSubmissionTokens.data(), contextReadTokenCount)
            : Span<const Core::QueueSubmissionToken>{},
        .csgTraceBoundsBuffers = hasCsgTrace ? Span<const Core::BufferHandle>(m_lightSpaceShadow.m_csg.dynamicBounds) : Span<const Core::BufferHandle>{},
        .shadowInstanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer,
        .shadowMaterialTypedBuffer = state.m_shadowMaterialTypedBuffer,
        .shadowInstanceBuffer = state.m_shadowInstanceBuffer,
        .causticEmissionTargetBuffer = state.m_causticEmissionTargetBuffer,
        .surfelFrameConstantsBuffer = state.m_surfelConstants,
        .hardwareTransparentCrossingsBuffer = state.m_hardwareTransparentShadow.m_crossingsBuffer,
        .hardwareTransparentOverflowListBuffer = state.m_hardwareTransparentShadow.m_overflowListBuffer,
        .hardwareTransparentOverflowArgsBuffer = state.m_hardwareTransparentShadow.m_overflowArgsBuffer,
        .sceneBvhNodeBuffer = m_shadowVisibilityHardwareSupported ? nullptr : state.m_sceneBvhNodeBuffer,
        .sceneInstanceBuffer = m_shadowVisibilityHardwareSupported ? nullptr : state.m_sceneInstanceBuffer,
        .sceneTlas = state.m_tlas,
        .causticAccumulatorInitialized = state.m_causticAccumulatorInitialized,
        .surfelUsesHardwareTrace = state.m_surfelUseHwTrace,
        .surfelSplitGraphPipelinesReady =
            state.m_surfelAgeFreePipeline
            && state.m_surfelHashBuildPipeline
            && state.m_surfelSpawnPipeline
            && (state.m_surfelUseHwTrace ? (state.m_surfelUseCsgTrace ? state.m_surfelTraceCsgHwPipeline : state.m_surfelTraceHwPipeline) : (state.m_surfelUseCsgTrace ? state.m_surfelTraceCsgPipeline : state.m_surfelTracePipeline))
            && state.m_surfelResolvePipeline
            && state.m_surfelUpsamplePipeline
            && state.m_surfelTraceBuildArgsPipeline,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::logCapabilityOnce(){
    LogRayTracingCapabilityIfNeeded(
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct),
        m_graphics.queryFeatureSupport(Core::Feature::RayTracingPipeline),
        m_graphics.queryFeatureSupport(Core::Feature::RayQuery),
        m_rayTracingState.m_capabilityLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

