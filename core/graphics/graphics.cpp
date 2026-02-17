// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "graphics.h"

#include <logger/client/logger.h>

#include "vulkan/vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


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
    Vulkan::DeviceDesc deviceDesc = {};
    deviceDesc.systemMemoryAllocator = &m_systemMemoryAllocator;
    deviceDesc.allocationCallbacks = nullptr;
    deviceDesc.maxTimerQueries = m_gpuTimeQueriesPerFrame;
    deviceDesc.aftermathEnabled = false;
    deviceDesc.logBufferLifetime = false;
    
    m_device = Vulkan::CreateDevice(deviceDesc);
}
Graphics::~Graphics(){}

bool Graphics::init(const Common::FrameData& data){ return m_engine->init(data); }
void Graphics::destroy(){ m_engine.reset(); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
