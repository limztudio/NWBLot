// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_raster_configuration.h"

#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/avboit/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_raster_configuration{


static Core::BlendState::RenderTarget BuildAdditiveBlendTarget(const Core::ColorMask::Mask colorWriteMask = Core::ColorMask::All)noexcept{
    Core::BlendState::RenderTarget target;
    target
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::One)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::One)
        .setBlendOpAlpha(Core::BlendOp::Add)
        .setColorWriteMask(colorWriteMask)
    ;
    return target;
}



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum AvboitRasterConfiguration::SelectRendererAvboitAccumColorFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 2> s_Candidates = { Core::Format::RGBA16_FLOAT, Core::Format::RGBA8_UNORM, };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::Format::Enum AvboitRasterConfiguration::SelectRendererAvboitAccumExtinctionFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 5> s_Candidates = { Core::Format::R16_FLOAT, Core::Format::R32_FLOAT, Core::Format::RGBA16_FLOAT, Core::Format::R8_UNORM, Core::Format::RGBA8_UNORM, };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::Format::Enum AvboitRasterConfiguration::SelectRendererAvboitTransmittanceFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 1> s_Candidates = { NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT, };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderSample | Core::FormatSupport::ShaderUavStore;

    return ECSRenderDetail::SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::Format::Enum AvboitRasterConfiguration::SelectRendererAvboitLowRasterFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 2> s_Candidates = { Core::Format::R8_UNORM, Core::Format::RGBA8_UNORM, };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return ECSRenderDetail::SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::RenderState AvboitRasterConfiguration::BuildRendererAvboitVoxelRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState.targets[0].setColorWriteMask(Core::ColorMask::None);
    return renderState;
}

Core::RenderState AvboitRasterConfiguration::BuildRendererAvboitAccumulateRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().disableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState
        .setRenderTarget(NWB_AVBOIT_ACCUM_COLOR_LOCATION, __hidden_avboit_raster_configuration::BuildAdditiveBlendTarget())
        .setRenderTarget(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION, __hidden_avboit_raster_configuration::BuildAdditiveBlendTarget(Core::ColorMask::Red))
        .setRenderTarget(NWB_AVBOIT_ACCUM_FOREGROUND_COLOR_LOCATION, __hidden_avboit_raster_configuration::BuildAdditiveBlendTarget())
        .setRenderTarget(NWB_AVBOIT_ACCUM_FOREGROUND_EXTINCTION_LOCATION, __hidden_avboit_raster_configuration::BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

Core::RenderState AvboitRasterConfiguration::BuildRendererAvboitRefractionCaptureRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip().setCullBack();
    // Nearest fragment wins; no attachment blends.
    return renderState;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

