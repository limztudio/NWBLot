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


namespace VulkanBufferDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 IsBufferDescriptionCompatibleWithResourceStates(const BufferDesc& desc, const ResourceStates::Mask states)noexcept{
    if(states == ResourceStates::Unknown)
        return true;
    if(!IsBufferResourceStateMaskValid(states))
        return false;
    if((states & ResourceStates::ConstantBuffer) && !desc.isConstantBuffer)
        return false;
    if((states & ResourceStates::VertexBuffer) && !desc.isVertexBuffer)
        return false;
    if((states & ResourceStates::IndexBuffer) && !desc.isIndexBuffer)
        return false;
    if((states & ResourceStates::IndirectArgument) && !desc.isDrawIndirectArgs && (!(states & ResourceStates::AccelStructBuildInput) || !desc.isAccelStructBuildInput))
        return false;
    if((states & (ResourceStates::AccelStructRead | ResourceStates::AccelStructBuildBlas)) && !desc.isAccelStructStorage)
        return false;
    if((states & ResourceStates::AccelStructBuildInput) && !desc.isAccelStructBuildInput)
        return false;
    if((states & ResourceStates::OpacityMicromapWrite) && !desc.isAccelStructStorage)
        return false;
    if((states & ResourceStates::OpacityMicromapBuildInput) && !desc.isAccelStructBuildInput)
        return false;
    return true;
}

VkBufferUsageFlags RequiredBufferUsageForResourceStates(const BufferDesc& desc, const ResourceStates::Mask states)noexcept{
    VkBufferUsageFlags usage = 0u;
    if(states & ResourceStates::ConstantBuffer)
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(states & ResourceStates::VertexBuffer)
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(states & ResourceStates::IndexBuffer)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if((states & ResourceStates::IndirectArgument) && (desc.isDrawIndirectArgs || !(states & ResourceStates::AccelStructBuildInput)))
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if(states & ResourceStates::CopySource)
        usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if(states & ResourceStates::CopyDest)
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if(states & (ResourceStates::AccelStructRead | ResourceStates::AccelStructBuildBlas))
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(states & ResourceStates::AccelStructBuildInput){
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    if(states & ResourceStates::OpacityMicromapWrite)
        usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(states & ResourceStates::OpacityMicromapBuildInput)
        usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if(states & (ResourceStates::ConvertCoopVecMatrixInput | ResourceStates::ConvertCoopVecMatrixOutput))
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return usage;
}

VkBufferUsageFlags RequiredBufferUsageForDescription(const BufferDesc& desc)noexcept{
    VkBufferUsageFlags usage = 0u;
    if(desc.isVertexBuffer)
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(desc.isIndexBuffer)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(desc.isConstantBuffer)
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(HasStorageBufferUsage(desc))
        usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if(desc.canHaveTypedViews){
        usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        if(desc.canHaveUAVs)
            usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    }
    if(desc.isDrawIndirectArgs)
        usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if(desc.isAccelStructBuildInput)
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    if(desc.isAccelStructStorage)
        usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if(desc.isShaderBindingTable)
        usage |= VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR;
    if(desc.isAccelStructBuildInput || desc.isAccelStructStorage || desc.isShaderBindingTable)
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return usage;
}

b8 IsBufferUsageSupportedByDevice(const VulkanContext& context, const VkBufferUsageFlags usage)noexcept{
    if(usage == 0u)
        return false;
    if((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) && (!context.extensions.bufferDeviceAddress || !context.deviceDispatch.vkGetBufferDeviceAddress))
        return false;

    const VkBufferUsageFlags accelStructUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
    if((usage & accelStructUsage) && (!context.extensions.khrAccelerationStructure || !context.accelerationStructureFeatureEnabled || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)))
        return false;
    if((usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) && (!context.extensions.khrRayTracingPipeline || !context.rayTracingPipelineFeatureEnabled || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)))
        return false;

    const VkBufferUsageFlags micromapUsage = VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    if(
        (usage & micromapUsage)
        && (
            !context.extensions.extOpacityMicromap
            || !context.opacityMicromapFeatureEnabled
            || !context.extensions.khrSynchronization2
            || !context.extensions.khrAccelerationStructure
            || !context.accelerationStructureFeatureEnabled
            || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        )
    )
        return false;

    const VkBufferUsageFlags descriptorBufferUsage = VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT | VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT;
    if((usage & descriptorBufferUsage) && (!context.extensions.extDescriptorBuffer || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)))
        return false;
    return true;
}

VkBufferUsageFlags PickManagedBufferUsage(const VulkanContext& context, const BufferDesc& desc)noexcept{
    VkBufferUsageFlags usage = RequiredBufferUsageForDescription(desc);
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    if(context.extensions.extOpacityMicromap && context.opacityMicromapFeatureEnabled && context.extensions.khrSynchronization2 && context.extensions.khrAccelerationStructure && context.accelerationStructureFeatureEnabled){
        if(desc.isAccelStructBuildInput)
            usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
        if(desc.isAccelStructStorage)
            usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    }
    if(context.extensions.bufferDeviceAddress)
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return usage;
}

b8 IsBufferUsageCompatibleWithDescription(const BufferDesc& desc, const VkBufferUsageFlags usage)noexcept{
    const VkBufferUsageFlags requiredUsage = RequiredBufferUsageForDescription(desc);
    return (usage & requiredUsage) == requiredUsage;
}

b8 IsBufferUsageCompatibleWithResourceStates(const BufferDesc& desc, const VkBufferUsageFlags usage, const ResourceStates::Mask states)noexcept{
    if(!IsBufferDescriptionCompatibleWithResourceStates(desc, states))
        return false;
    const VkBufferUsageFlags requiredUsage = RequiredBufferUsageForResourceStates(desc, states);
    return (usage & requiredUsage) == requiredUsage;
}

b8 IsBufferUsageConsistent(const VulkanContext& context, const BufferDesc& desc, const b8 managed, const VkBufferUsageFlags usage)noexcept{
    if(!IsBufferUsageSupportedByDevice(context, usage))
        return false;
    if(!IsBufferUsageCompatibleWithDescription(desc, usage))
        return false;
    if(!IsBufferUsageCompatibleWithResourceStates(desc, usage, desc.initialState))
        return false;
    return !managed || usage == PickManagedBufferUsage(context, desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<BufferHandle> Device::createHandleForNativeBuffer(const ObjectType objectType, const Object nativeBufferHandle, const BufferDesc& desc, const NativeBufferProvenance& nativeProvenance){
    if(!ResourceQueueSharing::IsValid(desc.queueSharing)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: queue sharing contains unknown bits"));
        return MakeUnexpected(Failure{});
    }
    if(objectType != ObjectTypes::s_Buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: object type is not VK_Buffer"));
        return MakeUnexpected(Failure{});
    }

    auto* nativeBuffer = static_cast<VkBuffer_T*>(nativeBufferHandle);
    if(nativeBuffer == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: buffer handle is null"));
        return MakeUnexpected(Failure{});
    }
    if(desc.byteSize == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: byte size is zero"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanBufferDetail::IsBufferCreationStateMaskValid(desc.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: initial state is invalid for a buffer"));
        return MakeUnexpected(Failure{});
    }

    if(nativeProvenance.usage == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage is zero"));
        return MakeUnexpected(Failure{});
    }
    if(nativeProvenance.flags & VK_BUFFER_CREATE_PROTECTED_BIT){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: protected buffers are unsupported"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanNativeSharingDetail::ValidateNativeResourceSharing(*this, m_context.instanceDispatch, m_context.physicalDevice, desc.queueSharing, nativeProvenance, "buffer handle for native buffer"))
        return MakeUnexpected(Failure{});
    if(!VulkanBufferDetail::IsBufferUsageCompatibleWithDescription(desc, nativeProvenance.usage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage contradicts the logical description"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanBufferDetail::IsBufferUsageCompatibleWithResourceStates(desc, nativeProvenance.usage, desc.initialState)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage contradicts the declared initial state"));
        return MakeUnexpected(Failure{});
    }
    if(!VulkanBufferDetail::IsBufferUsageSupportedByDevice(m_context, nativeProvenance.usage)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: native usage is unsupported by the device"));
        return MakeUnexpected(Failure{});
    }

    u64 deviceAddress = 0u;
    if(nativeProvenance.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT){
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = nativeBuffer;
        deviceAddress = m_context.deviceDispatch.vkGetBufferDeviceAddress(m_context.device, &addressInfo);
        if(deviceAddress == 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: device address is zero"));
            return MakeUnexpected(Failure{});
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

    BufferHandle buffer(NewArenaObject<Buffer>(m_context.objectArena, m_context, m_allocator, desc, bufferInfo, nativeProvenance.initialStateKnown), BufferHandle::deleter_type(&m_context.objectArena), s_AdoptRef);
    if(!buffer)
        return MakeUnexpected(Failure{});
    buffer->m_buffer = nativeBuffer;
    buffer->m_deviceAddress = deviceAddress;
    buffer->m_managed = false;

    if(!m_allocator.tryRegisterBufferNativeIdentity(*buffer)){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to create buffer handle for native buffer: a live wrapper already exists"));
        return MakeUnexpected(Failure{});
    }

    return buffer;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

