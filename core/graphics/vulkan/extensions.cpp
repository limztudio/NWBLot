// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
    if(!VulkanDetail::IsPushConstantByteSizeValid(
        pushConstantByteSize,
        m_context.physicalDeviceProperties.limits.maxPushConstantsSize
    )){
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
        rejectCommandRecording(
            NWB_TEXT("set push constants"),
            NWB_TEXT("exactly one active valid pipeline layout is required")
        );
        return;
    }
    if(!recordAndValidateCommandCapability(requiredCapabilities, NWB_TEXT("set push constants")))
        return;

    if(pipelinePushConstantByteSize == 0){
        rejectCommandRecording(NWB_TEXT("set push constants"), NWB_TEXT("active pipeline layout has no push constant range"));
        return;
    }
    if(pushConstantByteSize > pipelinePushConstantByteSize){
        rejectCommandRecording(
            NWB_TEXT("set push constants"),
            NWB_TEXT("byte size exceeds the active pipeline push constant range")
        );
        return;
    }

    m_context.deviceDispatch.vkCmdPushConstants(m_currentCmdBuf->m_cmdBuf, layout, VK_SHADER_STAGE_ALL, 0, pushConstantByteSize, data);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cluster Acceleration Structure


void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    constexpr const tchar* s_OperationName = NWB_TEXT("execute cluster acceleration operation");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(
        !m_context.extensions.NV_cluster_acceleration_structure
        || !m_context.clusterAccelerationStructureFeatureEnabled
        || !m_context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV
        || !m_context.deviceDispatch.vkCmdBuildClusterAccelerationStructureIndirectNV
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("cluster acceleration structure feature or entry points are unavailable"));
        return;
    }

    ShaderTable* const shaderTable = m_currentRayTracingState.shaderTable;
    RayTracingPipeline* const pipeline = shaderTable ? shaderTable->m_pipeline.get() : nullptr;
    if(
        !pipeline
        || pipeline->m_pipeline == VK_NULL_HANDLE
        || !pipeline->allowsClusterAccelerationStructures()
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("a cluster-enabled ray tracing pipeline must be bound"));
        return;
    }
    if(opDesc.params.maxArgCount == 0u){
        rejectCommandRecording(s_OperationName, NWB_TEXT("maximum argument count is zero"));
        return;
    }
    if(opDesc.params.type > RayTracingClusterOperationType::BlasBuild){
        rejectCommandRecording(s_OperationName, NWB_TEXT("operation type is invalid"));
        return;
    }
    if(opDesc.params.mode > RayTracingClusterOperationMode::GetSizes){
        rejectCommandRecording(s_OperationName, NWB_TEXT("operation mode is invalid"));
        return;
    }
    constexpr u32 s_SupportedOperationFlags =
        static_cast<u32>(RayTracingClusterOperationFlags::FastTrace)
        | static_cast<u32>(RayTracingClusterOperationFlags::FastBuild)
        | static_cast<u32>(RayTracingClusterOperationFlags::NoOverlap)
        | static_cast<u32>(RayTracingClusterOperationFlags::AllowOMM)
    ;
    if((static_cast<u32>(opDesc.params.flags) & ~s_SupportedOperationFlags) != 0u){
        rejectCommandRecording(s_OperationName, NWB_TEXT("operation flags contain unsupported bits"));
        return;
    }
    if(
        (opDesc.params.flags & RayTracingClusterOperationFlags::FastTrace)
        && (opDesc.params.flags & RayTracingClusterOperationFlags::FastBuild)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("fast-trace and fast-build flags are mutually exclusive"));
        return;
    }
    if(
        (opDesc.params.flags & RayTracingClusterOperationFlags::AllowOMM)
        && (
            !m_context.extensions.EXT_opacity_micromap
            || !m_context.opacityMicromapFeatureEnabled
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("cluster opacity micromaps are unavailable"));
        return;
    }
    if(
        (opDesc.params.flags & RayTracingClusterOperationFlags::NoOverlap)
        && opDesc.params.type != RayTracingClusterOperationType::Move
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("no-overlap is valid only for move operations"));
        return;
    }
    if(
        opDesc.params.type == RayTracingClusterOperationType::Move
        && opDesc.params.move.type > RayTracingClusterOperationMoveType::Template
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("move object type is invalid"));
        return;
    }

    auto* const indirectArgCountBuffer = opDesc.inIndirectArgCountBuffer;
    auto* const indirectArgsBuffer = opDesc.inIndirectArgsBuffer;
    auto* const inOutAddressesBuffer = opDesc.inOutAddressesBuffer;
    auto* const outSizesBuffer = opDesc.outSizesBuffer;
    auto* const outAccelerationStructuresBuffer = opDesc.outAccelerationStructuresBuffer;
    if(!indirectArgsBuffer){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect source-info array is required"));
        return;
    }
    if(!RayTracingClusterOperationMode::IsDestinationTopologyValid(
        opDesc.params.mode,
        inOutAddressesBuffer != nullptr,
        outSizesBuffer != nullptr,
        outAccelerationStructuresBuffer != nullptr
    )){
        switch(opDesc.params.mode){
        case RayTracingClusterOperationMode::ImplicitDestinations:
            rejectCommandRecording(s_OperationName, NWB_TEXT("implicit destinations require acceleration-structure output storage"));
            break;
        case RayTracingClusterOperationMode::ExplicitDestinations:
            rejectCommandRecording(s_OperationName, NWB_TEXT("explicit destinations require address and size arrays without implicit output storage"));
            break;
        case RayTracingClusterOperationMode::GetSizes:
            rejectCommandRecording(s_OperationName, NWB_TEXT("size-query mode requires only the size output buffer"));
            break;
        default:
            break;
        }
        return;
    }

    VkClusterAccelerationStructureInputInfoNV inputInfo{};
    VkClusterAccelerationStructureMoveObjectsInputNV moveInput{};
    VkClusterAccelerationStructureTriangleClusterInputNV clusterInput{};
    VkClusterAccelerationStructureClustersBottomLevelInputNV blasInput{};
    if(!VulkanDetail::BuildClusterOperationInputInfo(
        opDesc.params,
        inputInfo,
        moveInput,
        clusterInput,
        blasInput,
        s_OperationName
    )){
        rejectCommandRecording(s_OperationName, NWB_TEXT("operation parameters are invalid"));
        return;
    }

    if(
        opDesc.params.type == RayTracingClusterOperationType::ClasBuild
        || opDesc.params.type == RayTracingClusterOperationType::ClasBuildTemplates
        || opDesc.params.type == RayTracingClusterOperationType::ClasInstantiateTemplates
    ){
        const auto& properties = m_context.nvClusterAccelerationStructureProperties;
        if(
            opDesc.params.clas.maxTriangleCount > properties.maxTrianglesPerCluster
            || opDesc.params.clas.maxVertexCount > properties.maxVerticesPerCluster
            || opDesc.params.clas.maxGeometryIndex > properties.maxClusterGeometryIndex
            || opDesc.params.clas.minPositionTruncateBitCount > 32u
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("triangle-cluster parameters exceed device limits"));
            return;
        }

        VkFormatProperties formatProperties{};
        m_context.instanceDispatch.vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, clusterInput.vertexFormat, &formatProperties);
        if((formatProperties.bufferFeatures & VK_FORMAT_FEATURE_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR) == 0u){
            rejectCommandRecording(s_OperationName, NWB_TEXT("triangle-cluster vertex format lacks acceleration-structure support"));
            return;
        }
    }

    u64 sourceInfoSize = 0u;
    switch(opDesc.params.type){
    case RayTracingClusterOperationType::Move:                     sourceInfoSize = sizeof(VkClusterAccelerationStructureMoveObjectsInfoNV); break;
    case RayTracingClusterOperationType::ClasBuild:                sourceInfoSize = sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV); break;
    case RayTracingClusterOperationType::ClasBuildTemplates:       sourceInfoSize = sizeof(VkClusterAccelerationStructureBuildTriangleClusterTemplateInfoNV); break;
    case RayTracingClusterOperationType::ClasInstantiateTemplates: sourceInfoSize = sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV); break;
    case RayTracingClusterOperationType::BlasBuild:                sourceInfoSize = sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV); break;
    default:                                                       return;
    }

    const auto computeStridedRangeSize = [s_OperationName](
        const u64 stride,
        const u64 elementSize,
        const u32 count,
        const tchar* const rangeName,
        u64& outSize
    ) -> bool{
        const u64 spanCount = static_cast<u64>(count - 1u);
        if(stride != 0u && spanCount > (Limit<u64>::s_Max - elementSize) / stride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} range overflows"), s_OperationName, rangeName);
            return false;
        }
        outSize = spanCount * stride + elementSize;
        return true;
    };
    const auto getCheckedAddress = [s_OperationName](
        Buffer& buffer,
        const u64 offset,
        const u64 size,
        const u64 alignment,
        const tchar* const rangeName,
        VkDeviceAddress& outAddress
    ) -> bool{
        const VkDeviceAddress baseAddress = buffer.getGpuVirtualAddress();
        if(baseAddress == 0u || baseAddress > Limit<u64>::s_Max - offset){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device address is invalid or overflows"), s_OperationName, rangeName);
            return false;
        }
        outAddress = baseAddress + offset;
        if(outAddress == 0u || alignment == 0u || (outAddress % alignment) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device address is not aligned to {} bytes")
                , s_OperationName
                , rangeName
                , alignment
            );
            return false;
        }
        if(size == 0u || outAddress > Limit<u64>::s_Max - (size - 1u)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device-address range is empty or overflows"), s_OperationName, rangeName);
            return false;
        }
        return true;
    };

    const BufferDesc& sourceDesc = indirectArgsBuffer->getCreationDescription();
    if(sourceDesc.structStride != 0u && sourceDesc.structStride <= sourceInfoSize){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect source-info stride must be zero or greater than its structure size"));
        return;
    }
    const u64 sourceStride = sourceDesc.structStride != 0u ? sourceDesc.structStride : sourceInfoSize;
    u64 sourceRangeSize = 0u;
    if(
        !computeStridedRangeSize(sourceStride, sourceInfoSize, opDesc.params.maxArgCount, NWB_TEXT("source-info"), sourceRangeSize)
        || !VulkanDetail::IsBufferRangeInBounds(sourceDesc, opDesc.inIndirectArgsOffsetInBytes, sourceRangeSize)
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect source-info range is outside the buffer"));
        return;
    }

    u64 addressRangeSize = 0u;
    u64 sizeRangeSize = 0u;
    u64 addressStride = 0u;
    u64 sizeStride = 0u;
    if(outSizesBuffer){
        sizeStride = outSizesBuffer->getCreationDescription().structStride;
        if(sizeStride < sizeof(u32)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("size-array stride must be at least four bytes"));
            return;
        }
        if(
            !computeStridedRangeSize(sizeStride, sizeof(u32), opDesc.params.maxArgCount, NWB_TEXT("size-array"), sizeRangeSize)
            || !VulkanDetail::IsBufferRangeInBounds(
                outSizesBuffer->getCreationDescription(),
                opDesc.outSizesOffsetInBytes,
                sizeRangeSize
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("size-array range is outside the buffer"));
            return;
        }
    }
    if(inOutAddressesBuffer){
        addressStride = inOutAddressesBuffer->getCreationDescription().structStride;
        if(addressStride < sizeof(VkDeviceAddress)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("address-array stride must be at least eight bytes"));
            return;
        }
        if(
            !computeStridedRangeSize(
                addressStride,
                sizeof(VkDeviceAddress),
                opDesc.params.maxArgCount,
                NWB_TEXT("address-array"),
                addressRangeSize
            )
            || !VulkanDetail::IsBufferRangeInBounds(
                inOutAddressesBuffer->getCreationDescription(),
                opDesc.inOutAddressesOffsetInBytes,
                addressRangeSize
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("address-array range is outside the buffer"));
            return;
        }
    }

    if(
        indirectArgCountBuffer
        && !VulkanDetail::IsBufferRangeInBounds(
            indirectArgCountBuffer->getCreationDescription(),
            opDesc.inIndirectArgCountOffsetInBytes,
            sizeof(u32)
        )
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect count range is outside the buffer"));
        return;
    }

    constexpr ResourceStates::Mask s_ClusterIndirectInputState = static_cast<ResourceStates::Mask>(
        static_cast<u32>(ResourceStates::AccelStructBuildInput) | static_cast<u32>(ResourceStates::IndirectArgument)
    );
    constexpr VkBufferUsageFlags s_ClusterBuildInputUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    constexpr VkBufferUsageFlags s_ClusterStorageArrayUsage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    constexpr VkBufferUsageFlags s_ClusterAccelStructStorageUsage =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    if(
        !validateBufferForGpuState(
            indirectArgsBuffer,
            s_ClusterIndirectInputState,
            s_OperationName,
            s_ClusterBuildInputUsage
        )
        || (indirectArgCountBuffer && !validateBufferForGpuState(
            indirectArgCountBuffer,
            s_ClusterIndirectInputState,
            s_OperationName,
            s_ClusterBuildInputUsage
        ))
        || (outSizesBuffer && !validateBufferForGpuState(
            outSizesBuffer,
            ResourceStates::AccelStructWrite,
            s_OperationName,
            s_ClusterStorageArrayUsage
        ))
        || (inOutAddressesBuffer && !validateBufferForGpuState(
            inOutAddressesBuffer,
            ResourceStates::AccelStructWrite,
            s_OperationName,
            s_ClusterStorageArrayUsage
        ))
        || (outAccelerationStructuresBuffer && !validateBufferForGpuState(
            outAccelerationStructuresBuffer,
            ResourceStates::AccelStructWrite,
            s_OperationName,
            s_ClusterAccelStructStorageUsage
        ))
    )
        return;

    VkDeviceAddress sourceAddress = 0u;
    VkDeviceAddress sourceCountAddress = 0u;
    VkDeviceAddress addressArrayAddress = 0u;
    VkDeviceAddress sizeArrayAddress = 0u;
    if(
        !getCheckedAddress(
            *indirectArgsBuffer,
            opDesc.inIndirectArgsOffsetInBytes,
            sourceRangeSize,
            alignof(VkDeviceAddress),
            NWB_TEXT("source-info"),
            sourceAddress
        )
        || (indirectArgCountBuffer && !getCheckedAddress(
            *indirectArgCountBuffer,
            opDesc.inIndirectArgCountOffsetInBytes,
            sizeof(u32),
            alignof(u32),
            NWB_TEXT("source-info count"),
            sourceCountAddress
        ))
        || (outSizesBuffer && !getCheckedAddress(
            *outSizesBuffer,
            opDesc.outSizesOffsetInBytes,
            sizeRangeSize,
            alignof(u32),
            NWB_TEXT("size-array"),
            sizeArrayAddress
        ))
        || (inOutAddressesBuffer && !getCheckedAddress(
            *inOutAddressesBuffer,
            opDesc.inOutAddressesOffsetInBytes,
            addressRangeSize,
            alignof(VkDeviceAddress),
            NWB_TEXT("address-array"),
            addressArrayAddress
        ))
    ){
        rejectCommandRecording(s_OperationName, NWB_TEXT("a required device-address range is invalid"));
        return;
    }

    auto buildSize = VulkanDetail::MakeVkStruct<VkAccelerationStructureBuildSizesInfoKHR>(VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
    m_context.deviceDispatch.vkGetClusterAccelerationStructureBuildSizesNV(m_context.device, &inputInfo, &buildSize);
    if(opDesc.scratchSizeInBytes < buildSize.buildScratchSize){
        rejectCommandRecording(s_OperationName, NWB_TEXT("declared scratch size is smaller than the queried requirement"));
        return;
    }

    VkDeviceAddress destinationAddress = 0u;
    if(outAccelerationStructuresBuffer){
        u64 destinationSize = buildSize.accelerationStructureSize;
        if(opDesc.params.type == RayTracingClusterOperationType::Move)
            destinationSize = Max<u64>(destinationSize, opDesc.params.move.maxBytes);
        if(
            destinationSize == 0u
            || !VulkanDetail::IsBufferRangeInBounds(
                outAccelerationStructuresBuffer->getCreationDescription(),
                opDesc.outAccelerationStructuresOffsetInBytes,
                destinationSize
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("implicit destination range is smaller than the queried requirement"));
            return;
        }

        const auto& properties = m_context.nvClusterAccelerationStructureProperties;
        u64 destinationAlignment = 0u;
        switch(opDesc.params.type){
        case RayTracingClusterOperationType::Move:
            switch(opDesc.params.move.type){
            case RayTracingClusterOperationMoveType::BottomLevel:  destinationAlignment = properties.clusterBottomLevelByteAlignment; break;
            case RayTracingClusterOperationMoveType::ClusterLevel: destinationAlignment = properties.clusterByteAlignment; break;
            case RayTracingClusterOperationMoveType::Template:     destinationAlignment = properties.clusterTemplateByteAlignment; break;
            default:                                               return;
            }
            break;
        case RayTracingClusterOperationType::ClasBuild:                destinationAlignment = properties.clusterByteAlignment; break;
        case RayTracingClusterOperationType::ClasBuildTemplates:       destinationAlignment = properties.clusterTemplateByteAlignment; break;
        case RayTracingClusterOperationType::ClasInstantiateTemplates: destinationAlignment = properties.clusterByteAlignment; break;
        case RayTracingClusterOperationType::BlasBuild:                destinationAlignment = properties.clusterBottomLevelByteAlignment; break;
        default:                                                       return;
        }
        if(!getCheckedAddress(
            *outAccelerationStructuresBuffer,
            opDesc.outAccelerationStructuresOffsetInBytes,
            destinationSize,
            destinationAlignment,
            NWB_TEXT("implicit destination"),
            destinationAddress
        )){
            rejectCommandRecording(s_OperationName, NWB_TEXT("the implicit destination device-address range is invalid"));
            return;
        }
    }

    VkDeviceAddress scratchAddress = 0u;
    if(buildSize.buildScratchSize != 0u){
        const u64 scratchAlignment = m_context.nvClusterAccelerationStructureProperties.clusterScratchByteAlignment;
        if(!suballocateBuildScratchAddress(
            buildSize.buildScratchSize,
            scratchAlignment,
            scratchAddress,
            s_OperationName
        )){
            rejectCommandRecording(s_OperationName, NWB_TEXT("scratch-buffer suballocation failed"));
            return;
        }
    }

    auto commandsInfo = VulkanDetail::MakeVkStruct<VkClusterAccelerationStructureCommandsInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV);
    commandsInfo.input = inputInfo;
    commandsInfo.dstImplicitData = destinationAddress;
    commandsInfo.scratchData = scratchAddress;
    commandsInfo.srcInfosArray.deviceAddress = sourceAddress;
    commandsInfo.srcInfosArray.stride = sourceDesc.structStride;
    commandsInfo.srcInfosArray.size = sourceRangeSize;
    commandsInfo.srcInfosCount = sourceCountAddress;
    if(outSizesBuffer){
        commandsInfo.dstSizesArray.deviceAddress = sizeArrayAddress;
        commandsInfo.dstSizesArray.stride = sizeStride;
        commandsInfo.dstSizesArray.size = sizeRangeSize;
    }
    if(inOutAddressesBuffer){
        commandsInfo.dstAddressesArray.deviceAddress = addressArrayAddress;
        commandsInfo.dstAddressesArray.stride = addressStride;
        commandsInfo.dstAddressesArray.size = addressRangeSize;
    }

    endActiveRenderPass();
    if(m_enableAutomaticBarriers){
        setBufferState(indirectArgsBuffer, s_ClusterIndirectInputState);
        if(indirectArgCountBuffer)
            setBufferState(indirectArgCountBuffer, s_ClusterIndirectInputState);
        if(outSizesBuffer)
            setBufferState(outSizesBuffer, ResourceStates::AccelStructWrite);
        if(inOutAddressesBuffer)
            setBufferState(inOutAddressesBuffer, ResourceStates::AccelStructWrite);
        if(outAccelerationStructuresBuffer)
            setBufferState(outAccelerationStructuresBuffer, ResourceStates::AccelStructWrite);
    }
    if(m_commandRecordingFailed)
        return;
    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    m_context.deviceDispatch.vkCmdBuildClusterAccelerationStructureIndirectNV(m_currentCmdBuf->m_cmdBuf, &commandsInfo);
    retainResource(indirectArgsBuffer);
    if(indirectArgCountBuffer)
        retainResource(indirectArgCountBuffer);
    if(outSizesBuffer)
        retainResource(outSizesBuffer);
    if(inOutAddressesBuffer)
        retainResource(inOutAddressesBuffer);
    if(outAccelerationStructuresBuffer)
        retainResource(outAccelerationStructuresBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cooperative Vector


void CommandList::convertCoopVecMatrices(CooperativeVectorConvertMatrixLayoutDesc const* convertDescs, usize numDescs){
    constexpr const tchar* s_OperationName = NWB_TEXT("convert cooperative-vector matrices");
    constexpr GpuQueueCapability::Mask s_ConvertCapabilities = static_cast<GpuQueueCapability::Mask>(
        static_cast<u8>(GpuQueueCapability::Graphics) | static_cast<u8>(GpuQueueCapability::Compute)
    );
    if(!recordAndValidateAnyCommandCapability(s_ConvertCapabilities, s_OperationName))
        return;
    if(
        !m_context.extensions.NV_cooperative_vector
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
    VkResult result = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(
        m_context.physicalDevice,
        &cooperativeVectorPropertyCount,
        nullptr
    );
    if(result != VK_SUCCESS){
        rejectCommandRecording(s_OperationName, NWB_TEXT("failed to enumerate cooperative-vector component types"));
        return;
    }

    Vector<VkCooperativeVectorPropertiesNV, Alloc::ScratchArena> cooperativeVectorProperties(
        cooperativeVectorPropertyCount,
        scratchArena
    );
    for(VkCooperativeVectorPropertiesNV& property : cooperativeVectorProperties){
        property.sType = VK_STRUCTURE_TYPE_COOPERATIVE_VECTOR_PROPERTIES_NV;
        property.pNext = nullptr;
    }
    if(cooperativeVectorPropertyCount != 0u){
        result = m_context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV(
            m_context.physicalDevice,
            &cooperativeVectorPropertyCount,
            cooperativeVectorProperties.data()
        );
        if(result != VK_SUCCESS){
            rejectCommandRecording(s_OperationName, NWB_TEXT("failed to enumerate cooperative-vector component types"));
            return;
        }
    }

    const auto isComponentTypeSupported = [&](const VkComponentTypeKHR componentType){
        if(componentType == VK_COMPONENT_TYPE_FLOAT32_KHR)
            return true;
        for(u32 i = 0u; i < cooperativeVectorPropertyCount; ++i){
            if(cooperativeVectorProperties[i].matrixInterpretation == componentType)
                return true;
        }
        return false;
    };

    const auto isLowerPrecisionFloat = [](const VkComponentTypeKHR componentType){
        switch(componentType){
        case VK_COMPONENT_TYPE_FLOAT_E4M3_NV:
        case VK_COMPONENT_TYPE_FLOAT_E5M2_NV:
        case VK_COMPONENT_TYPE_FLOAT16_KHR:
        case VK_COMPONENT_TYPE_BFLOAT16_KHR:
            return true;
        default:
            return false;
        }
    };

    const auto isFloat8 = [](const VkComponentTypeKHR componentType){
        return
            componentType == VK_COMPONENT_TYPE_FLOAT_E4M3_NV
            || componentType == VK_COMPONENT_TYPE_FLOAT_E5M2_NV
        ;
    };

    const auto isTypeConversionSupported = [&](const VkComponentTypeKHR srcType, const VkComponentTypeKHR dstType){
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
    };

    const auto getStandardLayoutSize = [](
        const usize elementByteSize,
        const u32 numRows,
        const u32 numColumns,
        const CooperativeVectorMatrixLayout::Enum layout,
        const usize requestedStride,
        usize& outStride,
        usize& outByteSize
    ){
        const usize minorElementCount = layout == CooperativeVectorMatrixLayout::RowMajor
            ? static_cast<usize>(numColumns)
            : static_cast<usize>(numRows)
        ;
        const usize majorElementCount = layout == CooperativeVectorMatrixLayout::RowMajor
            ? static_cast<usize>(numRows)
            : static_cast<usize>(numColumns)
        ;

        usize minorByteSize = 0u;
        if(!TryMultiply<usize>(minorElementCount, elementByteSize, minorByteSize))
            return false;

        usize stride = requestedStride;
        if(stride == 0u){
            if(AddOverflows<usize>(minorByteSize, elementByteSize))
                return false;
            stride = minorByteSize + elementByteSize;
        }
        if(stride <= minorByteSize || stride % elementByteSize != 0u)
            return false;

        usize precedingMajorBytes = 0u;
        if(!TryMultiply<usize>(majorElementCount - 1u, stride, precedingMajorBytes))
            return false;
        if(AddOverflows<usize>(precedingMajorBytes, minorByteSize))
            return false;

        outStride = stride;
        outByteSize = precedingMajorBytes + minorByteSize;
        return outByteSize != 0u;
    };

    const auto queryOptimalLayoutSize = [this](
        const VkComponentTypeKHR componentType,
        const VkCooperativeVectorMatrixLayoutNV layout,
        const u32 numRows,
        const u32 numColumns,
        usize& outByteSize
    ){
        const auto publicComponentType = VulkanDetail::ConvertCoopVecDataType(componentType);
        const usize elementByteSize = GetCooperativeVectorDataTypeSize(publicComponentType);

        usize rowByteSize = 0u;
        if(!TryMultiply<usize>(static_cast<usize>(numColumns), elementByteSize, rowByteSize))
            return false;
        if(AddOverflows<usize>(rowByteSize, elementByteSize))
            return false;
        const usize srcStride = rowByteSize + elementByteSize;

        usize precedingRowsByteSize = 0u;
        if(!TryMultiply<usize>(static_cast<usize>(numRows - 1u), srcStride, precedingRowsByteSize))
            return false;
        if(AddOverflows<usize>(precedingRowsByteSize, rowByteSize))
            return false;

        usize queriedByteSize = 0u;
        auto queryInfo = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(
            VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV
        );
        queryInfo.srcSize = precedingRowsByteSize + rowByteSize;
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

        if(m_context.deviceDispatch.vkConvertCooperativeVectorMatrixNV(m_context.device, &queryInfo) != VK_SUCCESS || queriedByteSize == 0u)
            return false;

        outByteSize = queriedByteSize;
        return true;
    };

    const auto resolveMatrixLayout = [&getStandardLayoutSize, &queryOptimalLayoutSize](
        const CooperativeVectorMatrixLayoutDesc& layoutDesc,
        const u32 numRows,
        const u32 numColumns,
        VkComponentTypeKHR& outComponentType,
        VkCooperativeVectorMatrixLayoutNV& outLayout,
        usize& outStride,
        usize& outByteSize
    ){
        if(
            layoutDesc.type > CooperativeVectorDataType::Float64
            || layoutDesc.layout > CooperativeVectorMatrixLayout::TrainingOptimal
            || layoutDesc.type == CooperativeVectorDataType::UInt8Packed
            || layoutDesc.type == CooperativeVectorDataType::SInt8Packed
        )
            return false;

        outComponentType = VulkanDetail::ConvertCoopVecDataType(layoutDesc.type);
        outLayout = VulkanDetail::ConvertCoopVecMatrixLayout(layoutDesc.layout);
        outStride = layoutDesc.stride;

        if(
            layoutDesc.layout == CooperativeVectorMatrixLayout::RowMajor
            || layoutDesc.layout == CooperativeVectorMatrixLayout::ColumnMajor
        ){
            return getStandardLayoutSize(
                GetCooperativeVectorDataTypeSize(layoutDesc.type),
                numRows,
                numColumns,
                layoutDesc.layout,
                layoutDesc.stride,
                outStride,
                outByteSize
            );
        }

        outStride = 0u;
        return queryOptimalLayoutSize(outComponentType, outLayout, numRows, numColumns, outByteSize);
    };

    struct BufferStateEntry{
        Buffer* buffer = nullptr;
        ResourceStates::Mask state = ResourceStates::Unknown;
    };
    Vector<BufferStateEntry, Alloc::ScratchArena> requiredBufferStates{scratchArena};
    requiredBufferStates.reserve(numDescs * 2u);

    struct DeviceAddressRange{
        VkDeviceAddress address = 0u;
        u64 byteSize = 0u;
    };
    Vector<DeviceAddressRange, Alloc::ScratchArena> accessedRanges{scratchArena};
    accessedRanges.reserve(numDescs * 2u);

    Vector<VkConvertCooperativeVectorMatrixInfoNV, Alloc::ScratchArena> vkConvertDescs(numDescs, scratchArena);
    Vector<usize, Alloc::ScratchArena> dstSizes(numDescs, scratchArena);

    const auto addRequiredBufferState = [&requiredBufferStates](
        Buffer* const buffer,
        const ResourceStates::Mask requiredState
    ) -> bool{
        for(BufferStateEntry& state : requiredBufferStates){
            if(state.buffer == buffer){
                state.state |= requiredState;
                return true;
            }
            if(buffer->m_buffer != VK_NULL_HANDLE && state.buffer->m_buffer == buffer->m_buffer)
                return false;
        }
        requiredBufferStates.push_back(BufferStateEntry{ buffer, requiredState });
        return true;
    };

    const auto getCheckedDeviceRange = [](
        Buffer* const buffer,
        const u64 offset,
        const usize declaredByteSize,
        const usize requiredByteSize,
        DeviceAddressRange& outRange
    ){
        if(declaredByteSize == 0u || requiredByteSize == 0u || declaredByteSize < requiredByteSize)
            return false;
        if(
            !VulkanDetail::IsBufferRangeInBounds(buffer->getCreationDescription(), offset, declaredByteSize)
            || !VulkanDetail::IsBufferRangeInBounds(buffer->getCreationDescription(), offset, requiredByteSize)
        )
            return false;

        const VkDeviceAddress baseAddress = buffer->getGpuVirtualAddress();
        if(baseAddress == 0u || baseAddress > Limit<u64>::s_Max - offset)
            return false;

        const VkDeviceAddress address = baseAddress + offset;
        if(address == 0u || address % 64u != 0u)
            return false;
        if(static_cast<u64>(requiredByteSize) > Limit<u64>::s_Max - address)
            return false;

        outRange.address = address;
        outRange.byteSize = static_cast<u64>(requiredByteSize);
        return true;
    };

    for(usize i = 0; i < numDescs; ++i){
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
        if(!isComponentTypeSupported(srcComponentType) || !isComponentTypeSupported(dstComponentType)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a matrix component type is unsupported"));
            return;
        }
        if(!isTypeConversionSupported(srcComponentType, dstComponentType)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("the matrix component type conversion is unsupported"));
            return;
        }
        if(
            isFloat8(dstComponentType)
            && dstLayout != VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_INFERENCING_OPTIMAL_NV
            && dstLayout != VK_COOPERATIVE_VECTOR_MATRIX_LAYOUT_TRAINING_OPTIMAL_NV
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("float8 destinations require an optimal matrix layout"));
            return;
        }

        usize srcStride = 0u;
        usize dstStride = 0u;
        usize srcByteSize = 0u;
        usize dstByteSize = 0u;
        if(
            !resolveMatrixLayout(
                convertDesc.src,
                convertDesc.numRows,
                convertDesc.numColumns,
                srcComponentType,
                srcLayout,
                srcStride,
                srcByteSize
            )
            || !resolveMatrixLayout(
                convertDesc.dst,
                convertDesc.numRows,
                convertDesc.numColumns,
                dstComponentType,
                dstLayout,
                dstStride,
                dstByteSize
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("matrix type, layout, stride, or size query is invalid"));
            return;
        }

        DeviceAddressRange srcRange;
        DeviceAddressRange dstRange;
        if(
            !getCheckedDeviceRange(
                convertDesc.src.buffer,
                convertDesc.src.offset,
                convertDesc.src.size,
                srcByteSize,
                srcRange
            )
            || !getCheckedDeviceRange(
                convertDesc.dst.buffer,
                convertDesc.dst.offset,
                convertDesc.dst.size,
                dstByteSize,
                dstRange
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a matrix range is undersized, out of bounds, overflowing, or not 64-byte aligned"));
            return;
        }

        for(const DeviceAddressRange& existingRange : accessedRanges){
            if(VulkanDetail::BufferRangesOverlap(
                existingRange.address,
                existingRange.byteSize,
                srcRange.address,
                srcRange.byteSize
            ) || VulkanDetail::BufferRangesOverlap(
                existingRange.address,
                existingRange.byteSize,
                dstRange.address,
                dstRange.byteSize
            )){
                rejectCommandRecording(s_OperationName, NWB_TEXT("cooperative-vector conversion memory ranges overlap"));
                return;
            }
        }
        if(VulkanDetail::BufferRangesOverlap(srcRange.address, srcRange.byteSize, dstRange.address, dstRange.byteSize)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("cooperative-vector conversion memory ranges overlap"));
            return;
        }
        accessedRanges.push_back(srcRange);
        accessedRanges.push_back(dstRange);

        if(
            !addRequiredBufferState(convertDesc.src.buffer, ResourceStates::ConvertCoopVecMatrixInput)
            || !addRequiredBufferState(convertDesc.dst.buffer, ResourceStates::ConvertCoopVecMatrixOutput)
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("distinct buffer objects alias the same native buffer"));
            return;
        }

        dstSizes[i] = dstByteSize;
        auto vkDesc = VulkanDetail::MakeVkStruct<VkConvertCooperativeVectorMatrixInfoNV>(
            VK_STRUCTURE_TYPE_CONVERT_COOPERATIVE_VECTOR_MATRIX_INFO_NV
        );
        vkDesc.srcSize = srcByteSize;
        vkDesc.srcData.deviceAddress = srcRange.address;
        vkDesc.pDstSize = &dstSizes[i];
        vkDesc.dstData.deviceAddress = dstRange.address;
        vkDesc.srcComponentType = srcComponentType;
        vkDesc.dstComponentType = dstComponentType;
        vkDesc.numRows = convertDesc.numRows;
        vkDesc.numColumns = convertDesc.numColumns;
        vkDesc.srcLayout = srcLayout;
        vkDesc.srcStride = srcStride;
        vkDesc.dstLayout = dstLayout;
        vkDesc.dstStride = dstStride;
        vkConvertDescs[i] = vkDesc;
    }

    for(const BufferStateEntry& state : requiredBufferStates){
        if(!validateBufferForGpuState(
            state.buffer,
            state.state,
            s_OperationName,
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        ))
            return;
    }

    endActiveRenderPass();
    if(m_enableAutomaticBarriers){
        for(const BufferStateEntry& state : requiredBufferStates){
            setBufferState(state.buffer, state.state);
            if(m_commandRecordingFailed)
                return;
        }
    }

    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    m_context.deviceDispatch.vkCmdConvertCooperativeVectorMatrixNV(m_currentCmdBuf->m_cmdBuf, static_cast<u32>(numDescs), vkConvertDescs.data());
    for(usize i = 0; i < numDescs; ++i){
        retainResource(convertDescs[i].src.buffer);
        retainResource(convertDescs[i].dst.buffer);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

