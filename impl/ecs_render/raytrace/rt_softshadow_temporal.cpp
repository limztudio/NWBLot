// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "soft_shadow_temporal.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureSoftCombinedTemporalPipeline(){
    auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(resolve.m_combinedTemporal.m_pipeline)
        return true;
    if(resolve.m_combinedTemporalFailed)
        return false;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!resolve.m_combinedTemporalBindingLayout){
        Core::BindingLayoutDesc layout(m_arena);
        layout.setVisibility(Core::ShaderType::Compute);
        layout.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(ShadowCombinedTemporalPushConstants)));
        resolve.m_combinedTemporalBindingLayout = device.createBindingLayout(layout);
        if(!resolve.m_combinedTemporalBindingLayout){
            resolve.m_combinedTemporalFailed = true;
            return false;
        }
    }
    if(!m_shaderSystem.loadShader<ComputeShader>(
        resolve.m_combinedTemporal.m_shader,
        AssetsGraphicsShadow::s_SoftReprojectMergeCombinedShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_CombinedShadowTemporal"
    )){
        resolve.m_combinedTemporalFailed = true;
        return false;
    }
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(resolve.m_combinedTemporal.m_shader)
        .addBindingLayout(resolve.m_combinedTemporalBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    resolve.m_combinedTemporal.m_pipeline = device.createComputePipeline(desc);
    resolve.m_combinedTemporalFailed = !resolve.m_combinedTemporal.m_pipeline;
    return !resolve.m_combinedTemporalFailed;
}

bool RendererRayTracingSystem::renderSoftShadowCombinedTemporalMerge(Core::CommandList& commandList, DeferredFrameTargets& targets){
    const auto& resolve = m_rayTracingState.m_softShadowResolve;
    if(
        !resolve.m_combinedTemporal.m_pipeline
        || !m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady
        || !m_rayTracingState.m_softShadowTemporalReady || !m_rayTracingState.m_softTransparentTemporalReady
        || !targets.bindless.valid() || m_rayTracingState.m_softShadowSlotMask == 0u
    )
        return false;

    const bool frontIsA = m_rayTracingState.m_softShadowHistoryFrontIsA != 0u;
    Core::Texture* const outputs[] = {
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
    ShadowCombinedTemporalPushConstants push;
    push.prevWorldToClip = m_rayTracingState.m_prevWorldToClip;
    push.receiverFactor = targets.shadowReceiverFactor;
    for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
        if((m_rayTracingState.m_softShadowSlotMask & (1u << slot)) != 0u)
            push.lightSlotCount = slot + 1u;
    }
    push.historyValid = softShadowTemporalHistoryUsable() ? 1u : 0u;
    push.geometryCurrSlot = bindless.shadowSoftGeometry.slot();
    push.geometryPrevSlot = bindless.shadowSoftGeometryPrev.slot();
    push.worldPositionSlot = bindless.gbufferWorldPosition.slot();
    push.opaqueSoftTraceSlot = bindless.shadowSoftHalfA.slot();
    push.opaqueHistoryInSlot = frontIsA ? bindless.shadowHistA.slot() : bindless.shadowHistB.slot();
    push.opaqueMomentsInSlot = frontIsA ? bindless.shadowMomentsA.slot() : bindless.shadowMomentsB.slot();
    push.opaqueHistoryOutSlot = frontIsA ? bindless.shadowHistBStorage.slot() : bindless.shadowHistAStorage.slot();
    push.opaqueMomentsOutSlot = frontIsA ? bindless.shadowMomentsBStorage.slot() : bindless.shadowMomentsAStorage.slot();
    push.transparentSoftTraceSlot = bindless.transparentSoftHalf.slot();
    push.transparentHistoryInSlot = frontIsA ? bindless.transparentHistA.slot() : bindless.transparentHistB.slot();
    push.transparentMomentsInSlot = frontIsA ? bindless.transparentMomentsA.slot() : bindless.transparentMomentsB.slot();
    push.transparentHistoryOutSlot = frontIsA ? bindless.transparentHistBStorage.slot() : bindless.transparentHistAStorage.slot();
    push.transparentMomentsOutSlot = frontIsA ? bindless.transparentMomentsBStorage.slot() : bindless.transparentMomentsAStorage.slot();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

