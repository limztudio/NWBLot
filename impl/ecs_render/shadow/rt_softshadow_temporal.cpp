// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/shadow_subsystem.adhd.h>
#include <impl/ecs_render/shadow/shadow_pipeline_set.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(
        !resolve.m_combinedTemporal.m_pipeline
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !m_rayTracingState.m_softShadowTemporalReady || !m_rayTracingState.m_softTransparentTemporalReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    const b8 frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    const Array<Core::Texture*, 4> outputs = {
        frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
        frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
        frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
        frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
    };
    // This graph-only pass inherits both raw inputs and all four selected history/moment output states.
    for(Core::Texture* const output : outputs)
        commandList.setEnableUavBarriersForTexture(output, true);
    commandList.commitBarriers();

    const DeferredBindlessFrameResources& bindless = targets.bindless;
    const ShadowCombinedTemporalPushConstants push = MakeShadowCombinedTemporalPushConstants(
        LoadFloat(m_rayTracingState.m_prevWorldToClip),
        targets.shadowReceiverFactor,
        ResolveSoftShadowLightSlotRangeCount(m_rayTracingState.m_softShadowSlotMask),
        softShadowTemporalHistoryUsable(),
        bindless.shadowSoftGeometry.slot(),
        bindless.shadowSoftGeometryPrev.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.shadowSoftHalfA.slot(),
        frontIsA ? bindless.shadowHistA.slot() : bindless.shadowHistB.slot(),
        frontIsA ? bindless.shadowMomentsA.slot() : bindless.shadowMomentsB.slot(),
        frontIsA ? bindless.shadowHistBStorage.slot() : bindless.shadowHistAStorage.slot(),
        frontIsA ? bindless.shadowMomentsBStorage.slot() : bindless.shadowMomentsAStorage.slot(),
        bindless.transparentSoftHalf.slot(),
        frontIsA ? bindless.transparentHistA.slot() : bindless.transparentHistB.slot(),
        frontIsA ? bindless.transparentMomentsA.slot() : bindless.transparentMomentsB.slot(),
        frontIsA ? bindless.transparentHistBStorage.slot() : bindless.transparentHistAStorage.slot(),
        frontIsA ? bindless.transparentMomentsBStorage.slot() : bindless.transparentMomentsAStorage.slot()
    );
    Core::ComputeState state;
    state.setPipeline(resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setComputeState(state);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resolve.m_combinedTemporal.m_pipeline.get());
    commandList.setPushConstants(&push, sizeof(push));
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_ShadowCombinedTemporal, m_graphics.getDevice(), commandList);

    commandList.dispatch(
        DivideUp(DivideUp(targets.width, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        DivideUp(DivideUp(targets.height, targets.shadowReceiverFactor), static_cast<u32>(NWB_SHADOW_REPROJECT_MERGE_GROUP_SIZE)),
        1u
    );
    m_rayTracingState.m_transparentShadowSamplingHistory.record(m_rayTracingState.m_softShadowSlotMask);
    return true;
}

b8 ShadowPipelineSet::ensureSoftCombinedTemporalPipeline(){
    auto& resolve = m_shadow.m_softShadowResolve;
    if(resolve.m_combinedTemporal.m_pipeline)
        return true;
    if(resolve.m_combinedTemporalFailed)
        return false;
    if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &ShadowSubsystem::shadowReprojectMergeCombinedCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowTemporal",
        sizeof(ShadowCombinedTemporalPushConstants),
        false,
        resolve.m_combinedTemporalBindingLayout,
        resolve.m_combinedTemporal.m_shader,
        resolve.m_combinedTemporal.m_pipeline
    )){
        resolve.m_combinedTemporalFailed = true;
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

