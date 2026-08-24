// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


// Push Constants


void CommandList::setPushConstants(const void* data, usize byteSize){
    if(byteSize == 0)
        return;
    if(!VulkanDetail::AreAllPointersValid(data)){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("data is null"));
        return;
    }
    if(byteSize > UINT32_MAX){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("byte size exceeds uint32 range"));
        return;
    }

    const u32 pushConstantByteSize = static_cast<u32>(byteSize);
    if(!VulkanDetail::IsPushConstantByteSizeValid(pushConstantByteSize, m_context.physicalDeviceProperties.limits.maxPushConstantsSize)){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("byte size is unaligned or exceeds the device limit"));
        return;
    }

    VkPipelineLayout layout = VK_NULL_HANDLE;
    GpuQueueCapability::Mask requiredCapabilities = GpuQueueCapability::None;
    u32 activePipelineCount = 0u;
    u32 pipelinePushConstantByteSize = 0;

    const auto selectPipeline = [&](
        const VkPipelineLayout candidateLayout,
        const u32 candidatePushConstantByteSize,
        const GpuQueueCapability::Mask candidateCapabilities
    ){
        ++activePipelineCount;
        layout = candidateLayout;
        requiredCapabilities = candidateCapabilities;
        pipelinePushConstantByteSize = candidatePushConstantByteSize;
    };

    if(m_currentGraphicsState.pipeline){
        auto* gp = m_currentGraphicsState.pipeline;
        selectPipeline(gp->m_pipelineLayout, gp->m_pushConstantByteSize, GpuQueueCapability::Graphics);
    }
    if(m_currentComputeState.pipeline){
        auto* cp = m_currentComputeState.pipeline;
        selectPipeline(cp->m_pipelineLayout, cp->m_pushConstantByteSize, GpuQueueCapability::Compute);
    }
    if(m_currentMeshletState.pipeline){
        auto* mp = m_currentMeshletState.pipeline;
        selectPipeline(mp->m_pipelineLayout, mp->m_pushConstantByteSize, GpuQueueCapability::Graphics);
    }
    if(m_currentRayTracingState.shaderTable){
        auto* rtp = m_currentRayTracingState.shaderTable->getPipeline();
        if(rtp)
            selectPipeline(rtp->m_pipelineLayout, rtp->m_pushConstantByteSize, GpuQueueCapability::Compute);
        else
            ++activePipelineCount;
    }

    if(activePipelineCount != 1u || layout == VK_NULL_HANDLE){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("exactly one active valid pipeline layout is required"));
        return;
    }
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("set push constants")))
        return;

    if(pipelinePushConstantByteSize == 0){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("byte size exceeds the active pipeline push constant range"));
        return;
    }

    vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Acceleration Structure


void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("execute cluster acceleration operation")))
        return;
    if(!m_context.extensions.NV_cluster_acceleration_structure)
        return;

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(opDesc.params, inputInfo, moveInput, clusterInput, blasInput, NWB_TEXT("execute cluster operation")))
        return;

    auto* indirectArgCountBuffer = opDesc.inIndirectArgCountBuffer ? opDesc.inIndirectArgCountBuffer : nullptr;
    auto* indirectArgsBuffer = opDesc.inIndirectArgsBuffer ? opDesc.inIndirectArgsBuffer : nullptr;
    auto* inOutAddressesBuffer = opDesc.inOutAddressesBuffer ? opDesc.inOutAddressesBuffer : nullptr;
    auto* outSizesBuffer = opDesc.outSizesBuffer ? opDesc.outSizesBuffer : nullptr;
    auto* outAccelerationStructuresBuffer = opDesc.outAccelerationStructuresBuffer ? opDesc.outAccelerationStructuresBuffer : nullptr;

    if(indirectArgCountBuffer && opDesc.inIndirectArgCountOffsetInBytes >= indirectArgCountBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-arg-count offset is out of range."));
        return;
    }
    if(indirectArgsBuffer && opDesc.inIndirectArgsOffsetInBytes >= indirectArgsBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation indirect-args offset is out of range."));
        return;
    }
    if(inOutAddressesBuffer && opDesc.inOutAddressesOffsetInBytes >= inOutAddressesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation in/out-addresses offset is out of range."));
        return;
    }
    if(outSizesBuffer && opDesc.outSizesOffsetInBytes >= outSizesBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation out-sizes offset is out of range."));
        return;
    }
    if(outAccelerationStructuresBuffer && opDesc.outAccelerationStructuresOffsetInBytes >= outAccelerationStructuresBuffer->getDescription().byteSize){
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Cluster operation output AS offset is out of range."));
        return;
    }

    if(m_enableAutomaticBarriers){
        constexpr ResourceStates::Mask s_ClusterIndirectInputState = static_cast<ResourceStates::Mask>(
            static_cast<u32>(ResourceStates::AccelStructBuildInput) | static_cast<u32>(ResourceStates::IndirectArgument)
        );
        if(indirectArgsBuffer)
            setBufferState(opDesc.inIndirectArgsBuffer, s_ClusterIndirectInputState);
        if(indirectArgCountBuffer)
            setBufferState(opDesc.inIndirectArgCountBuffer, s_ClusterIndirectInputState);
        if(inOutAddressesBuffer)
            setBufferState(opDesc.inOutAddressesBuffer, ResourceStates::AccelStructWrite);
        if(outSizesBuffer)
            setBufferState(opDesc.outSizesBuffer, ResourceStates::AccelStructWrite);
        if(outAccelerationStructuresBuffer)
            setBufferState(opDesc.outAccelerationStructuresBuffer, ResourceStates::AccelStructWrite);
    }
    if(m_commandRecordingFailed)
        return;

    if(indirectArgCountBuffer)
        retainResource(opDesc.inIndirectArgCountBuffer);
    if(indirectArgsBuffer)
        retainResource(opDesc.inIndirectArgsBuffer);
    if(inOutAddressesBuffer)
        retainResource(opDesc.inOutAddressesBuffer);
    if(outSizesBuffer)
        retainResource(opDesc.outSizesBuffer);
    if(outAccelerationStructuresBuffer)
        retainResource(opDesc.outAccelerationStructuresBuffer);

    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    BufferHandle scratchBufferHandle;
    Buffer* scratchBuffer = nullptr;
    if(opDesc.scratchSizeInBytes > 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = opDesc.scratchSizeInBytes;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "ClusterOp_Scratch";
        scratchDesc.canHaveUAVs = true;

        scratchBufferHandle = m_device.createBuffer(scratchDesc);
        if(!scratchBufferHandle){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate cluster operation scratch buffer"));
            return;
        }

        scratchBuffer = scratchBufferHandle.get();
    }

    auto commandsInfo = VulkanDetail::MakeVkStruct<VkClusterAccelerationStructureCommandsInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV);
    commandsInfo.input = inputInfo;
    commandsInfo.scratchData = scratchBuffer ? scratchBuffer->m_deviceAddress : 0;
    commandsInfo.dstImplicitData = outAccelerationStructuresBuffer ? outAccelerationStructuresBuffer->m_deviceAddress + opDesc.outAccelerationStructuresOffsetInBytes : 0;

    if(inOutAddressesBuffer){
        commandsInfo.dstAddressesArray.deviceAddress = inOutAddressesBuffer->m_deviceAddress + opDesc.inOutAddressesOffsetInBytes;
        commandsInfo.dstAddressesArray.stride = inOutAddressesBuffer->getDescription().structStride;
        commandsInfo.dstAddressesArray.size = inOutAddressesBuffer->getDescription().byteSize - opDesc.inOutAddressesOffsetInBytes;
    }

    if(outSizesBuffer){
        commandsInfo.dstSizesArray.deviceAddress = outSizesBuffer->m_deviceAddress + opDesc.outSizesOffsetInBytes;
        commandsInfo.dstSizesArray.stride = outSizesBuffer->getDescription().structStride;
        commandsInfo.dstSizesArray.size = outSizesBuffer->getDescription().byteSize - opDesc.outSizesOffsetInBytes;
    }

    if(indirectArgsBuffer){
        commandsInfo.srcInfosArray.deviceAddress = indirectArgsBuffer->m_deviceAddress + opDesc.inIndirectArgsOffsetInBytes;
        commandsInfo.srcInfosArray.stride = indirectArgsBuffer->getDescription().structStride;
        commandsInfo.srcInfosArray.size = indirectArgsBuffer->getDescription().byteSize - opDesc.inIndirectArgsOffsetInBytes;
    }

    commandsInfo.srcInfosCount = indirectArgCountBuffer ? indirectArgCountBuffer->m_deviceAddress + opDesc.inIndirectArgCountOffsetInBytes : 0;
    commandsInfo.addressResolutionFlags = static_cast<VkClusterAccelerationStructureAddressResolutionFlagsNV>(0);

    vkCmdBuildClusterAccelerationStructureIndirectNV(m_currentCmdBuf->m_cmdBuf, &commandsInfo);

    if(scratchBufferHandle)
        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBufferHandle));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cooperative Vector


void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    constexpr GpuQueueCapability::Mask s_ConvertCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute)
    );
    if(!recordAndValidateAnyCommandCapability(s_ConvertCapabilities, NWB_TEXT("convert cooperative-vector matrices")))
        return;
    if(!m_context.extensions.NV_cooperative_vector || !m_context.coopVecFeatures.cooperativeVector)
        return;

    if(numDescs == 0)
        return;
    if(!convertDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptors are null"));
        return;
    }
    if(numDescs > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_CooperativeVectorConvertArena);

    Vector<CooperativeVectorConvertMatrixLayoutDesc const*, Alloc::ScratchArena> validDescs{scratchArena};
    validDescs.reserve(numDescs);

    for(usize i = 0; i < numDescs; ++i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = convertDescs[i];
        if(!convertDesc.src.buffer || !convertDesc.dst.buffer)
            continue;

        auto* srcBuffer = convertDesc.src.buffer;
        auto* dstBuffer = convertDesc.dst.buffer;
        if(!srcBuffer || !dstBuffer){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: buffer is invalid"));
            continue;
        }
        if(!VulkanDetail::IsBufferRangeInBounds(srcBuffer->m_desc, convertDesc.src.offset, convertDesc.src.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: source range is outside the buffer"));
            continue;
        }
        if(!VulkanDetail::IsBufferRangeInBounds(dstBuffer->m_desc, convertDesc.dst.offset, convertDesc.dst.size)){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Skipping cooperative vector matrix conversion: destination range is outside the buffer"));
            continue;
        }

        if(m_enableAutomaticBarriers){
            setBufferState(convertDesc.src.buffer, ResourceStates::ConvertCoopVecMatrixInput);
            setBufferState(convertDesc.dst.buffer, ResourceStates::ConvertCoopVecMatrixOutput);
        }

        validDescs.push_back(&convertDesc);
    }
    if(m_commandRecordingFailed)
        return;

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchArena> vkConvertDescs(validDescs.size(), scratchArena);
    Vector<usize, Alloc::ScratchArena> dstSizes(validDescs.size(), scratchArena);

    auto buildConvertDesc = [&](usize i){
        const CooperativeVectorConvertMatrixLayoutDesc& convertDesc = *validDescs[i];
        dstSizes[i] = convertDesc.dst.size;

        auto vkDesc = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV);
        vkDesc.srcSize = convertDesc.src.size;
        vkDesc.srcData.deviceAddress = convertDesc.src.buffer->m_deviceAddress + convertDesc.src.offset;
        vkDesc.pDstSize = &dstSizes[i];
        vkDesc.dstData.deviceAddress = convertDesc.dst.buffer->m_deviceAddress + convertDesc.dst.offset;
        vkDesc.srcComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.src.type);
        vkDesc.dstComponentType = VulkanDetail::ConvertCoopVecDataType(convertDesc.dst.type);
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;

        vkDesc.srcLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.src.layout);
        vkDesc.srcStride =
            convertDesc.src.stride != 0
            ? convertDesc.src.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.src.type, convertDesc.src.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkDesc.dstLayout = VulkanDetail::ConvertCoopVecMatrixLayout(convertDesc.dst.layout);
        vkDesc.dstStride =
            convertDesc.dst.stride != 0
            ? convertDesc.dst.stride
            : GetCooperativeVectorOptimalMatrixStride(convertDesc.dst.type, convertDesc.dst.layout, convertDesc.numRows, convertDesc.numColumns)
        ;

        vkConvertDescs[i] = vkDesc;
    };

    if(taskPool().isParallelEnabled() && validDescs.size() >= s_ParallelConvertThreshold)
        scheduleParallelFor(static_cast<usize>(0), validDescs.size(), s_ConvertGrainSize, buildConvertDesc);
    else{
        for(usize i = 0; i < validDescs.size(); ++i)
            buildConvertDesc(i);
    }

    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    if(vkConvertDescs.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to convert cooperative vector matrices: descriptor count exceeds Vulkan limit"));
        return;
    }

    if(!vkConvertDescs.empty()){
        vkCmdConvertCooperativeVectorMatrixNV(m_currentCmdBuf->m_cmdBuf, static_cast<u32>(vkConvertDescs.size()), vkConvertDescs.data());
        for(const CooperativeVectorConvertMatrixLayoutDesc* convertDesc : validDescs){
            retainResource(convertDesc->src.buffer);
            retainResource(convertDesc->dst.buffer);
        }
    }
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

