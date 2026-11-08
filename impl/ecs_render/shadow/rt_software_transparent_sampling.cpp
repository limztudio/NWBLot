// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/shadow/quality_settings.adhd.h>

#include <impl/ecs_render/shadow/shadow_pipeline_set.h>
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
    ApplyShadowQualitySettingsChange(
        settings,
        m_shadowQualitySettings,
        m_temporalOneShadowSamplingLogged,
        m_rayTracingState.m_transparentShadowSamplingHistory,
        m_rayTracingState.m_softShadowTemporalSeeded,
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending
    );
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

b8 ShadowPipelineSet::ensureSoftwareTransparentSamplingPipeline(){
    auto& sampling = m_shadow.m_softwareTransparentSampling;
    if(sampling.m_pipelines[0] && sampling.m_pipelines[1])
        return true;
    auto& device = m_graphics.getDevice();
    const Array<AStringView, 2u> variants = { s_SwTransparentAdaptiveDisabledVariant, s_SwTransparentAdaptiveEnabledVariant };
    for(u32 index = 0u; index < variants.size(); ++index){
        if(!RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_arena,
            device,
            m_shaderSystem,
            &ShadowSubsystem::swShadowTransparentSoftCs,
            variants[index],
            Name("ECSRender_SwShadowTransparentSoft"),
            sizeof(SoftwareTransparentSamplingPush),
            false,
            sampling.m_layout,
            sampling.m_shaders[index],
            sampling.m_pipelines[index]
        ))
            return false;
    }
    return true;
}

u32 RendererRayTracingSystem::transparentShadowSampleCount()const noexcept{
    const b8 historyUsable = m_rayTracingState.m_softTransparentTemporalReady && softShadowTemporalHistoryUsable() && m_rayTracingState.m_transparentShadowSamplingHistory.covers(m_rayTracingState.m_softShadowSlotMask);
    return ResolveTransparentShadowSampleCount(m_shadowQualitySettings, historyUsable);
}

void RendererRayTracingSystem::reportTransparentShadowSampling(const u32 sampleCount){
    LogTransparentShadowSamplingIfNeeded(sampleCount, hardwareTransparentShadowReady(), m_temporalOneShadowSamplingLogged);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

