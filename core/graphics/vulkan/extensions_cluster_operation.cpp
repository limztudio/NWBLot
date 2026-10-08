// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::executeMultiIndirectClusterOperation(const RayTracingClusterOperationDesc& opDesc){
    constexpr TStringView s_OperationName = NWB_TEXT("execute cluster acceleration operation");
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, s_OperationName))
        return;
    if(
        !m_context.extensions.nvClusterAccelerationStructure
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
    constexpr u32 s_MaxPositionTruncateBitCount = 32u;
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
            !m_context.extensions.extOpacityMicromap
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

    auto input = VulkanDetail::BuildClusterOperationInputInfo(opDesc.params, s_OperationName);
    if(!input){
        rejectCommandRecording(s_OperationName, NWB_TEXT("operation parameters are invalid"));
        return;
    }
    const auto inputInfo = input->nativeInfo();

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
            || opDesc.params.clas.minPositionTruncateBitCount > s_MaxPositionTruncateBitCount
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("triangle-cluster parameters exceed device limits"));
            return;
        }

        VkFormatProperties formatProperties{};
        m_context.instanceDispatch.vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, input->cluster.vertexFormat, &formatProperties);
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
        const TStringView rangeName
    ) -> Expected<u64>{
        const u64 spanCount = static_cast<u64>(count - 1u);
        if(stride != 0u && spanCount > (Limit<u64>::s_Max - elementSize) / stride){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} range overflows"), s_OperationName, rangeName);
            return MakeUnexpected(Failure{});
        }
        return spanCount * stride + elementSize;
    };
    const auto getCheckedAddress = [s_OperationName](
        Buffer& buffer,
        const u64 offset,
        const u64 size,
        const u64 alignment,
        const TStringView rangeName
    ) -> Expected<VkDeviceAddress>{
        const VkDeviceAddress baseAddress = buffer.getGpuVirtualAddress();
        if(baseAddress == 0u || baseAddress > Limit<u64>::s_Max - offset){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device address is invalid or overflows"), s_OperationName, rangeName);
            return MakeUnexpected(Failure{});
        }
        const VkDeviceAddress address = baseAddress + offset;
        if(address == 0u || alignment == 0u || (address % alignment) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device address is not aligned to {} bytes")
                , s_OperationName
                , rangeName
                , alignment
            );
            return MakeUnexpected(Failure{});
        }
        if(size == 0u || address > Limit<u64>::s_Max - (size - 1u)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: {} device-address range is empty or overflows"), s_OperationName, rangeName);
            return MakeUnexpected(Failure{});
        }
        return address;
    };

    const BufferDesc& sourceDesc = indirectArgsBuffer->getCreationDescription();
    if(sourceDesc.structStride != 0u && sourceDesc.structStride <= sourceInfoSize){
        rejectCommandRecording(s_OperationName, NWB_TEXT("indirect source-info stride must be zero or greater than its structure size"));
        return;
    }
    const u64 sourceStride = sourceDesc.structStride != 0u ? sourceDesc.structStride : sourceInfoSize;
    const auto sourceRangeSize = computeStridedRangeSize(sourceStride, sourceInfoSize, opDesc.params.maxArgCount, NWB_TEXT("source-info"));
    if(
        !sourceRangeSize
        || !VulkanDetail::IsBufferRangeInBounds(sourceDesc, opDesc.inIndirectArgsOffsetInBytes, *sourceRangeSize)
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
        const auto sizeRange = computeStridedRangeSize(sizeStride, sizeof(u32), opDesc.params.maxArgCount, NWB_TEXT("size-array"));
        if(
            !sizeRange
            || !VulkanDetail::IsBufferRangeInBounds(
                outSizesBuffer->getCreationDescription(),
                opDesc.outSizesOffsetInBytes,
                *sizeRange
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("size-array range is outside the buffer"));
            return;
        }
        sizeRangeSize = *sizeRange;
    }
    if(inOutAddressesBuffer){
        addressStride = inOutAddressesBuffer->getCreationDescription().structStride;
        if(addressStride < sizeof(VkDeviceAddress)){
            rejectCommandRecording(s_OperationName, NWB_TEXT("address-array stride must be at least eight bytes"));
            return;
        }
        const auto addressRange = computeStridedRangeSize(
                addressStride,
                sizeof(VkDeviceAddress),
                opDesc.params.maxArgCount,
                NWB_TEXT("address-array")
            );
        if(
            !addressRange
            || !VulkanDetail::IsBufferRangeInBounds(
                inOutAddressesBuffer->getCreationDescription(),
                opDesc.inOutAddressesOffsetInBytes,
                *addressRange
            )
        ){
            rejectCommandRecording(s_OperationName, NWB_TEXT("address-array range is outside the buffer"));
            return;
        }
        addressRangeSize = *addressRange;
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

    const auto sourceAddress = getCheckedAddress(
        *indirectArgsBuffer,
        opDesc.inIndirectArgsOffsetInBytes,
        *sourceRangeSize,
        alignof(VkDeviceAddress),
        NWB_TEXT("source-info")
    );
    if(!sourceAddress){
        rejectCommandRecording(s_OperationName, NWB_TEXT("a required device-address range is invalid"));
        return;
    }
    VkDeviceAddress sourceCountAddress = 0u;
    if(indirectArgCountBuffer){
        const auto address = getCheckedAddress(
            *indirectArgCountBuffer,
            opDesc.inIndirectArgCountOffsetInBytes,
            sizeof(u32),
            alignof(u32),
            NWB_TEXT("source-info count")
        );
        if(!address){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a required device-address range is invalid"));
            return;
        }
        sourceCountAddress = *address;
    }
    VkDeviceAddress sizeArrayAddress = 0u;
    if(outSizesBuffer){
        const auto address = getCheckedAddress(
            *outSizesBuffer,
            opDesc.outSizesOffsetInBytes,
            sizeRangeSize,
            alignof(u32),
            NWB_TEXT("size-array")
        );
        if(!address){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a required device-address range is invalid"));
            return;
        }
        sizeArrayAddress = *address;
    }
    VkDeviceAddress addressArrayAddress = 0u;
    if(inOutAddressesBuffer){
        const auto address = getCheckedAddress(
            *inOutAddressesBuffer,
            opDesc.inOutAddressesOffsetInBytes,
            addressRangeSize,
            alignof(VkDeviceAddress),
            NWB_TEXT("address-array")
        );
        if(!address){
            rejectCommandRecording(s_OperationName, NWB_TEXT("a required device-address range is invalid"));
            return;
        }
        addressArrayAddress = *address;
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
        const auto address = getCheckedAddress(
            *outAccelerationStructuresBuffer,
            opDesc.outAccelerationStructuresOffsetInBytes,
            destinationSize,
            destinationAlignment,
            NWB_TEXT("implicit destination")
        );
        if(!address){
            rejectCommandRecording(s_OperationName, NWB_TEXT("the implicit destination device-address range is invalid"));
            return;
        }
        destinationAddress = *address;
    }

    VkDeviceAddress scratchAddress = 0u;
    if(buildSize.buildScratchSize != 0u){
        const u64 scratchAlignment = m_context.nvClusterAccelerationStructureProperties.clusterScratchByteAlignment;
        const auto scratchAddressResult = suballocateBuildScratchAddress(buildSize.buildScratchSize, scratchAlignment, s_OperationName);
        if(!scratchAddressResult){
            rejectCommandRecording(s_OperationName, NWB_TEXT("scratch-buffer suballocation failed"));
            return;
        }
        scratchAddress = *scratchAddressResult;
    }

    auto commandsInfo = VulkanDetail::MakeVkStruct<VkClusterAccelerationStructureCommandsInfoNV>(VK_STRUCTURE_TYPE_CLUSTER_ACCELERATION_STRUCTURE_COMMANDS_INFO_NV);
    commandsInfo.input = inputInfo;
    commandsInfo.dstImplicitData = destinationAddress;
    commandsInfo.scratchData = scratchAddress;
    commandsInfo.srcInfosArray.deviceAddress = *sourceAddress;
    commandsInfo.srcInfosArray.stride = sourceDesc.structStride;
    commandsInfo.srcInfosArray.size = *sourceRangeSize;
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


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

