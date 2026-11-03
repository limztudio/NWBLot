// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowTerminalUpsample(Core::CommandList& commandList, DeferredFrameTargets& targets, const DeferredLightingGraphResources& deferredLightingResources, const b8 transparentReady){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    const Core::ComputePipelineHandle& pipeline = transparentReady ? resolve.m_combinedUpsample.m_pipeline : resolve.m_scalar.m_upsample.m_pipeline;
    if(
        !pipeline
        || !m_rayTracingState.m_softShadowReady
        || m_rayTracingState.m_softShadowSlotMask == 0u
        || NWB_SHADOW_RESOLVE_PASS_COUNT != 1u
        || NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT != 1u
        || !deferredLightingResources.valid()
        || !targets.bindless.valid()
        || !targets.depth
        || !targets.worldPosition
        || !targets.normal
        || !targets.shadowSoftHalfB
        || !targets.shadowSoftGeometry
        || !targets.shadowVisibility
        || (transparentReady && (!m_rayTracingState.m_softTransparentReady || !targets.shadowSoftHalfA))
    )
        return false;

    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowResolvePushConstants push = MakeShadowTerminalUpsamplePushConstants(
        targets.width,
        targets.height,
        DivideUp(targets.width, static_cast<u32>(targets.shadowReceiverFactor)),
        DivideUp(targets.height, static_cast<u32>(targets.shadowReceiverFactor)),
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        bindless.shadowSoftGeometry.slot(),
        bindless.gbufferDepth.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.gbufferNormal.slot(),
        transparentReady ? bindless.shadowSoftHalfA.slot() : bindless.shadowSoftHalfB.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.shadowVisibilityStorage.slot(),
        bindless.sceneShading.slot(),
        bindless.shadowSoftHalfB.slot(),
        targets.shadowReceiverFactor
    );
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE)), 1u);
    // The original terminal fold is the only successful history-advance endpoint. Opaque recovery does not advance it.
    if(transparentReady && m_rayTracingState.m_softShadowTemporalReady)
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = true;
    return true;
}

b8 RendererRayTracingSystem::ensureSoftCombinedUpsamplePipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_combinedUpsample.m_pipeline)
        return true;
    if(resolve.m_combinedUpsampleFailed || !resolve.m_bindingLayout)
        return false;
    auto& device = m_graphics.getDevice();
    if(
        !m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::shadowResolveCombinedCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_CombinedShadowUpsample", resolve.m_combinedUpsample.m_shader)
        || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, resolve.m_combinedUpsample.m_shader, resolve.m_bindingLayout, false, resolve.m_combinedUpsample.m_pipeline)
    ){
        resolve.m_combinedUpsampleFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

