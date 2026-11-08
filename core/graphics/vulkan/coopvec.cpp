// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "coopvec_feature_query.h"
#include "coopvec_matrix_policy.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/overflow.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<CooperativeVectorDeviceFeatures> Device::queryCoopVecFeatures(){
    return CooperativeVectorFeatureQuery::query(m_context, taskScheduler());
}

Expected<usize> Device::getCoopVecMatrixSize(CooperativeVectorDataType::Enum type, CooperativeVectorMatrixLayout::Enum layout, i32 rows, i32 columns){
    VkResult res = VK_SUCCESS;

    if(type > CooperativeVectorDataType::Float64 || layout > CooperativeVectorMatrixLayout::TrainingOptimal || type == CooperativeVectorDataType::UInt8Packed || type == CooperativeVectorDataType::SInt8Packed || rows <= 0 || columns <= 0)
        return MakeUnexpected(Failure{});
    if((type == CooperativeVectorDataType::FloatE4M3 || type == CooperativeVectorDataType::FloatE5M2) && (layout == CooperativeVectorMatrixLayout::RowMajor || layout == CooperativeVectorMatrixLayout::ColumnMajor))
        return MakeUnexpected(Failure{});
    if(!m_context.extensions.nvCooperativeVector || m_context.coopVecFeatures.cooperativeVector != VK_TRUE || !m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV || !m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV)
        return MakeUnexpected(Failure{});

    const VkComponentTypeKHR componentType = VulkanDetail::ConvertCoopVecDataType(type);
    if(componentType != VK_COMPONENT_TYPE_FLOAT32_KHR){
        uint32_t propertyCount = 0u;
        res = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, nullptr);
        if(res != VK_SUCCESS || propertyCount == 0u)
            return MakeUnexpected(Failure{});

        Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorQueryArena);
        Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> properties(propertyCount, scratchArena);
        for(VkCooperativeVectorPropertiesNV& property : properties){
            property.sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
            property.pNext = nullptr;
        }

        res = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &propertyCount, properties.data());
        if(res != VK_SUCCESS)
            return MakeUnexpected(Failure{});

        b8 matrixInterpretationSupported = false;
        for(u32 i = 0u; i < propertyCount; ++i){
            if(properties[i].matrixInterpretation == componentType){
                matrixInterpretationSupported = true;
                break;
            }
        }
        if(!matrixInterpretationSupported)
            return MakeUnexpected(Failure{});
    }

    usize dstSize = 0;
    const usize dataTypeSize = GetCooperativeVectorDataTypeSize(type);
    const usize rowCount = static_cast<usize>(rows);
    const usize columnCount = static_cast<usize>(columns);
    const auto rowByteSize = TryMultiply<usize>(columnCount, dataTypeSize);
    if(!rowByteSize)
        return MakeUnexpected(Failure{});

    const auto srcStride = GetCooperativeVectorOptimalMatrixStride(type, CooperativeVectorMatrixLayout::RowMajor, static_cast<u32>(rows), static_cast<u32>(columns));
    if(!srcStride || *srcStride == 0u)
        return MakeUnexpected(Failure{});

    const auto precedingRowsByteSize = TryMultiply<usize>(rowCount - 1u, *srcStride);
    if(!precedingRowsByteSize)
        return MakeUnexpected(Failure{});
    if(AddOverflows<usize>(*precedingRowsByteSize, *rowByteSize))
        return MakeUnexpected(Failure{});
    const usize srcSize = *precedingRowsByteSize + *rowByteSize;

    const auto dstStride = GetCooperativeVectorOptimalMatrixStride(type, layout, static_cast<u32>(rows), static_cast<u32>(columns));
    if(!dstStride || ((layout == CooperativeVectorMatrixLayout::RowMajor || layout == CooperativeVectorMatrixLayout::ColumnMajor) && *dstStride == 0u))
        return MakeUnexpected(Failure{});

    auto convertInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
    convertInfo.srcSize = srcSize;
    convertInfo.srcData.hostAddress = nullptr;
    convertInfo.pDstSize = &dstSize;
    convertInfo.dstData.hostAddress = nullptr;
    convertInfo.srcComponentType = componentType;
    convertInfo.dstComponentType = convertInfo.srcComponentType;
    convertInfo.numRows = static_cast<u32>(rows);
    convertInfo.numColumns = static_cast<u32>(columns);
    convertInfo.srcLayout = VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_ROW_MAJOR_NV;
    convertInfo.srcStride = *srcStride;
    convertInfo.dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(layout);
    convertInfo.dstStride = *dstStride;

    res = m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV(m_context.device, &convertInfo);
    if(res == VK_SUCCESS)
        return dstSize;

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    constexpr TStringView s_OperationName = NWB_TEXT("convert cooperative-vector matrices");
    constexpr TStringView s_EnumerateComponentTypesFailedReason = NWB_TEXT("failed to enumerate cooperative-vector component types");
    constexpr TStringView s_MatrixQueryInvalidReason = NWB_TEXT("matrix type, layout, stride, or size query is invalid");
    constexpr TStringView s_MatrixRangeInvalidReason = NWB_TEXT("a matrix range is undersized, out of bounds, overflowing, or not 64-byte aligned");
    constexpr TStringView s_ConversionRangesOverlapReason = NWB_TEXT("cooperative-vector conversion memory ranges overlap");
    constexpr GpuQueueCapability::Mask s_ConvertCapabilities = static_cast<GpuQueueCapability::Mask>(static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute));
    if(!recordAndValidateAnyCommandCapability(s_ConvertCapabilities, s_OperationName))
        return;
    if(
        !m_context.extensions.nvCooperativeVector
        || !m_context.coopVecFeatures.cooperativeVector
        || !m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV
        || !m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV
        || !m_context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("cooperative-vector feature or entry points are unavailable"));
        return;
    }

    if(numDescs == 0)
        return;
    if(!convertDescs){
        rejectCommandRecording(s_OperationName, NWB_TEXT("descriptors are null"));
        return;
    }
    if(numDescs > UINT32_MAX || numDescs > Limit<usize>::s_Max / 2u){
        rejectCommandRecording(s_OperationName, NWB_TEXT("descriptor count exceeds supported limits"));
        return;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorConvertArena);

    u32 cooperativeVectorPropertyCount = 0u;
    VkResult result = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &cooperativeVectorPropertyCount, nullptr);
    if(result != VK_SUCCESS){
        rejectCommandRecording(s_OperationName, s_EnumerateComponentTypesFailedReason);
        return;
    }

    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> cooperativeVectorProperties(cooperativeVectorPropertyCount, scratchArena);
    for(VkCooperativeVectorPropertiesNV& property : cooperativeVectorProperties){
        property.sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
        property.pNext = nullptr;
    }
    if(cooperativeVectorPropertyCount != 0u){
        result = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(m_context.physicalDevice, &cooperativeVectorPropertyCount, cooperativeVectorProperties.data());
        if(result != VK_SUCCESS){
            rejectCommandRecording(s_OperationName, s_EnumerateComponentTypesFailedReason);
            return;
        }
    }

    struct BufferStateEntry{
        NotNull<Buffer*> buffer;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };
    Vector<BufferStateEntry, Alloc::ScratchArena> requiredBufferStates{scratchArena};
    requiredBufferStates.reserve(numDescs * 2u);

    using DeviceAddressRange = CooperativeVectorMatrixPolicy::DeviceAddressRange;
    Vector<DeviceAddressRange, Alloc::ScratchArena> accessedRanges{scratchArena};
    accessedRanges.reserve(numDescs * 2u);

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchArena> vkConvertDescs(numDescs, scratchArena);
    Vector<usize, Alloc::ScratchArena> dstSizes(numDescs, scratchArena);

    const auto addRequiredBufferState = [&requiredBufferStates](Buffer& buffer, const ResourceStates::Mask requiredState)noexcept -> b8{
        for(BufferStateEntry& state : requiredBufferStates){
            if(state.buffer.get() == &buffer){
                state.state |= requiredState;
                return true;
            }
            if(buffer.m_buffer != VK_NULL_HANDLE && state.buffer->m_buffer == buffer.m_buffer)
                return false;
        }
        requiredBufferStates.push_back(BufferStateEntry{ MakeNotNull(&buffer), requiredState });
        return true;
    };

    for(usize i = 0u; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = convertDescs[i];
        if(!convertDesc.src.buffer || !convertDesc.dst.buffer){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a source or destination buffer is null"));
            return;
        }
        if(convertDesc.numRows == 0u || convertDesc.numColumns == 0u){
            rejectCommandRecording(s_OperationName, NWB_TEXT("matrix dimensions must be nonzero"));
            return;
        }

        if(
            convertDesc.src.type > CooperativeVectorDataType::Float64
            || convertDesc.dst.type > CooperativeVectorDataType::Float64
            || convertDesc.src.layout > CooperativeVectorMatrixLayout::TrainingOptimal
            || convertDesc.dst.layout > CooperativeVectorMatrixLayout::TrainingOptimal
            || convertDesc.src.type == CooperativeVectorDataType::UInt8Packed
            || convertDesc.src.type == CooperativeVectorDataType::SInt8Packed
            || convertDesc.dst.type == CooperativeVectorDataType::UInt8Packed
            || convertDesc.dst.type == CooperativeVectorDataType::SInt8Packed
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("matrix type or layout is invalid"));
            return;
        }

        VkComponentTypeKHR srcComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.src.type);
        VkComponentTypeKHR dstComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.dst.type);
        VkCooperativeVectorMatrixLayoutNV srcLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.src.layout);
        VkCooperativeVectorMatrixLayoutNV dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.dst.layout);
        if(!CooperativeVectorMatrixPolicy::isComponentTypeSupported(srcComponentType, cooperativeVectorProperties.data(), cooperativeVectorPropertyCount) || !CooperativeVectorMatrixPolicy::isComponentTypeSupported(dstComponentType, cooperativeVectorProperties.data(), cooperativeVectorPropertyCount)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a matrix component type is unsupported"));
            return;
        }
        if(!CooperativeVectorMatrixPolicy::isTypeConversionSupported(srcComponentType, dstComponentType)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("the matrix component type conversion is unsupported"));
            return;
        }
        if(CooperativeVectorMatrixPolicy::isFloat8(dstComponentType) && dstLayout != VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV && dstLayout != VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL_NV){
            rejectCommandRecording(s_OperationName, NWB_TEXT("float8 destinations require an optimal matrix layout"));
            return;
        }

        const auto sourceLayout = CooperativeVectorMatrixPolicy::resolveMatrixLayout(m_context, convertDesc.src, convertDesc.numRows, convertDesc.numColumns);
        if(!sourceLayout){
            rejectCommandRecording(s_OperationName, s_MatrixQueryInvalidReason);
            return;
        }
        const auto destinationLayout = CooperativeVectorMatrixPolicy::resolveMatrixLayout(m_context, convertDesc.dst, convertDesc.numRows, convertDesc.numColumns);
        if(!destinationLayout){
            rejectCommandRecording(s_OperationName, s_MatrixQueryInvalidReason);
            return;
        }

        const auto srcRange = CooperativeVectorMatrixPolicy::checkedDeviceRange(*convertDesc.src.buffer, convertDesc.src.offset, convertDesc.src.size, sourceLayout->byteSize);
        if(!srcRange){
            rejectCommandRecording(s_OperationName, s_MatrixRangeInvalidReason);
            return;
        }
        const auto dstRange = CooperativeVectorMatrixPolicy::checkedDeviceRange(*convertDesc.dst.buffer, convertDesc.dst.offset, convertDesc.dst.size, destinationLayout->byteSize);
        if(!dstRange){
            rejectCommandRecording(s_OperationName, s_MatrixRangeInvalidReason);
            return;
        }

        for(const DeviceAddressRange& existingRange : accessedRanges){
            if(UnsignedRangesOverlap(existingRange.address, existingRange.byteSize, srcRange->address, srcRange->byteSize) || UnsignedRangesOverlap(existingRange.address, existingRange.byteSize, dstRange->address, dstRange->byteSize)){
                rejectCommandRecording(s_OperationName, s_ConversionRangesOverlapReason);
                return;
            }
        }
        if(UnsignedRangesOverlap(srcRange->address, srcRange->byteSize, dstRange->address, dstRange->byteSize)){
            rejectCommandRecording(s_OperationName, s_ConversionRangesOverlapReason);
            return;
        }
        accessedRanges.push_back(*srcRange);
        accessedRanges.push_back(*dstRange);

        if(!addRequiredBufferState(*convertDesc.src.buffer, ResourceStates::ConvertCoopVecMatrixInput) || !addRequiredBufferState(*convertDesc.dst.buffer, ResourceStates::ConvertCoopVecMatrixOutput)){
            rejectCommandRecording(s_OperationName, s_NativeBufferAliasReason);
            return;
        }

        dstSizes[i] = destinationLayout->byteSize;
        auto vkDesc = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
        vkDesc.srcSize = sourceLayout->byteSize;
        vkDesc.srcData.deviceAddress = srcRange->address;
        vkDesc.pDstSize = &dstSizes[i];
        vkDesc.dstData.deviceAddress = dstRange->address;
        vkDesc.srcComponentType = srcComponentType;
        vkDesc.dstComponentType = dstComponentType;
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;
        vkDesc.srcLayout = srcLayout;
        vkDesc.srcStride = sourceLayout->stride;
        vkDesc.dstLayout = dstLayout;
        vkDesc.dstStride = destinationLayout->stride;
        vkConvertDescs[i] = vkDesc;
    }

    for(const BufferStateEntry& state : requiredBufferStates){
        if(!validateBufferForGpuState(state.buffer.get(), state.state, s_OperationName, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
            return;
    }

    endActiveRenderPass();
    if(m_enableAutomaticBarriers){
        for(const BufferStateEntry& state : requiredBufferStates){
            setBufferState(state.buffer.get(), state.state);
            if(m_commandRecordingFailed)
                return;
        }
    }

    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    m_context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV(m_currentCmdBuf->m_cmdBuf, static_cast<u32>(numDescs), vkConvertDescs.data());
    for(usize i = 0u; i < numDescs; ++i){
        retainResource(convertDescs[i].src.buffer);
        retainResource(convertDescs[i].dst.buffer);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

