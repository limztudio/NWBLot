// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coopvec_matrix_policy.h"

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


b8 CooperativeVectorMatrixPolicy::isComponentTypeSupported(const VkComponentTypeKHR componentType, const VkCooperativeVectorPropertiesNV* const cooperativeVectorProperties, const u32 cooperativeVectorPropertyCount)noexcept{
    if(componentType == VK_COMPONENT_TYPE_FLOAT32_KHR)
        return true;
    for(u32 i = 0u; i < cooperativeVectorPropertyCount; ++i){
        if(cooperativeVectorProperties[i].matrixInterpretation == componentType)
            return true;
    }
    return false;
}

b8 CooperativeVectorMatrixPolicy::isLowerPrecisionFloat(const VkComponentTypeKHR componentType)noexcept{
    switch(componentType){
    case VK_COMPONENT_TYPE_FLOAT_E4M3_NV:
    case VK_COMPONENT_TYPE_FLOAT_E5M2_NV:
    case VK_COMPONENT_TYPE_FLOAT16_KHR:
    case VK_COMPONENT_TYPE_BFLOAT16_KHR:
        return true;
    default:
        return false;
    }
}

b8 CooperativeVectorMatrixPolicy::isFloat8(const VkComponentTypeKHR componentType)noexcept{
    return componentType == VK_COMPONENT_TYPE_FLOAT_E4M3_NV || componentType == VK_COMPONENT_TYPE_FLOAT_E5M2_NV;
}

b8 CooperativeVectorMatrixPolicy::isTypeConversionSupported(const VkComponentTypeKHR srcType, const VkComponentTypeKHR dstType)noexcept{
    if(srcType == dstType)
        return true;
    if(srcType == VK_COMPONENT_TYPE_FLOAT32_KHR)
        return isLowerPrecisionFloat(dstType);
    if(dstType == VK_COMPONENT_TYPE_FLOAT32_KHR)
        return isLowerPrecisionFloat(srcType);
    if(srcType == VK_COMPONENT_TYPE_FLOAT16_KHR)
        return isFloat8(dstType);
    if(dstType == VK_COMPONENT_TYPE_FLOAT16_KHR)
        return isFloat8(srcType);
    return false;
}

Expected<CooperativeVectorMatrixPolicy::StandardMatrixLayout> CooperativeVectorMatrixPolicy::standardLayoutSize(const usize elementByteSize, const u32 numRows, const u32 numColumns, const CooperativeVectorMatrixLayout::Enum layout, const usize requestedStride)noexcept{
    const usize minorElementCount = layout == CooperativeVectorMatrixLayout::RowMajor ? static_cast<usize>(numColumns) : static_cast<usize>(numRows);
    const usize majorElementCount = layout == CooperativeVectorMatrixLayout::RowMajor ? static_cast<usize>(numRows) : static_cast<usize>(numColumns);
    const auto minorByteSize = TryMultiply<usize>(minorElementCount, elementByteSize);
    if(!minorByteSize)
        return MakeUnexpected(Failure{});

    usize stride = requestedStride;
    if(stride == 0u){
        if(AddOverflows<usize>(*minorByteSize, elementByteSize))
            return MakeUnexpected(Failure{});
        stride = *minorByteSize + elementByteSize;
    }
    if(stride <= *minorByteSize || stride % elementByteSize != 0u)
        return MakeUnexpected(Failure{});

    const auto precedingMajorBytes = TryMultiply<usize>(majorElementCount - 1u, stride);
    if(!precedingMajorBytes || AddOverflows<usize>(*precedingMajorBytes, *minorByteSize))
        return MakeUnexpected(Failure{});
    const usize byteSize = *precedingMajorBytes + *minorByteSize;
    return StandardMatrixLayout{ .stride = stride, .byteSize = byteSize };
}

Expected<usize> CooperativeVectorMatrixPolicy::queryOptimalLayoutSize(const VulkanContext& context, const VkComponentTypeKHR componentType, const VkCooperativeVectorMatrixLayoutNV layout, const u32 numRows, const u32 numColumns)noexcept{
    const auto publicComponentType = VulkanDetail::ConvertCoopVecDataType(componentType);
    const usize elementByteSize = GetCooperativeVectorDataTypeSize(publicComponentType);
    const auto rowByteSize = TryMultiply<usize>(static_cast<usize>(numColumns), elementByteSize);
    if(!rowByteSize || AddOverflows<usize>(*rowByteSize, elementByteSize))
        return MakeUnexpected(Failure{});
    const usize srcStride = *rowByteSize + elementByteSize;
    const auto precedingRowsByteSize = TryMultiply<usize>(static_cast<usize>(numRows - 1u), srcStride);
    if(!precedingRowsByteSize || AddOverflows<usize>(*precedingRowsByteSize, *rowByteSize))
        return MakeUnexpected(Failure{});

    usize queriedByteSize = 0u;
    auto queryInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
    queryInfo.srcSize = *precedingRowsByteSize + *rowByteSize;
    queryInfo.srcData.hostAddress = nullptr;
    queryInfo.pDstSize = &queriedByteSize;
    queryInfo.dstData.hostAddress = nullptr;
    queryInfo.srcComponentType = componentType;
    queryInfo.dstComponentType = componentType;
    queryInfo.numRows = numRows;
    queryInfo.numColumns = numColumns;
    queryInfo.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    queryInfo.srcStride = srcStride;
    queryInfo.dstLayout = layout;
    queryInfo.dstStride = 0u;

    if(context.deviceDispatch.vkConvertCooperativeVectorMatrixNV(context.device, &queryInfo) != VK_SUCCESS || queriedByteSize == 0u)
        return MakeUnexpected(Failure{});
    return queriedByteSize;
}

Expected<CooperativeVectorMatrixPolicy::MatrixLayout> CooperativeVectorMatrixPolicy::resolveMatrixLayout(const VulkanContext& context, const CooperativeVectorMatrixLayoutDesc& layoutDesc, const u32 numRows, const u32 numColumns)noexcept{
    MatrixLayout resolved;
    resolved.componentType = VulkanDetail::ConvertCoopVecDataType(layoutDesc.type);
    resolved.layout = VulkanDetail::ConvertCoopVecMatrixLayout(layoutDesc.layout);
    if(layoutDesc.layout == CooperativeVectorMatrixLayout::RowMajor || layoutDesc.layout == CooperativeVectorMatrixLayout::ColumnMajor){
        const auto standard = standardLayoutSize(GetCooperativeVectorDataTypeSize(layoutDesc.type), numRows, numColumns, layoutDesc.layout, layoutDesc.stride);
        if(!standard)
            return MakeUnexpected(standard.error());
        resolved.stride = standard->stride;
        resolved.byteSize = standard->byteSize;
    }
    else{
        const auto byteSize = queryOptimalLayoutSize(context, resolved.componentType, resolved.layout, numRows, numColumns);
        if(!byteSize)
            return MakeUnexpected(byteSize.error());
        resolved.byteSize = *byteSize;
    }
    return resolved;
}

Expected<CooperativeVectorMatrixPolicy::DeviceAddressRange> CooperativeVectorMatrixPolicy::checkedDeviceRange(Buffer& buffer, const u64 offset, const usize declaredByteSize, const usize requiredByteSize)noexcept{
    NWB_ASSERT(requiredByteSize != 0u);
    if(declaredByteSize < requiredByteSize)
        return MakeUnexpected(Failure{});
    if(!VulkanDetail::IsBufferRangeInBounds(buffer.getCreationDescription(), offset, declaredByteSize))
        return MakeUnexpected(Failure{});

    const VkDeviceAddress baseAddress = buffer.getGpuVirtualAddress();
    if(baseAddress == 0u || baseAddress > Limit<u64>::s_Max - offset)
        return MakeUnexpected(Failure{});

    constexpr VkDeviceAddress s_MatrixRangeAlignment = 64u;
    const VkDeviceAddress address = baseAddress + offset;
    if(address % s_MatrixRangeAlignment != 0u)
        return MakeUnexpected(Failure{});
    if(static_cast<u64>(requiredByteSize) > Limit<u64>::s_Max - address)
        return MakeUnexpected(Failure{});

    return DeviceAddressRange{ address, static_cast<u64>(requiredByteSize) };
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

