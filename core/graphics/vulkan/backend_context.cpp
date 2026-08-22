// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend_context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BackendContext::BackendContext(
    const DeviceCreationParameters& params,
    SwapChainRuntimeState& swapChainState,
    GraphicsAllocator& allocator,
    Alloc::ThreadPool& threadPool
)
    : m_deviceParams(params)
    , m_swapChainState(swapChainState)
    , m_allocator(allocator)
    , m_threadPool(threadPool)
    , m_arena(m_allocator.getObjectArena())
    , m_enabledExtensions(m_arena)
    , m_optionalExtensions(m_arena)
    , m_rayTracingExtensions(0, Hasher<GraphicsString>(), EqualTo<GraphicsString>(), m_arena)
    , m_rendererString(m_arena)
    , m_swapChainImages(m_arena)
    , m_sameClassQueues(m_arena)
    , m_acquireSemaphores(m_arena)
    , m_presentSemaphores(m_arena)
    , m_framesInFlight(Deque<EventQueryHandle, Alloc::GlobalArena>(m_arena))
    , m_queryPool(m_arena)
{
    initDefaultExtensions();
}


bool BackendContext::isValidationMessageIdIgnored(i32 messageId)const{
    for(const auto& ignored : m_deviceParams.ignoredValidationMessageIds){
        if(ignored == messageId)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

