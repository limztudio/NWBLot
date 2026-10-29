// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "runtime.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GraphicsRuntime::setDebugRuntimeEnabled(b8 enabled)noexcept{
    if(enabled && !CanEnableDebugRuntime())
        return false;
    if(m_instanceCreated && m_deviceCreationParams.enableDebugRuntime != enabled)
        return false;

    m_deviceCreationParams.enableDebugRuntime = enabled;
    return true;
}

b8 GraphicsRuntime::setHardwareRayTracingPolicy(const HardwareRayTracingPolicy::Enum policy)noexcept{
    if(m_instanceCreated || !IsValidHardwareRayTracingPolicy(policy))
        return false;

    m_deviceCreationParams.hardwareRayTracingPolicy = policy;
    return true;
}

b8 GraphicsRuntime::setNativeMeshShadersEnabled(const b8 enabled)noexcept{
    if(m_instanceCreated)
        return false;

    m_deviceCreationParams.enableNativeMeshShaders = enabled;
    return true;
}

b8 GraphicsRuntime::setAsyncComputeLaneEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableAsyncComputeLane = enabled;
    return true;
}

b8 GraphicsRuntime::setTransferQueueEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableTransferQueue = enabled;
    return true;
}

b8 GraphicsRuntime::setSameClassMultiQueueEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSameClassMultiQueue = enabled;
    return true;
}

b8 GraphicsRuntime::setCrossFamilySameClassQueueRoutingEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableCrossFamilySameClassQueueRouting = enabled;
    return true;
}

b8 GraphicsRuntime::setAdapterIndex(const i32 index)noexcept{
    if(index < -1 || m_backend->getDevice())
        return false;

    m_deviceCreationParams.adapterIndex = index;
    return true;
}

b8 GraphicsRuntime::setHDR10OutputEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableHDR10Output = enabled;
    return true;
}

b8 GraphicsRuntime::setSwapChainReadbackEnabled(const b8 enabled)noexcept{
    if(m_backend->getDevice())
        return false;

    m_deviceCreationParams.enableSwapChainReadback = enabled;
    return true;
}

b8 GraphicsRuntime::setBindlessHeapAbi(const GpuDescriptorHeapAbi& abi)noexcept{
    if(!abi.valid() || m_backend->getDevice())
        return false;

    m_deviceCreationParams.bindlessHeapAbi = abi;
    return true;
}

void GraphicsRuntime::setPipelineCacheDirectory(const Path& directory){
    m_deviceCreationParams.pipelineCacheDirectory = directory;
}

b8 GraphicsRuntime::setFilesystemFactory(const Filesystem::FilesystemFactory& factory){
    if(m_backend->getDevice())
        return false;
    m_deviceCreationParams.filesystemFactory = factory;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

