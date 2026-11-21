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

static void RemoveRenderPassFromList(RenderPassCollection::PassList& passes, const IRenderPass* const target)noexcept{
    usize writeIndex = 0u;
    for(usize readIndex = 0u; readIndex < passes.size(); ++readIndex){
        if(passes[readIndex].get() == target)
            continue;
        if(writeIndex != readIndex)
            passes[writeIndex] = passes[readIndex];
        ++writeIndex;
    }
    passes.erase(passes.begin() + static_cast<isize>(writeIndex), passes.end());
}

void RenderPassCollection::addToBack(IRenderPass& pass, const u32 width, const u32 height, const u32 samples){
    RemoveRenderPassFromList(m_passes, &pass);
    m_passes.push_back(MakeNotNull(&pass));

    pass.backBufferResizing();
    pass.backBufferResized(width, height, samples);
    if(!pass.validateResources(width, height, samples))
        NWB_LOGGER_WARNING(FOUNDATION_TEXT("GraphicsRuntime: back render pass failed to validate resources after registration"));
}

void RenderPassCollection::remove(IRenderPass& pass){
    pass.invalidateResources();
    RemoveRenderPassFromList(m_passes, &pass);
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

