// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "quality_settings.h"

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::setShadowQualitySettings(const ShadowQualitySettings& settings){
    if(!ValidateShadowQualitySettings(settings))
        return false;
    if(m_shadowQualitySettings.transparentSampling == settings.transparentSampling)
        return true;
    m_shadowQualitySettings = settings;
    m_temporalOneShadowSamplingLogged = false;
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    m_rayTracingState.m_softShadowTemporalSeeded = false;
    m_rayTracingState.m_softShadowTemporalHistoryAdvancePending = false;
    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    return true;
}

u32 RendererRayTracingSystem::transparentShadowSampleCount()const noexcept{
    const bool historyUsable = m_rayTracingState.m_softTransparentTemporalReady && softShadowTemporalHistoryUsable()
        && m_rayTracingState.m_transparentShadowSamplingHistory.covers(m_rayTracingState.m_softShadowSlotMask);
    return ResolveTransparentShadowSampleCount(m_shadowQualitySettings, historyUsable);
}

void RendererRayTracingSystem::reportTransparentShadowSampling(const u32 sampleCount){
    if(sampleCount != 1u || m_temporalOneShadowSamplingLogged)
        return;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: recorded temporal-one transparent shadow sampling samples={} hardware={}")
        , sampleCount
        , hardwareTransparentShadowReady() ? 1u : 0u
    );
    m_temporalOneShadowSamplingLogged = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

