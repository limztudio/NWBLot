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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 BufferDescriptionsEqual(const BufferDesc& lhs, const BufferDesc& rhs)noexcept{
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

[[nodiscard]] constexpr b8 IsBufferResourceStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states != ResourceStates::Unknown && (states & ~s_ValidBufferResourceStates) == 0u;
}

[[nodiscard]] constexpr b8 IsBufferCreationStateMaskValid(const ResourceStates::Mask states)noexcept{
    return states == ResourceStates::Unknown || IsBufferResourceStateMaskValid(states);
}

[[nodiscard]] constexpr b8 HasStorageBufferUsage(const BufferDesc& desc)noexcept{
    return desc.structStride != 0u || desc.canHaveUAVs || desc.canHaveRawViews;
}

[[nodiscard]] b8 IsBufferDescriptionCompatibleWithResourceStates(const BufferDesc& desc, ResourceStates::Mask states)noexcept;
[[nodiscard]] VkBufferUsageFlags RequiredBufferUsageForResourceStates(const BufferDesc& desc, ResourceStates::Mask states)noexcept;
[[nodiscard]] VkBufferUsageFlags RequiredBufferUsageForDescription(const BufferDesc& desc)noexcept;
[[nodiscard]] b8 IsBufferUsageSupportedByDevice(const VulkanContext& context, VkBufferUsageFlags usage)noexcept;
[[nodiscard]] VkBufferUsageFlags PickManagedBufferUsage(const VulkanContext& context, const BufferDesc& desc)noexcept;
[[nodiscard]] b8 IsBufferUsageCompatibleWithDescription(const BufferDesc& desc, VkBufferUsageFlags usage)noexcept;
[[nodiscard]] b8 IsBufferUsageCompatibleWithResourceStates(const BufferDesc& desc, VkBufferUsageFlags usage, ResourceStates::Mask states)noexcept;
[[nodiscard]] b8 IsBufferUsageConsistent(const VulkanContext& context, const BufferDesc& desc, b8 managed, VkBufferUsageFlags usage)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

