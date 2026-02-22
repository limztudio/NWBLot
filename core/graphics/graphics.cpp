// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/vulkan_device_manager.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 Graphics::queryGraphicsWorkerThreadCount(){
    u32 coreCount = Alloc::QueryCoreCount(Alloc::CoreAffinity::Performance);
    if(coreCount <= 1)
        coreCount = Alloc::QueryCoreCount(Alloc::CoreAffinity::Any);

    const u32 reservedCores = s_reservedPerformanceCoresForMainThread;
    return coreCount > reservedCores ? (coreCount - reservedCores) : 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IDeviceManager* IDeviceManager::create(GraphicsAPI::Enum api, const DeviceCreationParameters& params){
    switch(api){
    case GraphicsAPI::VULKAN:
        return new Vulkan::DeviceManager(params);
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("DeviceManager: Unsupported graphics API."));
        return nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::Graphics()
    : m_allocator(s_dynamicAllocatorSize)
    , m_threadPool(queryGraphicsWorkerThreadCount(), Alloc::CoreAffinity::Any)
{
    m_deviceCreationParams.allocator = &m_allocator;
    m_deviceCreationParams.threadPool = &m_threadPool;
    m_deviceManager = IDeviceManager::create(GraphicsAPI::VULKAN, m_deviceCreationParams);
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    if(!m_deviceManager)
        return false;

    return m_deviceManager->createWindowDeviceAndSwapChain(data);
}

bool Graphics::runFrame(){
    if(!m_deviceManager)
        return false;

    return m_deviceManager->runFrame();
}

void Graphics::updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus){
    if(!m_deviceManager)
        return;

    m_deviceManager->updateWindowState(width, height, windowVisible, windowIsInFocus);
}

void Graphics::destroy(){
    if(m_deviceManager){
        IDeviceManager* dm = m_deviceManager;
        m_deviceManager = nullptr;
        dm->shutdown();
        delete dm;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

