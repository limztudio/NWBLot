// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_execute_state_lifecycles.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameExecuteStateLifecycleSet::FrameExecuteStateLifecycleSet(
    RendererFramePipeline& renderer,
    DeferredFrameTargets& targets,
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources,
    const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources,
    const RayTracingSurfelPersistentResourceSnapshot& rayTracingSurfelResources,
    const bool hardwareShadowSupported,
    const bool usesLaggedHistory
)
    : shadowPrepareStateScratchArena(RendererArenaScope::s_TaskGraphArena)
    , meshAccelerationStateBuffers(shadowPrepareStateScratchArena)
    , shadowPrepareLiveStateBuffers(shadowPrepareStateScratchArena)
    , shadowPrepareAcceptedStateCandidate(renderer.m_shadowPreparePersistentState)
    , normalExecutionScratchArena(RendererArenaScope::s_TaskGraphArena)
    , shadowVisibilityReturnTextures{ targets.shadowVisibility, }
    , lightSpaceShadowResources(renderer.m_raytracingSystem.lightSpaceShadowSnapshot())
    , shadowComputeScratchTextures{
        lightSpaceShadowResources.depth,
        targets.shadowCoarseTransmittance,
        targets.shadowSoftHalfA,
        targets.shadowSoftHalfB,
        targets.shadowSoftGeometry,
        targets.shadowSoftGeometryPrev,
        targets.shadowHistA,
        targets.shadowHistB,
        targets.shadowMomentsA,
        targets.shadowMomentsB,
        targets.transparentSoftHalf,
        targets.transparentHistA,
        targets.transparentHistB,
        targets.transparentMomentsA,
        targets.transparentMomentsB,
    }
    , shadowComputeScratchBuffers{
        lightSpaceShadowResources.counts,
        lightSpaceShadowResources.events,
        lightSpaceShadowResources.views,
        lightSpaceShadowResources.drawArguments,
        lightSpaceShadowResources.csgContext,
        lightSpaceShadowResources.csgOpaqueDepth,
        rayTracingShadowResources.swShadowEdgeCounterBuffer,
        rayTracingShadowResources.swShadowEdgeListBuffer,
        rayTracingShadowResources.swShadowIndirectArgsBuffer,
        rayTracingGraphResources.hardwareTransparentCrossingsBuffer,
        rayTracingGraphResources.hardwareTransparentOverflowListBuffer,
        rayTracingGraphResources.hardwareTransparentOverflowArgsBuffer,
    }
    , shadowVisibilityReturnStateCandidate(renderer.m_shadowVisibilityReturnState)
    , shadowComputeScratchStateCandidate(renderer.m_shadowComputePersistentState)
    , causticsComputeScratchTextures{ targets.causticAccumulator, targets.causticHistory, targets.causticResolveHalf, targets.causticResolveGeometry, }
    , causticIrradianceTextures{ targets.causticIrradiance, }
    , causticReturnStateCandidate(renderer.m_causticIrradianceReturnState)
    , causticsScratchStateCandidate(renderer.m_causticsComputePersistentState)
    , surfelIrradianceReturnTextures{ targets.surfelIrradiance, }
    , surfelGiCounterBuffers{ rayTracingSurfelResources.counterBuffer, }
    , surfelGiComputeScratchTextures{ targets.surfelIrradianceHalf, }
    , surfelGiComputeScratchBuffers{
        rayTracingSurfelResources.poolBuffer,
        rayTracingSurfelResources.cellHeadBuffer,
        rayTracingSurfelResources.traceIndirectArgsBuffer,
        rayTracingSurfelResources.freeListBuffer,
        rayTracingSurfelResources.poolSnapshotBuffer,
        rayTracingSurfelResources.cellHeadSnapshotBuffer,
    }
    , surfelIrradianceReturnStateCandidate(renderer.m_surfelIrradianceReturnState)
    , surfelGiCounterStateCandidate(renderer.m_surfelGiCounterPersistentState)
    , surfelGiComputeStateCandidate(renderer.m_surfelGiComputePersistentState)
    , hardwareCausticAccumulatorTextures{ targets.causticAccumulator, }
    , hardwareCausticAccumulatorStateCandidate(renderer.m_hardwareCausticAccumulatorPersistentState)
    , deferredLightingShadowReturnStateCandidate(renderer.m_shadowVisibilityReturnState)
    , deferredLightingCausticReturnStateCandidate(renderer.m_causticIrradianceReturnState)
    , deferredLightingSurfelReturnStateCandidate(renderer.m_surfelIrradianceReturnState)
    , shadowPrepareStateLifecycle{
        .renderer = MakeNotNull(&renderer),
        .scratchArena = shadowPrepareStateScratchArena,
        .stateCandidate = MakeNotNull(&shadowPrepareAcceptedStateCandidate),
    }
    , shadowVisibilityStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .targets = MakeNotNull(&targets),
        .returnStateCandidate = MakeNotNull(&shadowVisibilityReturnStateCandidate),
        .scratchStateCandidate = MakeNotNull(&shadowComputeScratchStateCandidate),
        .returnTextures = shadowVisibilityReturnTextures,
        .scratchTextures = shadowComputeScratchTextures,
        .scratchBuffers = shadowComputeScratchBuffers,
        .lightSpacePrepared = &renderer.m_shadowPreparationOutcome.ready,
        .lightSpaceCaptureTicket = lightSpaceShadowResources.captureTicket,
        .returnTextureCount = LengthOf(shadowVisibilityReturnTextures),
        .scratchTextureCount = LengthOf(shadowComputeScratchTextures),
        .scratchBufferCount = LengthOf(shadowComputeScratchBuffers),
    }
    , softwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .returnStateCandidate = MakeNotNull(&causticReturnStateCandidate),
        .scratchStateCandidate = MakeNotNull(&causticsScratchStateCandidate),
        .irradianceTextures = causticIrradianceTextures,
        .scratchTextures = causticsComputeScratchTextures,
        .irradianceTextureCount = LengthOf(causticIrradianceTextures),
        .scratchTextureCount = LengthOf(causticsComputeScratchTextures),
    }
    , surfelGiStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .returnStateCandidate = MakeNotNull(&surfelIrradianceReturnStateCandidate),
        .counterStateCandidate = MakeNotNull(&surfelGiCounterStateCandidate),
        .computeStateCandidate = MakeNotNull(&surfelGiComputeStateCandidate),
        .returnTextures = surfelIrradianceReturnTextures,
        .counterBuffers = surfelGiCounterBuffers,
        .computeTextures = surfelGiComputeScratchTextures,
        .computeBuffers = surfelGiComputeScratchBuffers,
        .returnTextureCount = LengthOf(surfelIrradianceReturnTextures),
        .counterBufferCount = LengthOf(surfelGiCounterBuffers),
        .computeTextureCount = LengthOf(surfelGiComputeScratchTextures),
        .computeBufferCount = LengthOf(surfelGiComputeScratchBuffers),
    }
    , hardwareCausticsStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .accumulatorStateCandidate = MakeNotNull(&hardwareCausticAccumulatorStateCandidate),
        .accumulatorTextures = hardwareCausticAccumulatorTextures,
        .accumulatorTextureCount = LengthOf(hardwareCausticAccumulatorTextures),
    }
    , deferredLightingStateLifecycle{
        .scratchArena = normalExecutionScratchArena,
        .renderer = MakeNotNull(&renderer),
        .targets = MakeNotNull(&targets),
        .shadowReturnStateCandidate = MakeNotNull(&deferredLightingShadowReturnStateCandidate),
        .causticReturnStateCandidate = MakeNotNull(&deferredLightingCausticReturnStateCandidate),
        .surfelReturnStateCandidate = MakeNotNull(&deferredLightingSurfelReturnStateCandidate),
        .shadowReturnTextures = shadowVisibilityReturnTextures,
        .causticReturnTextures = causticIrradianceTextures,
        .surfelReturnTextures = surfelIrradianceReturnTextures,
        .shadowReturnTextureCount = LengthOf(shadowVisibilityReturnTextures),
        .causticReturnTextureCount = LengthOf(causticIrradianceTextures),
        .surfelReturnTextureCount = LengthOf(surfelIrradianceReturnTextures),
        .usesLaggedHistory = usesLaggedHistory,
    }
{
    collectShadowPrepareStateBuffers(renderer, rayTracingShadowResources, rayTracingGraphResources);
    bindCallbacks(renderer, hardwareShadowSupported);
}


void FrameExecuteStateLifecycleSet::collectShadowPrepareStateBuffers(RendererFramePipeline& renderer, const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources, const RayTracingDeferredGraphResourceSnapshot& rayTracingGraphResources){
    renderer.m_meshSystem.collectRetainedAccelerationStateBuffers(meshAccelerationStateBuffers);
    const auto& acceptedTraceGeometry = renderer.m_raytracingSystem.acceptedShadowTraceGeometryBuffers();
    const auto& preparedTraceGeometry = renderer.m_raytracingSystem.preparedShadowTraceGeometryBuffers();
    shadowPrepareLiveStateBuffers.reserve(AddSize(AddSize(acceptedTraceGeometry.size(), preparedTraceGeometry.size()), AddSize(meshAccelerationStateBuffers.size(), rayTracingGraphResources.csgTraceContextBuffer ? s_ShadowPrepareCsgStateBufferCapacity : s_ShadowPrepareFixedStateBufferCapacity)));
    const auto appendStateBuffer = [this](const Core::BufferHandle& buffer)noexcept{
        if(buffer)
            shadowPrepareLiveStateBuffers.push_back(buffer);
    };
    if(rayTracingShadowResources.sceneTlasBackingBuffer)
        appendStateBuffer(rayTracingShadowResources.sceneTlasBackingBuffer);
    bool stateCandidateRequired = static_cast<bool>(rayTracingShadowResources.sceneTlas) || renderer.m_raytracingSystem.preparedMeshSwBvhBuildsReady() || renderer.m_raytracingSystem.shadowVisibilitySoftwareResourcesPreflighted();
    // Keep accepted trace-geometry state with the handoff, including invisible streams.
    for(const Core::BufferHandle& acceptedBuffer : acceptedTraceGeometry){
        appendStateBuffer(acceptedBuffer);
        stateCandidateRequired = true;
    }
    for(const PreparedShadowTraceGeometryBuffer& preparedBuffer : preparedTraceGeometry){
        appendStateBuffer(preparedBuffer.buffer);
        stateCandidateRequired = true;
    }
    for(const Core::BufferHandle& buffer : meshAccelerationStateBuffers){
        if(buffer)
            stateCandidateRequired = true;
        appendStateBuffer(buffer);
    }
    appendStateBuffer(rayTracingShadowResources.bvhSortKeysBuffer);
    appendStateBuffer(rayTracingShadowResources.bvhSortPayloadBuffer);
    appendStateBuffer(rayTracingShadowResources.bvhVisitCounterBuffer);
    if(rayTracingGraphResources.csgTraceContextBuffer){
        appendStateBuffer(rayTracingGraphResources.csgTraceContextBuffer);
        stateCandidateRequired = true;
    }

    shadowPrepareStateLifecycle.buffers = shadowPrepareLiveStateBuffers.data();
    shadowPrepareStateLifecycle.bufferCount = shadowPrepareLiveStateBuffers.size();
    shadowPrepareStateLifecycle.stateCandidateRequired = stateCandidateRequired;
}

void FrameExecuteStateLifecycleSet::bindCallbacks(const RendererFramePipeline& renderer, const bool hardwareShadowSupported)noexcept{
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredShadowPrepareTask, .context = &shadowPrepareStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareShadowPrepareTask, };
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredShadowVisibilityTask, .context = &shadowVisibilityStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareShadowVisibilityTask, };
    if(!hardwareShadowSupported){
        recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredSoftwareCausticsTask, .context = &softwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareSoftwareCausticsTask, };
    }
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredSurfelGiTask, .context = &surfelGiStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareSurfelGiTask, };
    if(hardwareShadowSupported){
        recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredHardwareCausticsTask, .context = &hardwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareHardwareCausticsTask, };
    }
    recordedCallbacks[recordedCallbackCount++] = Core::GpuTaskGraphTaskRecordedCallback{ .task = renderer.m_deferredLightingTask, .context = &deferredLightingStateLifecycle, .invoke = FrameExecuteLifecycle::PrepareDeferredLightingTask, };
    NWB_ASSERT(recordedCallbackCount <= LengthOf(recordedCallbacks));

    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredShadowPrepareTask, .context = &shadowPrepareStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptShadowPrepareTask, };
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredShadowVisibilityTask, .context = &shadowVisibilityStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptShadowVisibilityTask, };
    if(!hardwareShadowSupported){
        acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredSoftwareCausticsTask, .context = &softwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptSoftwareCausticsTask, };
    }
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredSurfelGiTask, .context = &surfelGiStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptSurfelGiTask, };
    if(hardwareShadowSupported){
        acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredHardwareCausticsTask, .context = &hardwareCausticsStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptHardwareCausticsTask, };
    }
    acceptedCallbacks[acceptedCallbackCount++] = Core::GpuTaskGraphTaskAcceptedCallback{ .task = renderer.m_deferredLightingTask, .context = &deferredLightingStateLifecycle, .invoke = FrameExecuteLifecycle::AcceptDeferredLightingTask, };
    NWB_ASSERT(acceptedCallbackCount <= LengthOf(acceptedCallbacks));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

