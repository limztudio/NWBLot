// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "buffer_resource_detail.h"
#include "native_sharing_validation.h"

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
    return VulkanNativeSharingDetail::ValidateNativeResourceSharing(
        device,
        instanceDispatch,
        physicalDevice,
        desc.queueSharing,
        provenance,
        "buffer handle for native buffer"
    );
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

