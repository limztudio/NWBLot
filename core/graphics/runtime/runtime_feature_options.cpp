// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GraphicsRuntime::setDebugRuntimeEnabled(bool enabled)noexcept{
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

bool GraphicsRuntime::setHardwareRayTracingPolicy(const HardwareRayTracingPolicy::Enum policy)noexcept{
    if(m_instanceCreated || !IsValidHardwareRayTracingPolicy(policy))
        return false;

    m_deviceCreationParams.hardwareRayTracingPolicy = policy;
    return true;
}

bool GraphicsRuntime::setNativeMeshShadersEnabled(const bool enabled)noexcept{
    if(m_instanceCreated)
        return false;

    m_deviceCreationParams.enableNativeMeshShaders = enabled;
    return true;
}

bool GraphicsRuntime::setAsyncComputeLaneEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableAsyncComputeLane = enabled;
    return true;
}

bool GraphicsRuntime::setTransferQueueEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableTransferQueue = enabled;
    return true;
}

bool GraphicsRuntime::setSameClassMultiQueueEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSameClassMultiQueue = enabled;
    return true;
}

bool GraphicsRuntime::setCrossFamilySameClassQueueRoutingEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableCrossFamilySameClassQueueRouting = enabled;
    return true;
}

bool GraphicsRuntime::setAdapterIndex(const i32 index)noexcept{
    if(index < -1 || m_backend->getDevice())
        return false;

    m_deviceCreationParams.adapterIndex = index;
    return true;
}

bool GraphicsRuntime::setHDR10OutputEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableHDR10Output = enabled;
    return true;
}

bool GraphicsRuntime::setSwapChainReadbackEnabled(const bool enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSwapChainReadback = enabled;
    return true;
}

bool GraphicsRuntime::setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept{
    if(!abi.valid() || m_backend->getDevice())
        return false;

    m_deviceCreationParams.bindlessHeapAbi = abi;
    return true;
}

void GraphicsRuntime::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
}

bool GraphicsRuntime::setFilesystemFactory(const Filesystem::FilesystemFactory& factory){
    if(m_backend->getDevice())
        return false;
    m_deviceCreationParams.filesystemFactory = factory;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

