// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "render_pass_collection.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsModuleDetail{


RenderPassCollection::RenderPassCollection(Alloc::GlobalArena& arena)
    : m_passes(arena)
{}

void RenderPassCollection::addToBack(IRenderPass& pass, const u32 width, const u32 height, const u32 samples){
    m_passes.remove(MakeNotNull(&pass));
    m_passes.push_back(MakeNotNull(&pass));

    pass.backBufferResizing();
    pass.backBufferResized(width, height, samples);
    if(!pass.validateResources(width, height, samples))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: back render pass failed to validate resources after registration"));
}

void RenderPassCollection::remove(IRenderPass& pass){
    pass.invalidateResources();
    m_passes.remove(MakeNotNull(&pass));
}

void RenderPassCollection::clear(){
    m_passes.clear();
}

void RenderPassCollection::invalidateResources(){
    for(const auto renderPass : m_passes)
        renderPass->invalidateResources();
}

b8 RenderPassCollection::validateResources(const u32 width, const u32 height, const u32 samples){
    b8 valid = true;
    for(const auto renderPass : m_passes)
        valid = renderPass->validateResources(width, height, samples) && valid;
    return valid;
}

void RenderPassCollection::backBufferResizing(){
    for(const auto renderPass : m_passes)
        renderPass->backBufferResizing();
}

void RenderPassCollection::backBufferResized(const u32 width, const u32 height, const u32 samples){
    for(const auto renderPass : m_passes)
        renderPass->backBufferResized(width, height, samples);
}

void RenderPassCollection::displayScaleChanged(const f32 x, const f32 y){
    for(const auto renderPass : m_passes)
        renderPass->displayScaleChanged(x, y);
}

void RenderPassCollection::animate(const f64 elapsedTime){
    for(const auto renderPass : m_passes)
        renderPass->animate(static_cast<f32>(elapsedTime));
}

b8 RenderPassCollection::shouldRenderUnfocused()const{
    for(auto it = m_passes.crbegin(); it != m_passes.crend(); ++it){
        if((*it)->shouldRenderUnfocused())
            return true;
    }
    return false;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

