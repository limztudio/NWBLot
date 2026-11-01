// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/quality_settings.adhd.h>

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::setShadowQualitySettings(const ShadowQualitySettings& settings)noexcept{
    if(!ValidateShadowQualitySettings(settings))
        return false;
    if(SameShadowQualitySettings(m_shadowQualitySettings, settings))
        return true;
    m_shadowQualitySettings = settings;
    m_temporalOneShadowSamplingLogged = false;
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    m_rayTracingState.m_softShadowTemporalSeeded = false;
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

b8 RendererRayTracingSystem::ensureSoftwareTransparentSamplingPipeline(){
    auto& sampling = m_rayTracingState.m_softwareTransparentSampling;
    if(sampling.m_pipelines[0] && sampling.m_pipelines[1])
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    if(!heap.isInitialized())
        return false;
    if(!sampling.m_layout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(SoftwareTransparentSamplingPush)));
        auto layoutResult = device.createBindingLayout(desc);
        if(layoutResult)
            sampling.m_layout = Move(*layoutResult);
        else
            sampling.m_layout.reset();
        if(!layoutResult)
            return false;
    }
    const Array<AStringView, 2u> variants = { "NWB_SW_TRANSPARENT_ADAPTIVE=0", "NWB_SW_TRANSPARENT_ADAPTIVE=1" };
    for(u32 index = 0u; index < variants.size(); ++index){
        if(sampling.m_pipelines[index])
            continue;
        if(!m_shaderSystem.loadShader<ComputeShader>(&ShadowSubsystem::swShadowTransparentSoftCs, variants[index], "ECSRender_SwShadowTransparentSoft", sampling.m_shaders[index]))
            return false;
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(sampling.m_shaders[index]).addBindingLayout(sampling.m_layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        auto pipelinesResult = device.createComputePipeline(desc);
        if(pipelinesResult)
            sampling.m_pipelines[index] = Move(*pipelinesResult);
        else
            sampling.m_pipelines[index].reset();
        if(!pipelinesResult)
            return false;
    }
    return true;
}

u32 RendererRayTracingSystem::transparentShadowSampleCount()const noexcept{
    const b8 historyUsable = m_rayTracingState.m_softTransparentTemporalReady && softShadowTemporalHistoryUsable() && m_rayTracingState.m_transparentShadowSamplingHistory.covers(m_rayTracingState.m_softShadowSlotMask);
    return ResolveTransparentShadowSampleCount(m_shadowQualitySettings, historyUsable);
}

void RendererRayTracingSystem::reportTransparentShadowSampling(const u32 sampleCount){
    if(sampleCount != 1u || m_temporalOneShadowSamplingLogged)
        return;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: recorded temporal-one transparent shadow sampling samples={} hardware={}"), sampleCount, hardwareTransparentShadowReady() ? 1u : 0u);
    m_temporalOneShadowSamplingLogged = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

