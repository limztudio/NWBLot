// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_transparent_shadow_trace_recorder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SoftwareTransparentShadowTraceRecorder::Record(const SoftwareTransparentShadowTraceInputs& inputs, const RendererRayTracingState& state, Core::CommandList& commandList, DeferredFrameTargets& targets, Core::GpuDescriptorHeap& heap){
    // The software route is retained for devices without hardware ray queries.
    if(!inputs.graphOwnsOpaqueToTransparentBoundary){
        commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    }
    commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
    if(!inputs.graphOwnsOpaqueToTransparentBoundary)
        commandList.commitBarriers();

    const b8 samplingHistoryUsable = inputs.temporalHistoryReadable && state.m_softTransparentTemporalReady && state.m_softwareTransparentSampling.m_history.usable();
    if(samplingHistoryUsable && !inputs.graphOwnsOpaqueToTransparentBoundary){
        // These reads also belong to the split trace task; unsplit phases retain local transitions.
        commandList.setTextureState(targets.shadowSoftGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.shadowSoftGeometryPrev.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(inputs.historyFrontIsA ? targets.transparentMomentsA.get() : targets.transparentMomentsB.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }
    const SoftwareTransparentSamplingPush tracePush = MakeSoftwareTransparentSamplingPush(
        Span<const f32>(state.m_prevWorldToClip.raw, 16u),
        targets.width,
        targets.height,
        state.m_sceneBvhInstanceCount,
        inputs.frameIndex,
        inputs.sampleCount,
        targets.bindless.slotsBufferDescriptor.slot(),
        state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        targets.bindless.transparentSoftHalfStorage.slot(),
        samplingHistoryUsable,
        targets.bindless.shadowSoftGeometry.slot(),
        targets.bindless.shadowSoftGeometryPrev.slot(),
        inputs.historyFrontIsA ? targets.bindless.transparentMomentsA.slot() : targets.bindless.transparentMomentsB.slot(),
        targets.shadowReceiverFactor
    );
    const auto& pipeline = state.m_softwareTransparentSampling.m_pipelines[samplingHistoryUsable ? 1u : 0u];
    commandList.setComputeState(RayTracingDetail::MakeComputePassState(pipeline));
    heap.bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&tracePush, sizeof(tracePush));
    commandList.dispatch(inputs.groupsX, inputs.groupsY, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

