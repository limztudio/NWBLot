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


Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityOpaqueTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool* const prepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    bool* const opaqueProduced,
    u32* const opaqueFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const LightSpaceShadowSnapshot* const lightSpace
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .lightSpace = lightSpace ? *lightSpace : LightSpaceShadowSnapshot{},
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .prepared = prepared,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
        }
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityOpaqueFirstWaveletTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>* const opaqueResolveTiming,
    bool* const opaqueProduced,
    const u32* const opaqueFrameIndex,
    const bool hardwareShadowSupported,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const bool deferUpsample,
    const bool deferWavelet){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueFirstWaveletGraphTask::Payload{
            .raytracingSystem = this,
            .graphics = &m_graphics,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
            .graphOwnsOpaqueTemporalMergeEntryStates = graphOwnsOpaqueTemporalMergeEntryStates,
            .deferUpsample = deferUpsample,
            .deferWavelet = deferWavelet,
        }
    );
}

bool RendererRayTracingSystem::renderShadowVisibilityOpaque(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    u32& outFrameIndex,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates
){
    outFrameIndex = 0u;
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;
    return renderShadowVisibility(
        commandList,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        true,
        &outFrameIndex,
        graphOwnsOpaqueTemporalMergeEntryStates,
        true
    );
}

Core::GpuTaskId RendererRayTracingSystem::declareShadowVisibilityOpaqueResolveTailTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>* const asyncTiming,
    Optional<Core::GpuTimingMeasure>* const shadowVisibilityTiming,
    Optional<Core::GpuTimingMeasure>* const opaqueResolveTiming,
    bool* const opaqueProduced,
    const u32* const opaqueFrameIndex,
    const bool hardwareShadowSupported,
    const bool graphEntryStatesOwned
){
    return graph.addTask<RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask>(
        desc,
        RayTracingShadowVisibilityTaskDetail::ShadowVisibilityOpaqueResolveTailGraphTask::Payload{
            .raytracingSystem = this,
            .targets = &targets,
            .deferredLightingResources = deferredLightingResources,
            .timingTicket = &timingTicket,
            .asyncTiming = asyncTiming,
            .shadowVisibilityTiming = shadowVisibilityTiming,
            .opaqueResolveTiming = opaqueResolveTiming,
            .opaqueProduced = opaqueProduced,
            .opaqueFrameIndex = opaqueFrameIndex,
            .hardwareShadowSupported = hardwareShadowSupported,
            .graphEntryStatesOwned = graphEntryStatesOwned,
        }
    );
}

bool RendererRayTracingSystem::renderSoftOpaqueShadowResolvePhase(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool hardwareShadowSupported,
    const bool graphEntryStatesOwned,
    const bool graphOwnsOpaqueTemporalMergeEntryStates,
    const SoftShadowOpaqueResolvePhase::Enum phase){
    if(
        !m_rayTracingState.m_softShadowReady
        || !m_rayTracingState.m_softTransparentReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
        || (phase != SoftShadowOpaqueResolvePhase::TemporalAndWavelet && !m_rayTracingState.m_softShadowTemporalReady)
    )
        return false;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 groupSize = hardwareShadowSupported
        ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)
        : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)
    ;
    const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
    const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);
    dispatchSoftShadowDenoiseAndTransparentFold(
        commandList,
        targets,
        deferredLightingResources,
        frameIndex,
        softGroupsX,
        softGroupsY,
        graphEntryStatesOwned,
        false,
        true,
        false,
        false,
        true,
        false,
        false,
        graphOwnsOpaqueTemporalMergeEntryStates,
        false,
        false,
        true,
        false,
        false,
        false,
        phase
    );
    return true;
}

bool RendererRayTracingSystem::renderSoftOpaqueShadowResolveTail(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const u32 frameIndex,
    const bool hardwareShadowSupported,
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
    const u32 groupSize = hardwareShadowSupported
        ? static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)
        : static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)
    ;
    const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
    const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);
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
        true,
        false,
        false,
        false,
        false,
        true,
        false
    );
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

