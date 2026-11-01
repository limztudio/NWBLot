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


namespace __hidden_timing_render_pass{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_InFlightRanges = 32u;

[[nodiscard]] b8 PrepareTimingProbeScopes(Core::GraphicsRuntime& graphics, const Span<const Name> scopes, const TStringView probeName){
    auto& device = graphics.getDevice();
    for(const Name& scope : scopes){
        if(!graphics.gpuTiming().prepareScopeQueries(scope, device, s_InFlightRanges)){
            NWB_LOGGER_ERROR(NWB_TEXT("{}: failed to prepare scope '{}'"), probeName, ConvertString(scope.resolvedText()));
            return false;
        }
    }
    return true;
}

void RegisterTimingProbeRenderPass(Core::GraphicsRuntime& graphics, Core::IRenderPass& pass, const TStringView probeName){
    graphics.addRenderPassToBack(pass);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: in-flight ranges {}"), probeName, s_InFlightRanges);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: render unfocused 1"), probeName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitTimingRenderPass::AvboitTimingRenderPass(Core::GraphicsRuntime& graphics)noexcept
    : IRenderPass(graphics)
{}

AvboitTimingRenderPass::~AvboitTimingRenderPass()noexcept{
    stop();
}

b8 AvboitTimingRenderPass::start(const b8 includeCaustics){
    if(m_registered)
        return m_includeCaustics == includeCaustics;
    constexpr TStringView s_ProbeName = NWB_TEXT("AvboitTimingProbe");
    const Array<Name, 24> scopes = {
        Impl::RendererGpuTimingScope::s_Frame.identity,
        Impl::RendererGpuTimingScope::s_OpaqueRegular.identity,
        Impl::RendererGpuTimingScope::s_ShadowVisibility.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowViews.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowOpaqueCapture.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowTransparentCapture.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowShade.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowMapOpaque.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowMapTransparent.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowFallbackOpaque.identity,
        Impl::RendererGpuTimingScope::s_LightSpaceShadowFallbackTransparent.identity,
        Impl::RendererGpuTimingScope::s_DeferredLighting.identity,
        Impl::RendererGpuTimingScope::s_DeferredComposite.identity,
        Impl::RendererGpuTimingScope::s_DeferredPresent.identity,
        Impl::RendererGpuTimingScope::s_AvboitClear.identity,
        Impl::RendererGpuTimingScope::s_AvboitOccupancy.identity,
        Impl::RendererGpuTimingScope::s_AvboitDepthWarp.identity,
        Impl::RendererGpuTimingScope::s_AvboitExtinction.identity,
        Impl::RendererGpuTimingScope::s_AvboitIntegration.identity,
        Impl::RendererGpuTimingScope::s_AvboitAccumulate.identity,
        Impl::RendererGpuTimingScope::s_RefractionResolve.identity,
        Impl::RendererGpuTimingScope::s_RefractionClassifyTiles.identity,
        Impl::RendererGpuTimingScope::s_RefractionClearTileArguments.identity,
        Impl::RendererGpuTimingScope::s_OpticalBoundsFinalize.identity,
    };
    auto& graphics = getGraphics();
    if(!__hidden_timing_render_pass::PrepareTimingProbeScopes(graphics, scopes, s_ProbeName))
        return false;
    if(includeCaustics){
        const Array<Name, 2> causticScopes = { Impl::RendererGpuTimingScope::s_CausticPhotons.identity, Impl::RendererGpuTimingScope::s_CausticResolve.identity, };
        if(!__hidden_timing_render_pass::PrepareTimingProbeScopes(graphics, causticScopes, s_ProbeName))
            return false;
    }
    __hidden_timing_render_pass::RegisterTimingProbeRenderPass(graphics, *this, s_ProbeName);
    m_registered = true;
    m_includeCaustics = includeCaustics;
    if(includeCaustics)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AvboitTimingProbe: caustic in-flight ranges {}"), __hidden_timing_render_pass::s_InFlightRanges);
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


ShadowTimingRenderPass::ShadowTimingRenderPass(Core::GraphicsRuntime& graphics)noexcept
    : IRenderPass(graphics)
{}

ShadowTimingRenderPass::~ShadowTimingRenderPass()noexcept{
    stop();
}

b8 ShadowTimingRenderPass::start(){
    if(m_registered)
        return true;
    constexpr TStringView s_ProbeName = NWB_TEXT("ShadowTimingProbe");
    const Array<Name, 13> scopes = {
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
    if(!__hidden_timing_render_pass::PrepareTimingProbeScopes(graphics, scopes, s_ProbeName))
        return false;
    __hidden_timing_render_pass::RegisterTimingProbeRenderPass(graphics, *this, s_ProbeName);
    m_registered = true;
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

