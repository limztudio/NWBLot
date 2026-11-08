// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend.h"

#include <core/graphics/vulkan/coopvec_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


class CooperativeVectorMatrixPolicy final{
public:
    using StandardMatrixLayout = ::NWB::Core::VulkanCooperativeStandardMatrixLayout;


public:
    struct MatrixLayout{
        VkComponentTypeKHR componentType = VK_COMPONENT_TYPE_FLOAT32_KHR;
        VkCooperativeVectorMatrixLayoutNV layout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
        usize stride = 0u;
        usize byteSize = 0u;
    };
    struct DeviceAddressRange{
        VkDeviceAddress address = 0u;
        u64 byteSize = 0u;
    };

    [[nodiscard]] static b8 IsComponentTypeSupported(VkComponentTypeKHR componentType, const VkCooperativeVectorPropertiesNV* properties, u32 propertyCount)noexcept;
    [[nodiscard]] static b8 IsFloat8(VkComponentTypeKHR componentType)noexcept;
    [[nodiscard]] static b8 IsTypeConversionSupported(VkComponentTypeKHR srcType, VkComponentTypeKHR dstType)noexcept;
    [[nodiscard]] static Expected<StandardMatrixLayout> StandardLayoutSize(usize elementByteSize, u32 numRows, u32 numColumns, CooperativeVectorMatrixLayout::Enum layout, usize requestedStride)noexcept;
    [[nodiscard]] static Expected<MatrixLayout> ResolveMatrixLayout(const VulkanContext& context, const CooperativeVectorMatrixLayoutDesc& layoutDesc, u32 numRows, u32 numColumns)noexcept;
    [[nodiscard]] static Expected<DeviceAddressRange> CheckedDeviceRange(Buffer& buffer, u64 offset, usize declaredByteSize, usize requiredByteSize)noexcept;

private:
    [[nodiscard]] static b8 IsLowerPrecisionFloat(VkComponentTypeKHR componentType)noexcept;
    [[nodiscard]] static Expected<usize> QueryOptimalLayoutSize(const VulkanContext& context, VkComponentTypeKHR componentType, VkCooperativeVectorMatrixLayoutNV layout, u32 numRows, u32 numColumns)noexcept;
};


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

