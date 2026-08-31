// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanBufferDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr ResourceStates::Mask s_ValidBufferResourceStates = static_cast<ResourceStates::Mask>(
    static_cast<u32>(ResourceStates::Common)
    | static_cast<u32>(ResourceStates::ConstantBuffer)
    | static_cast<u32>(ResourceStates::VertexBuffer)
    | static_cast<u32>(ResourceStates::IndexBuffer)
    | static_cast<u32>(ResourceStates::IndirectArgument)
    | static_cast<u32>(ResourceStates::ShaderResource)
    | static_cast<u32>(ResourceStates::UnorderedAccess)
    | static_cast<u32>(ResourceStates::CopyDest)
    | static_cast<u32>(ResourceStates::CopySource)
    | static_cast<u32>(ResourceStates::AccelStructRead)
    | static_cast<u32>(ResourceStates::AccelStructWrite)
    | static_cast<u32>(ResourceStates::AccelStructBuildInput)
    | static_cast<u32>(ResourceStates::AccelStructBuildBlas)
    | static_cast<u32>(ResourceStates::OpacityMicromapWrite)
    | static_cast<u32>(ResourceStates::OpacityMicromapBuildInput)
    | static_cast<u32>(ResourceStates::ConvertCoopVecMatrixInput)
    | static_cast<u32>(ResourceStates::ConvertCoopVecMatrixOutput)
);

[[nodiscard]] inline bool BufferDescriptionsEqual(const BufferDesc& lhs, const BufferDesc& rhs)noexcept{
    return
        lhs.debugName == rhs.debugName
        && lhs.byteSize == rhs.byteSize
        && lhs.structStride == rhs.structStride
        && lhs.maxVersions == rhs.maxVersions
        && lhs.initialState == rhs.initialState
        && lhs.format == rhs.format
        && lhs.queueSharing == rhs.queueSharing
        && lhs.canHaveUAVs == rhs.canHaveUAVs
        && lhs.canHaveTypedViews == rhs.canHaveTypedViews
        && lhs.canHaveRawViews == rhs.canHaveRawViews
        && lhs.isVertexBuffer == rhs.isVertexBuffer
        && lhs.isIndexBuffer == rhs.isIndexBuffer
        && lhs.isConstantBuffer == rhs.isConstantBuffer
        && lhs.isDrawIndirectArgs == rhs.isDrawIndirectArgs
        && lhs.isAccelStructBuildInput == rhs.isAccelStructBuildInput
        && lhs.isAccelStructStorage == rhs.isAccelStructStorage
        && lhs.isShaderBindingTable == rhs.isShaderBindingTable
        && lhs.isVolatile == rhs.isVolatile
        && lhs.isVirtual == rhs.isVirtual
        && lhs.keepInitialState == rhs.keepInitialState
        && lhs.cpuAccess == rhs.cpuAccess
    ;
}

[[nodiscard]] constexpr bool IsBufferResourceStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states != ResourceStates::Unknown && (states & ~s_ValidBufferResourceStates) == 0u;
}

[[nodiscard]] constexpr bool IsBufferCreationStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states == ResourceStates::Unknown || IsBufferResourceStateMaskValid(states);
}

[[nodiscard]] constexpr bool HasStorageBufferUsage(const BufferDesc& desc)noexcept{
    return desc.structStride != 0u || desc.canHaveUAVs || desc.canHaveRawViews;
}

[[nodiscard]] inline bool IsBufferDescriptionCompatibleWithResourceStates(
    const BufferDesc& desc,
    const ResourceStates::Mask states
)noexcept{
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
    if(
        (states & ResourceStates::IndirectArgument)
        && !desc.isDrawIndirectArgs
        && (!(states & ResourceStates::AccelStructBuildInput) || !desc.isAccelStructBuildInput)
    )
        return false;
    if(
        (states & (ResourceStates::AccelStructRead | ResourceStates::AccelStructBuildBlas))
        && !desc.isAccelStructStorage
    )
        return false;
    if((states & ResourceStates::AccelStructBuildInput) && !desc.isAccelStructBuildInput)
        return false;
    if((states & ResourceStates::OpacityMicromapWrite) && !desc.isAccelStructStorage)
        return false;
    if((states & ResourceStates::OpacityMicromapBuildInput) && !desc.isAccelStructBuildInput)
        return false;
    return true;
}

[[nodiscard]] inline VkBufferUsageFlags RequiredBufferUsageForResourceStates(
    const BufferDesc& desc,
    const ResourceStates::Mask states
)noexcept{
    VkBufferUsageFlags usage = 0u;
    if(states & ResourceStates::ConstantBuffer)
        usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if(states & ResourceStates::VertexBuffer)
        usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if(states & ResourceStates::IndexBuffer)
        usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if(
        (states & ResourceStates::IndirectArgument)
        && (desc.isDrawIndirectArgs || !(states & ResourceStates::AccelStructBuildInput))
    )
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

[[nodiscard]] inline VkBufferUsageFlags RequiredBufferUsageForDescription(const BufferDesc& desc)noexcept{
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

[[nodiscard]] inline bool IsBufferUsageSupportedByDevice(
    const VulkanContext& context,
    const VkBufferUsageFlags usage
)noexcept{
    if(usage == 0u)
        return false;
    if(
        (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        && (!context.extensions.buffer_device_address || !vkGetBufferDeviceAddress)
    )
        return false;

    const VkBufferUsageFlags accelStructUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
        | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
    ;
    if(
        (usage & accelStructUsage)
        && (
            !context.extensions.KHR_acceleration_structure
            || !context.accelerationStructureFeatureEnabled
            || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        )
    )
        return false;
    if(
        (usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)
        && (
            !context.extensions.KHR_ray_tracing_pipeline
            || !context.rayTracingPipelineFeatureEnabled
            || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        )
    )
        return false;

    const VkBufferUsageFlags micromapUsage =
        VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT
        | VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT
    ;
    if(
        (usage & micromapUsage)
        && (
            !context.extensions.EXT_opacity_micromap
            || !context.opacityMicromapFeatureEnabled
            || !context.extensions.KHR_synchronization2
            || !context.extensions.KHR_acceleration_structure
            || !context.accelerationStructureFeatureEnabled
            || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        )
    )
        return false;

    const VkBufferUsageFlags descriptorBufferUsage =
        VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
        | VK_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT
    ;
    if(
        (usage & descriptorBufferUsage)
        && (
            !context.extensions.EXT_descriptor_buffer
            || !(usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
        )
    )
        return false;
    return true;
}

[[nodiscard]] inline VkBufferUsageFlags PickManagedBufferUsage(
    const VulkanContext& context,
    const BufferDesc& desc
)noexcept{
    VkBufferUsageFlags usage = RequiredBufferUsageForDescription(desc);
    usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
    if(
        context.extensions.EXT_opacity_micromap
        && context.opacityMicromapFeatureEnabled
        && context.extensions.KHR_synchronization2
        && context.extensions.KHR_acceleration_structure
        && context.accelerationStructureFeatureEnabled
    ){
        if(desc.isAccelStructBuildInput)
            usage |= VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT;
        if(desc.isAccelStructStorage)
            usage |= VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT;
    }
    if(context.extensions.buffer_device_address)
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return usage;
}

[[nodiscard]] inline bool IsBufferUsageCompatibleWithDescription(
    const BufferDesc& desc,
    const VkBufferUsageFlags usage
)noexcept{
    const VkBufferUsageFlags requiredUsage = RequiredBufferUsageForDescription(desc);
    return (usage & requiredUsage) == requiredUsage;
}

[[nodiscard]] inline bool IsBufferUsageCompatibleWithResourceStates(
    const BufferDesc& desc,
    const VkBufferUsageFlags usage,
    const ResourceStates::Mask states
)noexcept{
    if(!IsBufferDescriptionCompatibleWithResourceStates(desc, states))
        return false;
    const VkBufferUsageFlags requiredUsage = RequiredBufferUsageForResourceStates(desc, states);
    return (usage & requiredUsage) == requiredUsage;
}

[[nodiscard]] inline bool IsBufferUsageConsistent(
    const VulkanContext& context,
    const BufferDesc& desc,
    const bool managed,
    const VkBufferUsageFlags usage,
    const VkBufferUsageFlags requiredUsage = 0u
)noexcept{
    if(!IsBufferUsageSupportedByDevice(context, usage))
        return false;
    if(!IsBufferUsageCompatibleWithDescription(desc, usage))
        return false;
    if(!IsBufferUsageCompatibleWithResourceStates(desc, usage, desc.initialState))
        return false;
    if((usage & requiredUsage) != requiredUsage)
        return false;
    return !managed || usage == PickManagedBufferUsage(context, desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

