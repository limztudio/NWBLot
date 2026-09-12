// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/raytrace/rt_private.h>

#include <core/graphics/vulkan/backend.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::invalidateResources(){
    discardPreflightShadowVisibilityResources();
    invalidatePreparedShadowTraceGeometryBuffers();
    m_hardwareOpticalScene.invalidate();
    m_softwareOpticalScene.invalidate();
    releaseSceneTlasHeapHandle();
    releaseCausticEmissionTargetHeapHandle();
    releaseRayTraceMaterialContextHeapHandles();
    releaseSwBvhScratchHeapHandles();
    releaseSurfelGiHeapHandles();
    m_rayTracingState.invalidateResources();
}

void RendererRayTracingSystem::releaseSceneTlasHeapHandle(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_tlasHeapHandle);
        return;
    }
    m_rayTracingState.m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
}

RayTracingLightingClassificationInput RendererRayTracingSystem::snapshotLightingClassificationInput()const noexcept{
    return {
        .refractiveInstanceCount = m_rayTracingState.m_causticRefractiveInstanceCount,
    };
}

void RendererRayTracingSystem::publishPreparedLightingClassification(
    const RayTracingLightingClassification& classification,
    const ECSRenderDetail::SceneLightGpuData* const lights,
    const u32 lightCount
){
    m_rayTracingState.m_causticLightCount = classification.causticLightCount;
    m_rayTracingState.m_softShadowSlotMask = classification.softShadowSlotMask;

    // Emit the selected caustic-light and refractive-target gate once from the domain that owns the gathered bounds
    // and diagnostic latch. A successful prefix publication leaves the latch set across optional graph-build retries;
    // final declaration failure restores the earlier snapshot.
    if(m_rayTracingState.m_causticEmissionGateLogged)
        return;
    m_rayTracingState.m_causticEmissionGateLogged = true;

    const Float4& boundsMin = m_rayTracingState.m_causticTargetBoundsMin;
    const Float4& boundsMax = m_rayTracingState.m_causticTargetBoundsMax;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: caustic P1 -- {} caustic light(s); {} refractive emission target(s), combined extent min ({}, {}, {}) max ({}, {}, {})")
        , classification.causticLightCount
        , m_rayTracingState.m_causticRefractiveInstanceCount
        , boundsMin.x
        , boundsMin.y
        , boundsMin.z
        , boundsMax.x
        , boundsMax.y
        , boundsMax.z
    );

    NWB_ASSERT(lights || lightCount == 0u);
    if(!lights)
        return;
    for(u32 i = 0u; i < lightCount; ++i){
        if(lights[i].params.w < 0.f)
            continue;
        // params.y carries the light type (Directional=0, Point=1, Spot=2); point lights are excluded so only
        // directional/spot reach here.
        const bool directional = lights[i].params.y < ECSRenderDetail::s_LightTypeDirectionalMax;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: caustic P1 -- caustic slot {} -> light index {} ({})")
            , static_cast<u32>(lights[i].params.w)
            , i
            , directional ? NWB_TEXT("directional") : NWB_TEXT("spot")
        );
    }
}


RayTracingShadowPreparationResourceSnapshot RendererRayTracingSystem::snapshotShadowPreparationResources()const{
    return RayTracingShadowPreparationResourceSnapshot{
        .sceneTlas = m_rayTracingState.m_tlas,
        .sceneTlasBackingBuffer = m_rayTracingState.m_tlas ? m_rayTracingState.m_tlas->getBackingBufferHandle() : nullptr,
        .bvhSortKeysBuffer = m_rayTracingState.m_bvhSortKeysBuffer,
        .bvhSortPayloadBuffer = m_rayTracingState.m_bvhSortPayloadBuffer,
        .bvhVisitCounterBuffer = m_rayTracingState.m_bvhVisitCounterBuffer,
        .swShadowEdgeStatsBuffer = m_rayTracingState.m_swShadowEdgeStatsBuffer,
        .swShadowEdgeStatsReadback = m_rayTracingState.m_swShadowEdgeStatsReadback,
        .swShadowEdgeCounterBuffer = m_rayTracingState.m_swShadowEdgeCounterBuffer,
        .swShadowEdgeListBuffer = m_rayTracingState.m_swShadowEdgeListBuffer,
        .swShadowIndirectArgsBuffer = m_rayTracingState.m_swShadowIndirectArgsBuffer,
    };
}

RayTracingDeferredGraphResourceSnapshot RendererRayTracingSystem::snapshotDeferredGraphResources()const{
    const auto& state = m_rayTracingState;
    return RayTracingDeferredGraphResourceSnapshot{
        .materialContextSlotsBuffer = state.m_rayTraceMaterialContextSlotsBuffer,
        .shadowInstanceMaterialBuffer = state.m_shadowInstanceMaterialBuffer,
        .shadowMaterialTypedBuffer = state.m_shadowMaterialTypedBuffer,
        .shadowInstanceBuffer = state.m_shadowInstanceBuffer,
        .causticEmissionTargetBuffer = state.m_causticEmissionTargetBuffer,
        .surfelFrameConstantsBuffer = state.m_surfelConstants,
        .sceneBvhNodeBuffer = state.m_sceneBvhNodeBuffer,
        .sceneInstanceBuffer = state.m_sceneInstanceBuffer,
        .sceneTlas = state.m_tlas,
        .causticTemporalDecay = state.m_causticTemporalDecay,
        .causticAccumulatorInitialized = state.m_causticAccumulatorInitialized,
        .surfelUsesHardwareTrace = state.m_surfelUseHwTrace,
        .surfelSplitGraphPipelinesReady =
            state.m_surfelAgeFreePipeline
            && state.m_surfelHashBuildPipeline
            && state.m_surfelSpawnPipeline
            && (state.m_surfelUseHwTrace ? state.m_surfelTraceHwPipeline : state.m_surfelTracePipeline)
            && state.m_surfelResolvePipeline
            && state.m_surfelUpsamplePipeline
            && state.m_surfelTraceBuildArgsPipeline,
    };
}

RayTracingSurfelPersistentResourceSnapshot RendererRayTracingSystem::snapshotSurfelPersistentResources()const{
    return RayTracingSurfelPersistentResourceSnapshot{
        .constantsBuffer = m_rayTracingState.m_surfelConstants,
        .poolBuffer = m_rayTracingState.m_surfelPoolBuffer,
        .cellHeadBuffer = m_rayTracingState.m_surfelCellHeadBuffer,
        .counterBuffer = m_rayTracingState.m_surfelCounterBuffer,
        .traceIndirectArgsBuffer = m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
        .freeListBuffer = m_rayTracingState.m_surfelFreeListBuffer,
        .poolSnapshotBuffer = m_rayTracingState.m_surfelPoolSnapshotBuffer,
        .cellHeadSnapshotBuffer = m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
        .counterReadbackBuffer = m_rayTracingState.m_surfelCounterReadback,
        .countReadbackSubmissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken,
    };
}

RayTracingShadowVisibilityGraphPlanSnapshot RendererRayTracingSystem::snapshotShadowVisibilityGraphPlan(
    const bool hardwareShadowSupported
)const noexcept{
    const auto& state = m_rayTracingState;
    const bool softTransparentFoldReady =
        state.m_softShadowReady
        && state.m_softShadowSlotMask != 0u
        && softTransparentShadowReady()
        && state.m_sceneBvhNodeBuffer
        && state.m_sceneInstanceBuffer
        && state.m_shadowInstanceMaterialBuffer
        && state.m_shadowMaterialTypedBuffer
        && state.m_shadowInstanceBuffer
    ;
    GraphOwnedAdaptiveShadowPlan adaptivePlan;
    if(
        state.m_swShadowAdaptiveEnabled
        && !softTransparentShadowReady()
        && (
            hardwareShadowSupported
                ? hybridTransparentShadowReady()
                : shadowVisibilitySoftwareResourcesPreflighted()
        )
        && state.m_swShadowEdgeStatsBuffer
        && state.m_swShadowEdgeStatsReadback
        && state.m_swShadowEdgeCounterBuffer
    ){
        adaptivePlan.enabled = true;
        adaptivePlan.compact = state.m_swShadowCompactEnabled;
        adaptivePlan.statsTick = state.m_swShadowEdgeStatsTick;
        adaptivePlan.captureStatsSnapshot =
            state.m_swShadowEdgeStatsEnabled
            && !state.m_swShadowEdgeStatsPending
            && (adaptivePlan.statsTick % s_SwShadowEdgeStatsPeriod == 0u)
        ;
    }

    return RayTracingShadowVisibilityGraphPlanSnapshot{
        .adaptivePlan = adaptivePlan,
        .softTransparentFoldReady = softTransparentFoldReady,
        .softShadowHistoryReadable =
            state.m_softShadowTemporalReady
            && state.m_prevWorldToClipValid
            && state.m_softShadowTemporalSeeded,
        .opaqueTemporalMergeReady = state.m_softShadowTemporalReady,
        .transparentTemporalMergeReady = state.m_softTransparentTemporalReady,
        .historyFrontIsA = state.m_softShadowHistoryFrontIsA != 0u,
    };
}

bool RendererRayTracingSystem::surfelCountReadbackSubmissionMatches(
    const Core::QueueSubmissionToken& submissionToken
)const noexcept{
    const Core::QueueSubmissionToken& accepted = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
    return
        accepted.valid()
        && submissionToken.valid()
        && accepted.queue == submissionToken.queue
        && accepted.value == submissionToken.value
        && accepted.matchesPhysicalQueue(submissionToken.physicalQueueIndex, submissionToken.deviceGeneration)
    ;
}

void RendererRayTracingSystem::confirmSurfelCountReadbackSubmission(
    const Core::QueueSubmissionToken& submissionToken
)noexcept{
    NWB_ASSERT(submissionToken.valid());
    m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;
}

void RendererRayTracingSystem::retireCompletedAdaptiveShadowStatisticsReadback(){
    auto& state = m_rayTracingState;
    if(
        !state.m_swShadowEdgeStatsPending
        || (state.m_swShadowEdgeStatsTick - state.m_swShadowEdgeStatsPendingTick) < s_SwShadowEdgeStatsLogDelay
        || state.m_swShadowEdgeStatsPendingSubmissionID == 0u
        || !state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue.valid()
        || m_graphics.getDevice().queueGetCompletedInstance(
            state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue
        ) < state.m_swShadowEdgeStatsPendingSubmissionID
    )
        return;

    const u32* const stats = static_cast<const u32*>(
        m_graphics.getDevice().mapBuffer(*state.m_swShadowEdgeStatsReadback, Core::CpuAccessMode::Read)
    );
    if(stats){
        const u32 traced = stats[NWB_SW_SHADOW_EDGE_STATS_TRACED];
        const u32 total = stats[NWB_SW_SHADOW_EDGE_STATS_TOTAL];
        m_graphics.getDevice().unmapBuffer(*state.m_swShadowEdgeStatsReadback);
        const f64 fraction = (total > 0u) ? (100.0 * static_cast<f64>(traced) / static_cast<f64>(total)) : 0.0;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW shadow adaptive edge fraction = {}% ({} traced / {} total rays, threshold {})")
            , fraction
            , static_cast<u64>(traced)
            , static_cast<u64>(total)
            , static_cast<f64>(state.m_swShadowEdgeThreshold)
        );
    }
    state.m_swShadowEdgeStatsPending = false;
    state.m_swShadowEdgeStatsPendingSubmissionID = 0u;
    state.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = {};
}

void RendererRayTracingSystem::confirmGraphOwnedAdaptiveShadowSubmission(
    const GraphOwnedAdaptiveShadowPlan& plan,
    const bool adaptiveRouteRecorded,
    const Core::QueueSubmissionToken& submissionToken
){
    if(
        !plan.enabled
        || !adaptiveRouteRecorded
        || !submissionToken.valid()
        || !submissionToken.hasPhysicalQueueIdentity()
    )
        return;

    // The graph-owned plan can schedule clear/copy primitives before a later renderer callback discovers that its
    // producer is unavailable. Advance the diagnostic timeline only when that callback actually took the adaptive
    // route and its shared packet accepted.
    m_rayTracingState.m_swShadowEdgeStatsTick = plan.statsTick + 1u;
    if(!plan.captureStatsSnapshot)
        return;

    m_rayTracingState.m_swShadowEdgeStatsPending = true;
    m_rayTracingState.m_swShadowEdgeStatsPendingTick = plan.statsTick;
    m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionID = submissionToken.value;
    m_rayTracingState.m_swShadowEdgeStatsPendingSubmissionPhysicalQueue = Core::GpuPhysicalQueueId{
        submissionToken.physicalQueueIndex,
        submissionToken.deviceGeneration,
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::logCapabilityOnce(){
    if(m_rayTracingState.m_capabilityLogged)
        return;

    m_rayTracingState.m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , m_graphics.queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::shadowVisibilityResourcesPreflighted()const noexcept{
    return m_shadowVisibilityResourcesPreflighted;
}

bool RendererRayTracingSystem::shadowVisibilityHardwareSupported()const noexcept{
    return m_shadowVisibilityHardwareSupported;
}

bool RendererRayTracingSystem::shadowVisibilitySoftwareResourcesPreflighted()const noexcept{
    return m_shadowVisibilityTraceResourcesPreflighted
        && (!m_shadowVisibilityHardwareSupported || m_shadowVisibilityHybridResourcesPreflighted)
    ;
}

bool RendererRayTracingSystem::hybridShadowVisibilityResourcesPreflighted()const noexcept{
    return m_shadowVisibilityHardwareSupported
        && m_shadowVisibilityHybridResourcesPreflighted
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

