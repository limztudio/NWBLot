// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_timing_render_pass.h"

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitTimingRenderPass::AvboitTimingRenderPass(Core::GraphicsRuntime& graphics)
    : IRenderPass(graphics)
{}

AvboitTimingRenderPass::~AvboitTimingRenderPass(){
    stop();
}

bool AvboitTimingRenderPass::start(const bool includeCaustics){
    if(m_registered)
        return m_includeCaustics == includeCaustics;
    constexpr u32 s_InFlightRanges = 32u;
    const Name scopes[] = {
        Impl::RendererGpuTimingScope::s_Frame.identity,
        Impl::RendererGpuTimingScope::s_OpaqueRegular.identity,
        Impl::RendererGpuTimingScope::s_ShadowVisibility.identity,
        Impl::RendererGpuTimingScope::s_DeferredLighting.identity,
        Impl::RendererGpuTimingScope::s_DeferredComposite.identity,
        Impl::RendererGpuTimingScope::s_DeferredPresent.identity,
        Impl::RendererGpuTimingScope::s_AvboitClear.identity,
        Impl::RendererGpuTimingScope::s_AvboitOccupancy.identity,
        Impl::RendererGpuTimingScope::s_AvboitDepthWarp.identity,
        Impl::RendererGpuTimingScope::s_AvboitExtinction.identity,
        Impl::RendererGpuTimingScope::s_AvboitIntegration.identity,
        Impl::RendererGpuTimingScope::s_AvboitAccumulate.identity,
    };
    auto& graphics = getGraphics();
    auto& device = graphics.getDevice();
    for(const Name& scope : scopes){
        if(!graphics.gpuTiming().prepareScopeQueries(scope, device, s_InFlightRanges)){
            NWB_LOGGER_ERROR(NWB_TEXT("AvboitTimingProbe: failed to prepare scope '{}'"), StringConvert(scope.c_str()));
            return false;
        }
    }
    if(includeCaustics){
        const Name causticScopes[] = {
            Impl::RendererGpuTimingScope::s_CausticPhotons.identity,
            Impl::RendererGpuTimingScope::s_CausticResolve.identity,
        };
        for(const Name& scope : causticScopes){
            if(!graphics.gpuTiming().prepareScopeQueries(scope, device, s_InFlightRanges)){
                NWB_LOGGER_ERROR(NWB_TEXT("AvboitTimingProbe: failed to prepare scope '{}'"), StringConvert(scope.c_str()));
                return false;
            }
        }
    }
    graphics.addRenderPassToBack(*this);
    m_registered = true;
    m_includeCaustics = includeCaustics;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitTimingProbe: in-flight ranges {}"), s_InFlightRanges);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitTimingProbe: render unfocused 1"));
    if(includeCaustics)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitTimingProbe: caustic in-flight ranges {}"), s_InFlightRanges);
    return true;
}

void AvboitTimingRenderPass::stop(){
    if(!m_registered)
        return;
    getGraphics().removeRenderPass(*this);
    m_registered = false;
    m_includeCaustics = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

