// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



#include "runtime.h"

#include "runtime_internal.h"

#include <core/graphics/backend_selection.h>

#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture* GraphicsRuntime::getBackBuffer(u32 index)const{
    return m_backend->getBackBuffer(index);
}

u32 GraphicsRuntime::getBackBufferCount()const{
    return m_backend->getBackBufferCount();
}

Framebuffer* GraphicsRuntime::getFramebuffer(u32 index)const{
    if(index < m_swapChainFramebuffers.size())
        return m_swapChainFramebuffers[index].get();
    return nullptr;
}

BufferHandle GraphicsRuntime::createBuffer(const BufferDesc& desc)const{
    return getDevice().createBuffer(desc);
}

TextureHandle GraphicsRuntime::createTexture(const TextureDesc& desc)const{
    return getDevice().createTexture(desc);
}

bool GraphicsRuntime::backBufferResizing(SwapChainTransitionTicket& outTicket){
    waitTasks();
    if(!m_backend->prepareSwapChainTransition(SwapChainTransitionKind::Resize, outTicket)){
        requestDeviceRecreation();
        return false;
    }

    m_acquiredPresentationFrame = {};
    invalidateRenderPassResources();
    m_swapChainFramebuffers.clear();

    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResizing();
    return true;
}

bool GraphicsRuntime::resizeBackBuffer(
    const u32 width,
    const u32 height,
    const bool vsyncEnabled
){
    SwapChainTransitionTicket transitionTicket;
    if(!backBufferResizing(transitionTicket))
        return false;

    m_swapChainState.backBufferWidth = width;
    m_swapChainState.backBufferHeight = height;
    m_swapChainState.vsyncEnabled = vsyncEnabled;
    if(!m_backend->commitSwapChainResize(Move(transitionTicket))){
        requestDeviceRecreation();
        return false;
    }
    if(!backBufferResized()){
        requestDeviceRecreation();
        return false;
    }
    return true;
}

bool GraphicsRuntime::backBufferResized(){
    for(auto* renderPass : m_renderPasses)
        renderPass->backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);

    const u32 backBufferCount = getBackBufferCount();
    m_swapChainFramebuffers.clear();
    m_swapChainFramebuffers.reserve(backBufferCount);
    for(u32 index = 0; index < backBufferCount; ++index){
        FramebufferHandle framebuffer = getDevice().createFramebuffer(
            FramebufferDesc().addColorAttachment(getBackBuffer(index))
        );
        if(!framebuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: failed to rebuild swap-chain framebuffer {}"), index);
            m_swapChainFramebuffers.clear();
            invalidateRenderPassResources();
            return false;
        }
        m_swapChainFramebuffers.push_back(Move(framebuffer));
    }

    if(!validateRenderPassResources()){
        NWB_LOGGER_ERROR(NWB_TEXT("GraphicsRuntime: one or more render passes failed to validate resources after back buffer resize"));
        m_swapChainFramebuffers.clear();
        invalidateRenderPassResources();
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("GraphicsRuntime: Back buffer resized to {}x{}"), m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight);
    return true;
}


void GraphicsRuntime::displayScaleChanged(){
    notifyPointerScaleChanged();

    for(auto* renderPass : m_renderPasses)
        renderPass->displayScaleChanged(m_dpiScaleFactorX, m_dpiScaleFactorY);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

