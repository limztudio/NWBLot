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


inline Futex s_DescriptorBufferStorageIdentityMutex;
inline u64 s_NextDescriptorBufferStorageIdentity = 1u;

[[nodiscard]] inline u64 AllocateDescriptorBufferStorageIdentity(){
    ScopedLock lock(s_DescriptorBufferStorageIdentityMutex);

    if(s_NextDescriptorBufferStorageIdentity == 0u)
        return 0u;

    const u64 identity = s_NextDescriptorBufferStorageIdentity;
    s_NextDescriptorBufferStorageIdentity = identity == UINT64_MAX ? 0u : identity + 1u;
    return identity;
}

[[nodiscard]] inline constexpr bool UsesDescriptorBufferInfo(const ResourceType::Enum type){
    switch(type){
    case ResourceType::ConstantBuffer:
    case ResourceType::StructuredBuffer_SRV:
    case ResourceType::StructuredBuffer_UAV:
    case ResourceType::RawBuffer_SRV:
    case ResourceType::RawBuffer_UAV:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline u32 GetDescriptorSize(
    const VulkanContext& context,
    const bool enabled,
    const VkDescriptorType descriptorType
){
    if(!enabled)
        return 0u;

    const auto& props = context.descriptorBufferProperties;
    VkDeviceSize size = 0u;
    switch(descriptorType){
    case VK_DESCRIPTOR_TYPE_SAMPLER:                    size = props.samplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:     size = props.combinedImageSamplerDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:              size = props.sampledImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:              size = props.storageImageDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:       size = props.uniformTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:       size = props.storageTexelBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:             size = props.uniformBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:             size = props.storageBufferDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:           size = props.inputAttachmentDescriptorSize; break;
    case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: size = props.accelerationStructureDescriptorSize; break;
    default:                                            return 0u;
    }

    return size > UINT32_MAX ? 0u : static_cast<u32>(size);
}

[[nodiscard]] inline bool ResolveDescriptorBufferRange(
    const DescriptorWriteItem& item,
    const BufferDesc& bufferDesc,
    BufferRange& outRange
){
    outRange = item.range.resolve(bufferDesc);
    return outRange.byteSize > 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

