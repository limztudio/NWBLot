// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/vulkan_device_manager.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


IDeviceManager* IDeviceManager::create(GraphicsAPI::Enum api){
    switch(api){
    case GraphicsAPI::VULKAN:
        return new Vulkan::DeviceManager();
    default:
        NWB_LOGGER_ERROR(NWB_TEXT("DeviceManager: Unsupported graphics API."));
        return nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Graphics::Graphics()
    : m_allocator(s_maxDynamicAllocSize)
{
    m_deviceManager = IDeviceManager::create(GraphicsAPI::VULKAN);
}
Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    if(!m_deviceManager)
        return false;

    m_deviceCreationParams.allocator = &m_allocator;
    return m_deviceManager->createWindowDeviceAndSwapChain(m_deviceCreationParams, data);
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
