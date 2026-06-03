// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "avboit.h"
#include "renderer_pipeline_types.h"

#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline Core::RenderState BuildMeshRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .enableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip();
    return renderState;
}

inline Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass, const bool twoSided){
    auto applyTwoSided = [&](Core::RenderState renderState){
        if(twoSided)
            renderState.rasterState.setCullNone();
        return renderState;
    };

    switch(pass){
    case MaterialPipelinePass::Opaque:
        return applyTwoSided(BuildMeshRenderState());
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return applyTwoSided(BuildRendererAvboitVoxelRenderState());
    case MaterialPipelinePass::AvboitAccumulate:
        return applyTwoSided(BuildRendererAvboitAccumulateRenderState());
    default:
        return applyTwoSided(BuildMeshRenderState());
    }
}

inline Core::RenderState BuildCompositeRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

