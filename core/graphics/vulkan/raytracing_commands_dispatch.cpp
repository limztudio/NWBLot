// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(!validateNonTransferCommand(NWB_TEXT("dispatch rays")))
        return;
#if defined(NWB_DEBUG)
    recordTaskCapability(GpuQueueCapability::Compute);
#endif
    if(!m_context.extensions.KHR_ray_tracing_pipeline)
        return;

    RayTracingState& state = m_currentRayTracingState;
    if(!state.shaderTable)
        return;

    auto* sbt = state.shaderTable;
    if(!sbt->m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: shader table has no ray tracing pipeline"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: shader table has no ray tracing pipeline"));
        return;
    }
    if(!sbt->m_raygenBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: ray generation shader is not set"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: ray generation shader is not set"));
        return;
    }
    if(args.width == 0 || args.height == 0 || args.depth == 0)
        return;

    const u64 widthHeight = static_cast<u64>(args.width) * args.height;
    const u64 invocationCount = widthHeight * args.depth;
    if(
        (args.height != 0 && widthHeight / args.height != args.width)
        || (args.depth != 0 && invocationCount / args.depth != widthHeight)
        || invocationCount > m_context.rayTracingPipelineProperties.maxRayDispatchInvocationCount
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: dispatch dimensions ({}, {}, {}) exceed ray dispatch limit {}")
            , args.width
            , args.height
            , args.depth
            , m_context.rayTracingPipelineProperties.maxRayDispatchInvocationCount
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: dispatch dimensions exceed ray dispatch limit"));
        return;
    }

    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    u32 handleSize = 0;
    u32 handleSizeAligned = 0;
    u32 baseAlignment = 0;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(m_context, handleSize, handleSizeAligned, baseAlignment, NWB_TEXT("dispatch rays"))){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: invalid shader group handle layout"));
        return;
    }

    auto computeRegionSize = [&](u32 recordCount, VkDeviceSize& outRegionSize, const tchar* regionName) -> bool{
        if(static_cast<u64>(recordCount) > Limit<u64>::s_Max / static_cast<u64>(handleSizeAligned)){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to dispatch rays: {} shader table region size overflows"), regionName);
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to dispatch rays: shader table region size overflows"));
            return false;
        }

        outRegionSize = static_cast<u64>(recordCount) * static_cast<u64>(handleSizeAligned);
        return true;
    };

    if(sbt->m_raygenBuffer){
        raygenRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_raygenBuffer.get(), sbt->m_raygenOffset);
        raygenRegion.stride = handleSizeAligned;
        raygenRegion.size = handleSizeAligned;
    }

    if(sbt->m_missBuffer){
        missRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_missBuffer.get(), sbt->m_missOffset);
        missRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_missCount, missRegion.size, NWB_TEXT("miss")))
            return;
    }

    if(sbt->m_hitBuffer){
        hitRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_hitBuffer.get(), sbt->m_hitOffset);
        hitRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_hitCount, hitRegion.size, NWB_TEXT("hit")))
            return;
    }

    if(sbt->m_callableBuffer){
        callableRegion.deviceAddress = VulkanDetail::GetBufferDeviceAddress(sbt->m_callableBuffer.get(), sbt->m_callableOffset);
        callableRegion.stride = handleSizeAligned;
        if(!computeRegionSize(sbt->m_callableCount, callableRegion.size, NWB_TEXT("callable")))
            return;
    }

    vkCmdTraceRaysKHR(m_currentCmdBuf->m_cmdBuf, &raygenRegion, &missRegion, &hitRegion, &callableRegion, args.width, args.height, args.depth);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

