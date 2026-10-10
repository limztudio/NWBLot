// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_photon_parameters.h"

#include <impl/ecs_render/caustic/settings.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CausticPhotonPushConstants BuildCausticPhotonPushConstants(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const RendererRayTracingState& rayTracingState,
    const CausticPhotonBudget& photonBudget,
    const u32 instanceCount,
    const u32 frameIndex,
    const u32 temporalPhaseCount
)noexcept{
    CausticPhotonPushConstants pushConstants;
    pushConstants.width = targets.width;
    pushConstants.height = targets.height;
    pushConstants.instanceCount = instanceCount;
    pushConstants.photonCount = photonBudget.photonsPerFrame;
    pushConstants.emissionTargetCount = rayTracingState.m_causticRefractiveInstanceCount;
    pushConstants.gridSide = photonBudget.gridSide;
    // Both producers use the graphics frame clock so preparation skips cannot shift temporal sampling phases.
    pushConstants.frameIndex = frameIndex;
    pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
    pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    pushConstants.emissionTargetSlot = rayTracingState.m_causticEmissionTargetHeapHandle.slot();
    pushConstants.viewSlot = meshView.heapHandle.slot();
    pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
    pushConstants.materialContextSlotsHeapSlot = rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
    pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
    pushConstants.temporalPhaseCount = temporalPhaseCount;
    return pushConstants;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

