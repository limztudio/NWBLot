// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/assets/graphics/caustic/photon_push_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(CausticPhotonPushConstants) == sizeof(u32) * 15u, "CausticPhotonPushConstants must match the shader push-constant layout");

[[nodiscard]] inline CausticPhotonPushConstants BuildCausticPhotonPushConstants(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const RendererRayTracingState& rayTracingState,
    const CausticPhotonBudget& photonBudget,
    const u32 instanceCount,
    const u32 frameIndex,
    const u32 temporalPhaseCount
)noexcept{
    return ResolveCausticPhotonPushConstants(
        targets.width,
        targets.height,
        instanceCount,
        photonBudget,
        rayTracingState.m_causticRefractiveInstanceCount,
        frameIndex,
        targets.bindless.gbufferDepth.slot(),
        targets.bindless.gbufferWorldPosition.slot(),
        rayTracingState.m_causticEmissionTargetHeapHandle.slot(),
        meshView.heapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        targets.bindless.causticAccumulatorStorage.slot(),
        temporalPhaseCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
