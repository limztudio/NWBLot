// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_timing_render_pass.h"

#include <impl/ecs_render/kernel/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowTimingRenderPass::ShadowTimingRenderPass(Core::GraphicsRuntime& graphics)
    : IRenderPass(graphics){}

ShadowTimingRenderPass::~ShadowTimingRenderPass(){
    stop();
}

bool ShadowTimingRenderPass::start(){
    if(m_registered)
        return true;
    constexpr u32 s_InFlightRanges = 32u;
    const Name scopes[] = {
        Impl::RendererGpuTimingScope::s_Frame.identity,
        Impl::RendererGpuTimingScope::s_OpaqueRegular.identity,
        Impl::RendererGpuTimingScope::s_DeferredLighting.identity,
        Impl::RendererGpuTimingScope::s_DeferredComposite.identity,
        Impl::RendererGpuTimingScope::s_DeferredPresent.identity,
        Impl::RendererGpuTimingScope::s_ShadowVisibility.identity,
        Impl::RendererGpuTimingScope::s_ShadowOpaqueTrace.identity,
        Impl::RendererGpuTimingScope::s_ShadowGeometryDownsample.identity,
        Impl::RendererGpuTimingScope::s_ShadowOpaqueTemporal.identity,
        Impl::RendererGpuTimingScope::s_ShadowOpaqueResolve.identity,
        Impl::RendererGpuTimingScope::s_ShadowTransparentTrace.identity,
        Impl::RendererGpuTimingScope::s_ShadowTransparentTemporal.identity,
        Impl::RendererGpuTimingScope::s_ShadowTransparentResolve.identity,
    };
    auto& graphics = getGraphics();
    auto& device = graphics.getDevice();
    for(const Name& scope : scopes){
        if(!graphics.gpuTiming().prepareScopeQueries(scope, device, s_InFlightRanges)){
            NWB_LOGGER_ERROR(NWB_TEXT("ShadowTimingProbe: failed to prepare scope '{}'"), StringConvert(scope.c_str()));
            return false;
        }
    }
    graphics.addRenderPassToBack(*this);
    m_registered = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: in-flight ranges {}"), s_InFlightRanges);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ShadowTimingProbe: render unfocused 1"));
    return true;
}

void ShadowTimingRenderPass::stop(){
    if(!m_registered)
        return;
    getGraphics().removeRenderPass(*this);
    m_registered = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

