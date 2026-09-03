// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "buffer_resource_detail.h"

#include <core/common/log.h>
#include <core/graphics/rhi/queue_sharing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_buffer_native{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateNativeBufferSharing(
    const Device& device,
    const VolkInstanceTable& instanceDispatch,
    const VkPhysicalDevice physicalDevice,
    const BufferDesc& desc,
    const NativeBufferProvenance& provenance
){
    if(provenance.sharingMode == VK_SHARING_MODE_EXCLUSIVE){
        if(provenance.queueFamilyIndexCount != 0u || provenance.queueFamilyIndices != nullptr){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: exclusive sharing must not carry queue-family indices")
            );
            return false;
        }
        if(device.usesConcurrentQueueSharing(desc.queueSharing)){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: exclusive native sharing contradicts concurrent logical sharing")
            );
            return false;
        }
        return true;
    }
    if(provenance.sharingMode != VK_SHARING_MODE_CONCURRENT){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native sharing mode is invalid")
        );
        return false;
    }
    if(provenance.queueFamilyIndexCount < 2u || !provenance.queueFamilyIndices){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: concurrent sharing requires at least two queue-family indices")
        );
        return false;
    }
    if(desc.queueSharing == ResourceQueueSharing::Exclusive){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: concurrent native sharing requires explicit logical queue classes")
        );
        return false;
    }

    u32 physicalQueueFamilyCount = 0u;
    instanceDispatch.vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &physicalQueueFamilyCount, nullptr);
    if(physicalQueueFamilyCount == 0u || provenance.queueFamilyIndexCount > physicalQueueFamilyCount){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native queue-family count is invalid")
        );
        return false;
    }
    for(u32 familyIndex = 0u; familyIndex < provenance.queueFamilyIndexCount; ++familyIndex){
        const u32 nativeFamilyIndex = provenance.queueFamilyIndices[familyIndex];
        if(nativeFamilyIndex >= physicalQueueFamilyCount){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native queue-family index is out of range")
            );
            return false;
        }
        for(u32 earlierIndex = 0u; earlierIndex < familyIndex; ++earlierIndex){
            if(provenance.queueFamilyIndices[earlierIndex] == nativeFamilyIndex){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native queue-family indices are not unique")
                );
                return false;
            }
        }
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    bool hasLogicalQueue = false;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(!ResourceQueueSharing::IncludesQueueClass(desc.queueSharing, queue.queueClass))
            continue;
        hasLogicalQueue = true;
        if(!ResourceQueueSharing::QueueFamilyIndexListContains(
            provenance.queueFamilyIndices,
            provenance.queueFamilyIndexCount,
            queue.familyIndex
        )){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native sharing omits a logically admitted queue family")
            );
            return false;
        }
    }
    if(!hasLogicalQueue){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: logical sharing admits no device queue")
        );
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


BufferHandle Device::createHandleForNativeBuffer(
    const ObjectType objectType,
    const Object nativeBufferHandle,
    const BufferDesc& desc,
    const NativeBufferProvenance& nativeProvenance
){
    if(!ResourceQueueSharing::IsValid(desc.queueSharing)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: queue sharing contains unknown bits")
        );
        return nullptr;
    }
    if(objectType != ObjectTypes::VK_Buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: object type is not VK_Buffer"));
        return nullptr;
    }

    auto* nativeBuffer = static_cast<VkBuffer_T*>(nativeBufferHandle);
    if(nativeBuffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: buffer handle is null"));
        return nullptr;
    }
    if(desc.byteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: byte size is zero"));
        return nullptr;
    }
    if(!VulkanBufferDetail::IsBufferCreationStateMaskValid(desc.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: initial state is invalid for a buffer"));
        return nullptr;
    }

    if(nativeProvenance.usage == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage is zero"));
        return nullptr;
    }
    if(nativeProvenance.flags & VK_BUFFER_CREATE_PROTECTED_BIT){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: protected buffers are unsupported"));
        return nullptr;
    }
    if(!__hidden_buffer_native::ValidateNativeBufferSharing(*this, m_context.instanceDispatch, m_context.physicalDevice, desc, nativeProvenance))
        return nullptr;
    if(!VulkanBufferDetail::IsBufferUsageCompatibleWithDescription(desc, nativeProvenance.usage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage contradicts the logical description"));
        return nullptr;
    }
    if(!VulkanBufferDetail::IsBufferUsageCompatibleWithResourceStates(desc, nativeProvenance.usage, desc.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage contradicts the declared initial state"));
        return nullptr;
    }
    if(!VulkanBufferDetail::IsBufferUsageSupportedByDevice(m_context, nativeProvenance.usage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage is unsupported by the device"));
        return nullptr;
    }

    u64 deviceAddress = 0u;
    if(nativeProvenance.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        deviceAddress = m_context.deviceDispatch.vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        if(deviceAddress == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: device address is zero"));
            return nullptr;
        }
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.flags = nativeProvenance.flags;
    bufferInfo.size = desc.byteSize;
    bufferInfo.usage = nativeProvenance.usage;
    bufferInfo.sharingMode = nativeProvenance.sharingMode;
    bufferInfo.queueFamilyIndexCount = nativeProvenance.queueFamilyIndexCount;
    bufferInfo.pQueueFamilyIndices = nativeProvenance.queueFamilyIndices;

    auto* buffer = NewArenaObject<Buffer>(
        m_context.objectArena,
        m_context,
        m_allocator,
        desc,
        bufferInfo,
        nativeProvenance.initialStateKnown
    );
    buffer->m_buffer = nativeBuffer;
    buffer->m_deviceAddress = deviceAddress;
    buffer->m_managed = false;

    if(!m_allocator.tryRegisterBufferNativeIdentity(*buffer)){
        NWB_LOGGER_WARNING(
            NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: a live wrapper already exists")
        );
        DestroyArenaObject(m_context.objectArena, buffer);
        return nullptr;
    }

    return BufferHandle(buffer, BufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

