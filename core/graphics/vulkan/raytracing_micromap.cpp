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
        return VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT;
    }
}

bool BuildOpacityMicromapUsageCounts(const GraphicsVector<RayTracingOpacityMicromapUsageCount>& counts, MicromapUsageVector& outUsageCounts, const tchar* operation){
    outUsageCounts.clear();
    outUsageCounts.reserve(counts.size());

    for(usize i = 0; i < counts.size(); ++i){
        const RayTracingOpacityMicromapUsageCount& count = counts[i];
        const VkOpacityMicromapFormatEXT format = ConvertOpacityMicromapFormat(count.format);
        if(format == VK_OPACITY_MICROMAP_FORMAT_MAX_ENUM_EXT){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: opacity micromap usage count {} has invalid format {}")
                , operation
                , i
                , static_cast<u32>(count.format)
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

    if(!m_context.extensions.EXT_opacity_micromap){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Opacity micromap extension is required to create opacity micromaps."));
        return nullptr;
    }
    if(desc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create opacity micromap: usage-count count exceeds Vulkan limit"));
        return nullptr;
    }

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(desc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena);
    VulkanDetail::MicromapUsageVector usageCounts{ scratchArena };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(desc.counts, usageCounts, NWB_TEXT("create opacity micromap")))
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

    auto createInfo = VulkanDetail::MakeVkStruct<VkMicromapCreateInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_CREATE_INFO_EXT);
    createInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    createInfo.buffer = buffer->m_buffer;
    createInfo.size = buildSize.micromapSize;
    createInfo.deviceAddress = buffer->m_deviceAddress;

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
    if(!m_context.extensions.EXT_opacity_micromap)
        return;

    if(!opacityMicromapResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is null"));
        return;
    }

    auto* omm = opacityMicromapResource;
    if(!omm){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap is invalid"));
        return;
    }
    if(ommDesc.counts.size() > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: usage-count count exceeds Vulkan limit"));
        return;
    }

    u64 triangleDescBytes = 0;
    for(const RayTracingOpacityMicromapUsageCount& count : ommDesc.counts){
        const u64 countBytes = static_cast<u64>(count.count) * sizeof(VkMicromapTriangleEXT);
        if(triangleDescBytes > UINT64_MAX - countBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor size overflows"));
            return;
        }
        triangleDescBytes += countBytes;
    }

    auto* inputBuffer = ommDesc.inputBuffer;
    if(!inputBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: input buffer is invalid"));
        return;
    }
    if(!VulkanDetail::IsBufferRangeInBounds(inputBuffer->m_desc, ommDesc.inputBufferOffset, 1)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: input buffer offset is outside the buffer"));
        return;
    }

    auto* perOmmDescs = ommDesc.perOmmDescs;
    if(!perOmmDescs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor buffer is invalid"));
        return;
    }
    if(triangleDescBytes > 0 && !VulkanDetail::IsBufferRangeInBounds(perOmmDescs->m_desc, ommDesc.perOmmDescsOffset, triangleDescBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: per-OMM descriptor range is outside the buffer"));
        return;
    }

    if(m_enableAutomaticBarriers){
        if(ommDesc.inputBuffer)
            setBufferState(ommDesc.inputBuffer, ResourceStates::OpacityMicromapBuildInput);
        if(ommDesc.perOmmDescs)
            setBufferState(ommDesc.perOmmDescs, ResourceStates::OpacityMicromapBuildInput);
        if(omm->m_dataBuffer)
            setBufferState(omm->m_dataBuffer.get(), ResourceStates::OpacityMicromapWrite);
    }

    if(ommDesc.trackLiveness){
        if(ommDesc.inputBuffer)
            retainResource(ommDesc.inputBuffer);
        if(ommDesc.perOmmDescs)
            retainResource(ommDesc.perOmmDescs);
        if(omm->m_dataBuffer)
            retainResource(omm->m_dataBuffer.get());
    }

    commitBarriers();

    VkBuildMicromapFlagBitsEXT buildFlags = static_cast<VkBuildMicromapFlagBitsEXT>(0);
    if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastTrace)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_TRACE_BIT_EXT;
    else if(ommDesc.flags & RayTracingOpacityMicromapBuildFlags::FastBuild)
        buildFlags = VK_BUILD_MICROMAP_PREFER_FAST_BUILD_BIT_EXT;

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_RayTracingArena);
    VulkanDetail::MicromapUsageVector usageCounts{ scratchArena };
    if(!VulkanDetail::BuildOpacityMicromapUsageCounts(ommDesc.counts, usageCounts, NWB_TEXT("build opacity micromap")))
        return;

    auto buildInfo = VulkanDetail::MakeVkStruct<VkMicromapBuildInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_INFO_EXT);
    buildInfo.type = VK_MICROMAP_TYPE_OPACITY_MICROMAP_EXT;
    buildInfo.flags = buildFlags;
    buildInfo.mode = VK_BUILD_MICROMAP_MODE_BUILD_EXT;
    buildInfo.dstMicromap = omm->m_micromap;
    buildInfo.usageCountsCount = static_cast<u32>(usageCounts.size());
    buildInfo.pUsageCounts = usageCounts.empty() ? nullptr : usageCounts.data();
    buildInfo.data.deviceAddress = VulkanDetail::GetBufferDeviceAddress(ommDesc.inputBuffer, ommDesc.inputBufferOffset);
    buildInfo.triangleArray.deviceAddress = VulkanDetail::GetBufferDeviceAddress(ommDesc.perOmmDescs, ommDesc.perOmmDescsOffset);
    buildInfo.triangleArrayStride = sizeof(VkMicromapTriangleEXT);

    auto buildSize = VulkanDetail::MakeVkStruct<VkMicromapBuildSizesInfoEXT>(VK_STRUCTURE_TYPE_MICROMAP_BUILD_SIZES_INFO_EXT);
    vkGetMicromapBuildSizesEXT(m_context.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &buildSize);

    auto* dataBuffer = omm->m_dataBuffer.get();
    if(!dataBuffer || dataBuffer->m_desc.byteSize < buildSize.micromapSize){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to build opacity micromap: micromap storage is too small"));
        return;
    }

    if(buildSize.buildScratchSize != 0){
        BufferDesc scratchDesc;
        scratchDesc.byteSize = buildSize.buildScratchSize;
        scratchDesc.structStride = 1;
        scratchDesc.debugName = "OMM_BuildScratch";
        scratchDesc.canHaveUAVs = true;

        BufferHandle scratchBuffer = m_device.createBuffer(scratchDesc);
        if(!scratchBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate opacity micromap scratch buffer"));
            return;
        }

        buildInfo.scratchData.deviceAddress = VulkanDetail::GetBufferDeviceAddress(scratchBuffer.get());

        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);

        m_currentCmdBuf->m_referencedStagingBuffers.push_back(Move(scratchBuffer));
    }
    else
        vkCmdBuildMicromapsEXT(m_currentCmdBuf->m_cmdBuf, 1, &buildInfo);

    retainResource(omm);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

