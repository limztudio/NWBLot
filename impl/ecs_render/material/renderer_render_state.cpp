// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_render_state_private.h"
#include <impl/ecs_render/avboit/avboit_raster_configuration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::RenderState BuildMeshRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip();
    return renderState;
}

Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass, const b8 twoSided)noexcept{
    auto applyTwoSided = [&](Core::RenderState renderState)noexcept{
        if(twoSided)
            renderState.rasterState.setCullNone();
        return renderState;
    };

    switch(pass){
    case MaterialPipelinePass::Opaque:
        return applyTwoSided(BuildMeshRenderState());
    case MaterialPipelinePass::CsgReceiverSurface:{
        Core::RenderState renderState;
        renderState.depthStencilState.enableDepthTest().disableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
        Core::BlendState::RenderTarget target;
        target.setColorWriteMask(Core::ColorMask::None);
        for(u32 index = 0u; index < Core::s_MaxRenderTargets; ++index)
            renderState.blendState.setRenderTarget(index, target);
        renderState.rasterState.enableDepthClip().setCullNone();
        return renderState;
    }
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitVoxelRenderState());
    case MaterialPipelinePass::AvboitAccumulate:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitAccumulateRenderState());
    case MaterialPipelinePass::AvboitRefractionCapture:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitRefractionCaptureRenderState());
    default:
        return applyTwoSided(BuildMeshRenderState());
    }
}

Core::RenderState BuildCompositeRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

