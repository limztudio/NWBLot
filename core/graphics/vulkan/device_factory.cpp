// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "device_detail.h"
#include "resource_bindings_detail.h"

#include <core/common/log.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeviceHandle CreateDevice(const DeviceDesc& desc){
    if(
        !desc.nativeQueues
        || desc.nativeQueueCount == 0u
        || !desc.physicalQueues
        || desc.physicalQueueCount == 0u
        || !desc.getInstanceProcAddr
        || !desc.instanceDispatch.vkGetPhysicalDeviceProperties
        || !desc.deviceDispatch.vkDestroyDevice
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Device creation requires non-empty native and physical queue registries."));
        return {};
    }
    auto* device = NewArenaObject<Device>(desc.allocator.getObjectArena(), desc);
    if(!device || !device->m_queueRegistryReady){
        if(device)
            DestroyArenaObject(desc.allocator.getObjectArena(), device);
        return {};
    }
    return DeviceHandle(device, DeviceHandle::deleter_type(&desc.allocator.getObjectArena()), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

