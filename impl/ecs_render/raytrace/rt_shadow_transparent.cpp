// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_shadow_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderSoftTransparentShadowTrace(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueToTransparentBoundary,
    const LightSpaceShadowSnapshot* const lightSpace
){
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    if(lightSpace && lightSpace->ready){
        NWB_ASSERT(graphEntryStatesOwned && graphOwnsOpaqueToTransparentBoundary);
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowTransparentTrace, m_graphics.getDevice(), commandList
        );

        return RecordLightSpaceResolve(
            commandList, m_graphics.getDevice().getDescriptorHeap(), m_graphics.gpuTiming(), *lightSpace, *targets.transparentSoftHalf,
            frameIndex, NWB_SW_SHADOW_TRANSPARENT_SPP, targets.bindless.transparentSoftHalfStorage.slot(), true
        );
    }
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueToTransparentBoundary,
        false,
        false,
        false,
        false,
        false
    );
    if(!hardwareTransparentShadowReady())
        reportSoftwareShadowTraversal(targets);
    return true;
}

void RendererRayTracingSystem::reportSoftwareShadowTraversal(const DeferredFrameTargets& targets){
    if(m_rayTracingState.m_swShadowDispatchLogged)
        return;

    m_rayTracingState.m_swShadowDispatchLogged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
        , static_cast<u64>(targets.width)
        , static_cast<u64>(targets.height)
        , static_cast<u64>(m_rayTracingState.m_sceneBvhInstanceCount)
    );
}

bool RendererRayTracingSystem::renderSoftTransparentShadowTemporalMerge(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsTransparentTemporalMergeEntryStates
){
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || !m_rayTracingState.m_softTransparentTemporalReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        graphOwnsTransparentTemporalMergeEntryStates,
        false,
        false,
        false,
        false,
        true
    );
    return true;
}

bool RendererRayTracingSystem::renderSoftTransparentShadowFirstWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsTransparentWaveletInputBoundary
){
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        false,
        true,
        false,
        false,
        graphOwnsTransparentWaveletInputBoundary,
        false,
        false,
        false,
        false,
        false,
        true,
        false
    );
    return true;
}

bool RendererRayTracingSystem::renderSoftTransparentShadowFold(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool graphEntryStatesOwned
){
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softGroupsX = DivideUp(softHalfWidth, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    const u32 softGroupsY = DivideUp(softHalfHeight, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE));
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        false,
        true
    );
    return true;
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftTemporalMergeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const transparentResolveTiming,
    const bool* const opaqueProduced,
    bool* const transparentTraceProduced,
    const u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsTransparentTemporalMergeEntryStates
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTemporalMergeGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsTransparentTemporalMergeEntryStates = graphOwnsTransparentTemporalMergeEntryStates,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const transparentResolveTiming,
    const bool* const opaqueProduced,
    bool* const transparentTraceProduced,
    const u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsTransparentWaveletInputBoundary,
    const bool startsTransparentResolveTiming,
    const bool combinedWavelet
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFirstWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsTransparentWaveletInputBoundary = graphOwnsTransparentWaveletInputBoundary,
            .startsTransparentResolveTiming = startsTransparentResolveTiming,
            .combinedWavelet = combinedWavelet,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftFoldTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>* const transparentResolveTiming,
    const bool* const opaqueProduced,
    bool* const transparentTraceProduced,
    const u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool combinedUpsample){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftFoldGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .transparentResolveTiming = transparentResolveTiming,
            .opaqueProduced = opaqueProduced,
            .transparentTraceProduced = transparentTraceProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .combinedUpsample = combinedUpsample,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowTransparentSoftTraceTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool* const opaqueProduced,
    const u32* const opaqueFrameIndex,
    bool* const transparentTraceProduced,
    const bool graphEntryStatesOwned,
    const LightSpaceShadowSnapshot* const lightSpace
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowTransparentSoftTraceGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = &timingTicket,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .transparentTraceProduced = transparentTraceProduced,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool* const prepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAllLitVisibilityClear,
    const GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .prepared = prepared,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsAllLitVisibilityClear = graphOwnsAllLitVisibilityClear,
            .graphOwnedAdaptivePlan = graphOwnedAdaptivePlan,
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

