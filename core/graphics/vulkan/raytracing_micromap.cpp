// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"
#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MicromapUsageVector = Vector<VkMicromapUsageEXT, Alloc::ScratchArena>;
VkOpacityMicromapFormatEXT ConvertOpacityMicromapFormat(const OpacityMicromapFormat::Enum format){
    switch(format){
    case OpacityMicromapFormat::OC1_2_State:
        return VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT;
    case OpacityMicromapFormat::OC1_4_State:
        return VK_OPACITY_MICROMAP_FORMAT_4_STATE_EXT;
    default:
        return VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_KHR;
    }
}

bool ConvertOpacityMicromapBuildFlags(
    const RayTracingOpacityMicromapBuildFlags::Mask flags,
    VkBuildMicromapFlagsEXT& outFlags,
    const tchar* operation
){
    constexpr u8 s_KnownFlags = RayTracingOpacityMicromapBuildFlags::FastTrace
        | RayTracingOpacityMicromapBuildFlags::FastBuild
        | RayTracingOpacityMicromapBuildFlags::AllowCompaction
    ;
    if((static_cast<u8>(flags) & static_cast<u8>(~s_KnownFlags)) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: opacity micromap build flags contain unknown bits"), operation);
        return false;
    }
    if(
        (flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        && (flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: fast-trace and fast-build flags are mutually exclusive"), operation);
        return false;
    }

    outFlags = 0u;
    if(flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        outFlags |= VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    if(flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        outFlags |= VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;
    if(flags & RayTracingOpacityMicromapBuildFlags::AllowCompaction)
        outFlags |= VK_BUILD_MICROMAP_ALLOW_COMPACTION_BIT_EXT;
    return true;
}

bool BuildOpacityMicromapUsageCounts(
    const GraphicsVector<RayTracingOpacityMicromapUsageCount>& counts,
    const u32 maxOpacity2StateSubdivisionLevel,
    const u32 maxOpacity4StateSubdivisionLevel,
    MicromapUsageVector& outUsageCounts,
    const tchar* operation
){
    outUsageCounts.clear();
    outUsageCounts.reserve(counts.size());

    for(usize i = 0; i < counts.size(); ++i){
        const RayTracingOpacityMicromapUsageCount& count = counts[i];
        const VkOpacityMicromapFormatEXT format = ConvertOpacityMicromapFormat(count.format);
        if(format == VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_KHR){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: opacity micromap usage count {} has invalid format {}")
                , operation
                , i
                , static_cast<u32>(count.format)
            );
            outUsageCounts.clear();
            return false;
        }
        const u32 maxSubdivisionLevel = format == VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT
            ? maxOpacity2StateSubdivisionLevel
            : maxOpacity4StateSubdivisionLevel
        ;
        if(count.subdivisionLevel > maxSubdivisionLevel){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to {}: opacity micromap usage count {} subdivision level {} exceeds device limit {}")
                , operation
                , i
                , count.subdivisionLevel
                , maxSubdivisionLevel
            );
            outUsageCounts.clear();
            return false;
        }

        VkMicromapUsageEXT usageCount = {};
        usageCount.count = count.count;
        usageCount.subdivisionLevel = count.subdivisionLevel;
        usageCount.format = static_cast<u32>(format);
        outUsageCounts.push_back(usageCount);
    }

    return true;
}

bool ResolveOpacityMicromapBuildInputAddress(
    Buffer& buffer,
    const u64 offset,
    const u64 byteSize,
    const tchar* resourceName,
    VkDeviceAddress& outAddress
){
    constexpr u64 s_DeviceAddressAlignment = 256u;
    const u64 validatedByteSize = byteSize != 0u ? byteSize : 1u;
    if(!IsBufferRangeInBounds(buffer.getCreationDescription(), offset, validatedByteSize)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: {} range is outside the buffer"), resourceName);
        return false;
    }

    outAddress = GetBufferDeviceAddress(&buffer, offset);
    if(outAddress == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: {} device address is null or overflows"), resourceName);
        return false;
    }
    if((outAddress % s_DeviceAddressAlignment) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: {} device address is not 256-byte aligned"), resourceName);
        return false;
    }

    return true;
}

VkMemoryBarrier2 BuildOpacityMicromapWriteAfterWriteBarrier()noexcept{
    auto barrier = MakeVkStruct<VkMemoryBarrier2>(VK_STRUCTURE_TYPE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    barrier.srcAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    barrier.dstAccessMask = VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT;
    return barrier;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpacityMicromap::OpacityMicromap(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_desc(context.objectArena)
    , m_context(context)
{}
OpacityMicromap::~OpacityMicromap(){
    if(m_micromap != VK_NULL_HANDLE){
        vkDestroyMicromapEXT(m_context.device, m_micromap, m_context.allocationCallbacks);
        m_micromap = VK_NULL_HANDLE;
    }
    m_dataBuffer.reset();
}

RayTracingOpacityMicromapHandle Device::createOpacityMicromap(const RayTracingOpacityMicromapDesc& desc){
    VkResult res = VK_SUCCESS;

    if(!m_context.extensions.EXT_opacity_micromap || !m_context.opacityMicromapFeatureEnabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Enabled opacity micromap feature support is required to create opacity micromaps."));
        return nullptr;
    }
    if(desc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create opacity micromap: usage-count count exceeds Vulkan limit"));
        return nullptr;
    }

    VkBuildMicromapFlagsEXT buildFlags = 0u;
    if(!VulkanDetail::ConvertOpacityMicromapBuildFlags(desc.flags, buildFlags, NWB_TEXT("create opacity micromap")))
        return nullptr;

    auto opacityMicromapProperties = VulkanDetail::MakeVkStruct<VkPhysicalDeviceOpacityMicromapPropertiesEXT>(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_OPACITY_MICROMAP_PROPERTIES_EXT
    );
    auto physicalDeviceProperties = VulkanDetail::MakeVkStruct<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    physicalDeviceProperties.pNext = &opacityMicromapProperties;
    vkGetPhysicalDeviceProperties2(m_context.physicalDevice, &physicalDeviceProperties);

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena);
    VulkanDetail::MicromapUsageVector usageCounts{ scratchArena };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(
        desc.counts,
        opacityMicromapProperties.maxOpacity2StateSubdivisionLevel,
        opacityMicromapProperties.maxOpacity4StateSubdivisionLevel,
        usageCounts,
        NWB_TEXT("create opacity micromap")
    ))
        return nullptr;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkMicromapBuildInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT);
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.usageCountsCount = static_cast<u32>(usageCounts.size());
    buildInfo.pUsageCounts = usageCounts.empty() ? nullptr : usageCounts.data();

    auto buildSize = VulkanDetail::MakeVkStruct<VkMicromapBuildSizesInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT);
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    auto* om = NewArenaObject<OpacityMicromap>(m_context.objectArena, m_context);
    om->m_desc = desc;
    om->m_maxOpacity2StateSubdivisionLevel = opacityMicromapProperties.maxOpacity2StateSubdivisionLevel;
    om->m_maxOpacity4StateSubdivisionLevel = opacityMicromapProperties.maxOpacity4StateSubdivisionLevel;

    BufferDesc bufferDesc;
    bufferDesc.canHaveUAVs = true;
    bufferDesc.byteSize = buildSize.micromapSize;
    bufferDesc.initialState = ResourceStates::OpacityMicromapWrite;
    bufferDesc.keepInitialState = true;
    bufferDesc.isAccelStructStorage = true;
    bufferDesc.debugName = desc.debugName;

    om->m_dataBuffer = createBuffer(bufferDesc);
    if(!om->m_dataBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate opacity micromap storage buffer"));
        DestroyArenaObject(m_context.objectArena, om);
        return nullptr;
    }

    auto* buffer = om->m_dataBuffer.get();
    if(!isBufferReadyForGpuUse(
        buffer,
        VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    )){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create opacity micromap: storage buffer is not ready for device-address access")
        );
        DestroyArenaObject(m_context.objectArena, om);
        return nullptr;
    }

    auto createInfo = VulkanDetail::MakeVkStruct<VkMicromapCreateInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT);
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    createInfo.buffer = buffer->m_buffer;
    createInfo.size = buildSize.micromapSize;

    res = vkCreateMicromapEXT(m_context.device, &createInfo, m_context.allocationCallbacks, &om->m_micromap);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create opacity micromap: {}"), ResultToString(res));
        DestroyArenaObject(m_context.objectArena, om);
        return nullptr;
    }

    return RayTracingOpacityMicromapHandle(om, RayTracingOpacityMicromapHandle::deleter_type(&m_context.objectArena), AdoptRef);
}

void CommandList::buildOpacityMicromap(RayTracingOpacityMicromap* opacityMicromapResource, const RayTracingOpacityMicromapDesc& ommDesc){
    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("build opacity micromap")))
        return;
    if(!m_context.extensions.EXT_opacity_micromap || !m_context.opacityMicromapFeatureEnabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: enabled opacity micromap feature support is unavailable"));
        return;
    }

    if(!opacityMicromapResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is null"));
        return;
    }

    auto* omm = opacityMicromapResource;
    if(&omm->m_context != &m_context || omm->m_micromap == VK_NULL_HANDLE || !omm->m_dataBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is foreign or invalid"));
        return;
    }
    if(ommDesc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: usage-count count exceeds Vulkan limit"));
        return;
    }

    u64 triangleDescBytes = 0;
    for(const RayTracingOpacityMicromapUsageCount& count : ommDesc.counts){
        u64 countBytes = 0u;
        if(
            !TryMultiply<u64>(static_cast<u64>(count.count), sizeof(VkMicromapTriangleEXT), countBytes)
            || triangleDescBytes > Limit<u64>::s_Max - countBytes
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor size overflows"));
            return;
        }
        triangleDescBytes += countBytes;
    }

    VkBuildMicromapFlagsEXT buildFlags = 0u;
    if(!VulkanDetail::ConvertOpacityMicromapBuildFlags(ommDesc.flags, buildFlags, NWB_TEXT("build opacity micromap")))
        return;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena);
    VulkanDetail::MicromapUsageVector usageCounts{ scratchArena };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(
        ommDesc.counts,
        omm->m_maxOpacity2StateSubdivisionLevel,
        omm->m_maxOpacity4StateSubdivisionLevel,
        usageCounts,
        NWB_TEXT("build opacity micromap")
    ))
        return;

    auto* inputBuffer = ommDesc.inputBuffer;
    if(!inputBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: input buffer is invalid"));
        return;
    }
    auto* dataBuffer = omm->m_dataBuffer.get();
    constexpr VkBufferUsageFlags s_BuildInputUsage =
        VK_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    constexpr VkBufferUsageFlags s_StorageUsage =
        VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;
    constexpr const tchar* s_OperationName = NWB_TEXT("build opacity micromap");
    if(
        !validateBufferForGpuState(
            inputBuffer,
            ResourceStates::OpacityMicromapBuildInput,
            s_OperationName,
            s_BuildInputUsage
        )
        || !validateBufferForGpuState(
            dataBuffer,
            ResourceStates::OpacityMicromapWrite,
            s_OperationName,
            s_StorageUsage
        )
    )
        return;

    auto* perOmmDescs = ommDesc.perOmmDescs;
    if(!perOmmDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor buffer is invalid"));
        return;
    }
    if(!validateBufferForGpuState(
        perOmmDescs,
        ResourceStates::OpacityMicromapBuildInput,
        s_OperationName,
        s_BuildInputUsage
    ))
        return;

    VkDeviceAddress inputAddress = 0u;
    VkDeviceAddress triangleDescAddress = 0u;
    if(
        !VulkanDetail::ResolveOpacityMicromapBuildInputAddress(
            *inputBuffer,
            ommDesc.inputBufferOffset,
            1u,
            NWB_TEXT("input data"),
            inputAddress
        )
        || !VulkanDetail::ResolveOpacityMicromapBuildInputAddress(
            *perOmmDescs,
            ommDesc.perOmmDescsOffset,
            triangleDescBytes,
            NWB_TEXT("per-OMM descriptor"),
            triangleDescAddress
        )
    )
        return;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkMicromapBuildInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT);
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.dstMicromap = omm->m_micromap;
    buildInfo.usageCountsCount = static_cast<u32>(usageCounts.size());
    buildInfo.pUsageCounts = usageCounts.empty() ? nullptr : usageCounts.data();
    buildInfo.data.deviceAddress = inputAddress;
    buildInfo.triangleArray.deviceAddress = triangleDescAddress;
    buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

    auto buildSize = VulkanDetail::MakeVkStruct<VkMicromapBuildSizesInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT);
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    if(!dataBuffer || dataBuffer->getCreationDescription().byteSize < buildSize.micromapSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap storage is too small"));
        return;
    }

    if(buildSize.buildScratchSize != 0u){
        const u64 scratchAlignment = Max<u64>(
            static_cast<u64>(m_context.accelStructProperties.minAccelerationStructureScratchOffsetAlignment),
            1u
        );
        if(!suballocateBuildScratchAddress(
            buildSize.buildScratchSize,
            scratchAlignment,
            buildInfo.scratchData.deviceAddress,
            NWB_TEXT("build opacity micromap")
        ))
            return;
    }

    if(m_enableAutomaticBarriers){
        setBufferState(inputBuffer, ResourceStates::OpacityMicromapBuildInput);
        setBufferState(perOmmDescs, ResourceStates::OpacityMicromapBuildInput);
        setBufferState(dataBuffer, ResourceStates::OpacityMicromapWrite);
    }
    if(m_commandRecordingFailed)
        return;

    retainResource(inputBuffer);
    retainResource(perOmmDescs);
    retainResource(dataBuffer);

    commitBarriers();
    if(m_commandRecordingFailed)
        return;

    const VkMemoryBarrier2 reuseBarrier = VulkanDetail::BuildOpacityMicromapWriteAfterWriteBarrier();
    auto reuseDepInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    reuseDepInfo.memoryBarrierCount = 1u;
    reuseDepInfo.pMemoryBarriers = &reuseBarrier;
    executePipelineBarrier(reuseDepInfo);
    if(m_commandRecordingFailed)
        return;

    vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1u, &buildInfo);
    m_currentCmdBuf->appendPendingOpacityMicromapBuildCommit(*omm);

    retainResource(omm);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

