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


void* Graphics::allocatePersistentSystemMemory(void* userData, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    (void)scope;
    if(!userData)
        return nullptr;

    auto* arena = static_cast<PersistentArena*>(userData);
    return arena->allocate(alignment, size);
}

void* Graphics::reallocatePersistentSystemMemory(void* userData, void* original, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope){
    (void)scope;
    if(!userData)
        return nullptr;

    auto* arena = static_cast<PersistentArena*>(userData);
    return arena->reallocate(original, alignment, size);
}

void Graphics::freePersistentSystemMemory(void* userData, void* memory){
    if(!userData || !memory)
        return;

    auto* arena = static_cast<PersistentArena*>(userData);
    arena->deallocate(memory, 1, 0);
}


Graphics::Graphics()
    : m_persistentArena(s_maxDynamicAllocSize)
    , m_systemMemoryAllocator{
        &m_persistentArena,
        &Graphics::allocatePersistentSystemMemory,
        &Graphics::reallocatePersistentSystemMemory,
        &Graphics::freePersistentSystemMemory
    }
{
    m_deviceManager = IDeviceManager::create(GraphicsAPI::VULKAN);
}

Graphics::~Graphics(){
    destroy();
}

bool Graphics::init(const Common::FrameData& data){
    if(!m_deviceManager)
        return false;

    return m_deviceManager->createWindowDeviceAndSwapChain(m_deviceCreationParams, data);
}

void Graphics::destroy(){
    if(m_deviceManager){
        m_deviceManager->shutdown();
        delete m_deviceManager;
        m_deviceManager = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
