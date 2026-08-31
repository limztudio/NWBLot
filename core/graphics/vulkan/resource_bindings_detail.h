// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DescriptorBufferStartupPrerequisites{
    bool descriptorBufferExtensionEnabled = false;
    bool bufferDeviceAddressFeatureEnabled = false;
    bool getBufferDeviceAddressAvailable = false;
    bool getDescriptorAvailable = false;
    bool getDescriptorSetLayoutSizeAvailable = false;
    bool getDescriptorSetLayoutBindingOffsetAvailable = false;
    bool cmdBindDescriptorBuffersAvailable = false;
    bool cmdSetDescriptorBufferOffsetsAvailable = false;
};

[[nodiscard]] inline constexpr bool HasDescriptorBufferStartupPrerequisites(
    const DescriptorBufferStartupPrerequisites& prerequisites
)noexcept{
    return
        prerequisites.descriptorBufferExtensionEnabled
        && prerequisites.bufferDeviceAddressFeatureEnabled
        && prerequisites.getBufferDeviceAddressAvailable
        && prerequisites.getDescriptorAvailable
        && prerequisites.getDescriptorSetLayoutSizeAvailable
        && prerequisites.getDescriptorSetLayoutBindingOffsetAvailable
        && prerequisites.cmdBindDescriptorBuffersAvailable
        && prerequisites.cmdSetDescriptorBufferOffsetsAvailable
    ;
}

[[nodiscard]] inline DescriptorBufferStartupPrerequisites QueryDescriptorBufferStartupPrerequisites(
    const VulkanContext& context
)noexcept{
    return {
        .descriptorBufferExtensionEnabled = context.extensions.EXT_descriptor_buffer,
        .bufferDeviceAddressFeatureEnabled = context.extensions.buffer_device_address,
        .getBufferDeviceAddressAvailable = context.deviceDispatch.vkGetBufferDeviceAddress != nullptr,
        .getDescriptorAvailable = context.deviceDispatch.vkGetDescriptorEXT != nullptr,
        .getDescriptorSetLayoutSizeAvailable = context.deviceDispatch.vkGetDescriptorSetLayoutSizeEXT != nullptr,
        .getDescriptorSetLayoutBindingOffsetAvailable = context.deviceDispatch.vkGetDescriptorSetLayoutBindingOffsetEXT != nullptr,
        .cmdBindDescriptorBuffersAvailable = context.deviceDispatch.vkCmdBindDescriptorBuffersEXT != nullptr,
        .cmdSetDescriptorBufferOffsetsAvailable = context.deviceDispatch.vkCmdSetDescriptorBufferOffsetsEXT != nullptr,
    };
}

[[nodiscard]] bool IsDescriptorBufferBackendReady(const VulkanContext& context);
[[nodiscard]] inline constexpr bool IsSupportedDescriptorBindingType(const ResourceType::Enum type){
    switch(type){
    case ResourceType::Texture_SRV:
    case ResourceType::Texture_UAV:
    case ResourceType::TypedBuffer_SRV:
    case ResourceType::TypedBuffer_UAV:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::ConstantBuffer:
    case ResourceType::VolatileConstantBuffer:
    case ResourceType::Sampler:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
    case ResourceType::RayTracingAccelStruct:
        return true;
    default:
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

