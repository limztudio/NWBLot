// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"
#include "runtime_internal.h"
#include "profile_names.h"

#include <core/graphics/backend_selection.h>

#include <core/common/log.h>
#include <core/task/gpu/scheduler.h>
#include <core/telemetry/session.h>
#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GraphicsRuntime::addRenderPassToFront(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_front(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: front render pass failed to validate resources after registration"));
}

void GraphicsRuntime::addRenderPassToBack(IRenderPass& pass){
    m_renderPasses.remove(&pass);
    m_renderPasses.push_back(&pass);

    pass.backBufferResizing();
    pass.backBufferResized(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount);
    if(!pass.validateResources(m_swapChainState.backBufferWidth, m_swapChainState.backBufferHeight, m_deviceCreationParams.swapChainSampleCount))
        NWB_LOGGER_WARNING(NWB_TEXT("GraphicsRuntime: back render pass failed to validate resources after registration"));
}

void GraphicsRuntime::removeRenderPass(IRenderPass& pass){
    waitTasks();
    const bool deviceIdle = waitForIdle();
    GraphicsBackend::Device* const device = m_backend->getDevice();
    NWB_FATAL_ASSERT_MSG(
        deviceIdle || (device && device->isDeviceLost()),
        NWB_TEXT("Render-pass removal requires either a completed device join or terminal device loss")
    );

    pass.invalidateResources();
    m_renderPasses.remove(&pass);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GraphicsRuntime::invalidateRenderPassResources(){
    for(auto* renderPass : m_renderPasses)
        renderPass->invalidateResources();
}

bool GraphicsRuntime::validateRenderPassResources(){
    bool valid = true;
    for(auto* renderPass : m_renderPasses){
        valid =
            renderPass->validateResources(
                m_swapChainState.backBufferWidth,
                m_swapChainState.backBufferHeight,
                m_deviceCreationParams.swapChainSampleCount
            )
            && valid
        ;
    }
    return valid;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

