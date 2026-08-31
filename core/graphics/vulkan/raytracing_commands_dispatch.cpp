// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_internal.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateRayDispatchDimensions(
    const RayTracingDispatchRaysArguments& arguments,
    const RayDispatchLimits& limits
)noexcept{
    if(arguments.width == 0u || arguments.height == 0u || arguments.depth == 0u)
        return true;
    if(limits.maxInvocationCount == 0u)
        return false;

    const u64 widthHeight = static_cast<u64>(arguments.width) * static_cast<u64>(arguments.height);
    if(widthHeight > Limit<u64>::s_Max / static_cast<u64>(arguments.depth))
        return false;
    if(widthHeight * static_cast<u64>(arguments.depth) > limits.maxInvocationCount)
        return false;

    const Array<u32, 3u> dimensions = { arguments.width, arguments.height, arguments.depth };
    for(u32 axis = 0u; axis < dimensions.size(); ++axis){
        const u64 maxAxisCount = limits.maxAxisCounts[axis];
        const u64 maxAxisSize = limits.maxAxisSizes[axis];
        if(maxAxisCount == 0u || maxAxisSize == 0u || maxAxisCount > Limit<u64>::s_Max / maxAxisSize)
            return false;
        if(static_cast<u64>(dimensions[axis]) > maxAxisCount * maxAxisSize)
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::dispatchRays(const RayTracingDispatchRaysArguments& args){
    if(args.width == 0u || args.height == 0u || args.depth == 0u)
        return;

    if(!recordAndValidateCommandCapability(GpuQueueCapability::Compute, NWB_TEXT("dispatch rays")))
        return;

    if(
        m_context.device == VK_NULL_HANDLE
        || m_context.deviceGeneration == 0u
        || m_device.getDeviceGeneration() != m_context.deviceGeneration
        || !m_context.extensions.KHR_ray_tracing_pipeline
        || !m_context.extensions.buffer_device_address
        || !m_context.rayTracingPipelineFeatureEnabled
        || !m_context.deviceDispatch.vkCmdTraceRaysKHR
    ){
        rejectCommandRecording(
            NWB_TEXT("dispatch rays"),
            NWB_TEXT("ray-tracing dispatch is unavailable on this device generation")
        );
        return;
    }

    ShaderTable* const shaderTable = m_currentRayTracingState.shaderTable;
    if(!shaderTable){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("no shader table is bound"));
        return;
    }
    if(&shaderTable->m_context != &m_context || &shaderTable->m_device != &m_device){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("shader table belongs to another device generation"));
        return;
    }

    ShaderTable::DispatchSnapshot snapshot;
    shaderTable->captureDispatchSnapshot(snapshot);

    RayTracingPipeline* const pipeline = snapshot.pipeline.get();
    if(
        !pipeline
        || &pipeline->m_context != &m_context
        || &pipeline->m_device != &m_device
        || pipeline->getDeviceGeneration() != m_context.deviceGeneration
        || pipeline->m_pipeline == VK_NULL_HANDLE
        || pipeline->m_pipelineLayout == VK_NULL_HANDLE
    ){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("shader table pipeline is foreign or not ready"));
        return;
    }

    VulkanDetail::RayDispatchLimits dispatchLimits;
    dispatchLimits.maxInvocationCount = m_context.rayTracingPipelineProperties.maxRayDispatchInvocationCount;
    for(u32 axis = 0u; axis < dispatchLimits.maxAxisCounts.size(); ++axis){
        dispatchLimits.maxAxisCounts[axis] = m_context.physicalDeviceProperties.limits.maxComputeWorkGroupCount[axis];
        dispatchLimits.maxAxisSizes[axis] = m_context.physicalDeviceProperties.limits.maxComputeWorkGroupSize[axis];
    }
    if(!VulkanDetail::ValidateRayDispatchDimensions(args, dispatchLimits)){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("dispatch dimensions exceed device limits"));
        return;
    }

    u32 handleSize = 0u;
    u32 handleSizeAligned = 0u;
    u32 baseAlignment = 0u;
    if(!VulkanDetail::ComputeRayTracingHandleLayout(
        m_context,
        handleSize,
        handleSizeAligned,
        baseAlignment,
        NWB_TEXT("dispatch rays")
    )){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("shader group handle layout is invalid"));
        return;
    }
    const u32 handleAlignment = m_context.rayTracingPipelineProperties.shaderGroupHandleAlignment;
    const u32 maxShaderGroupStride = m_context.rayTracingPipelineProperties.maxShaderGroupStride;
    if(
        handleSize == 0u
        || handleSizeAligned < handleSize
        || handleAlignment == 0u
        || handleSizeAligned % handleAlignment != 0u
        || maxShaderGroupStride == 0u
        || handleSizeAligned > maxShaderGroupStride
    ){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("shader table stride is invalid"));
        return;
    }

    VkStridedDeviceAddressRegionKHR raygenRegion = {};
    VkStridedDeviceAddressRegionKHR missRegion = {};
    VkStridedDeviceAddressRegionKHR hitRegion = {};
    VkStridedDeviceAddressRegionKHR callableRegion = {};

    const auto buildRegion = [&](
        const ShaderTable::DispatchRegionSnapshot& regionSnapshot,
        const bool required,
        VkStridedDeviceAddressRegionKHR& outRegion
    ) -> bool{
        Buffer* const buffer = regionSnapshot.buffer.get();
        if(!buffer){
            return !required
                && regionSnapshot.offset == 0u
                && regionSnapshot.recordCount == 0u
                && regionSnapshot.selectedGroupCount == 0u
            ;
        }
        if(
            regionSnapshot.recordCount == 0u
            || regionSnapshot.selectedGroupCount != static_cast<usize>(regionSnapshot.recordCount)
            || (required && regionSnapshot.recordCount != 1u)
            || &buffer->m_context != &m_context
            || buffer->getDeviceGeneration() != m_context.deviceGeneration
            || !isBufferReadyForCommandQueue(
                buffer,
                VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            )
            || buffer->m_buffer == VK_NULL_HANDLE
            || buffer->m_deviceAddress == 0u
        )
            return false;

        const BufferDesc& creationDesc = buffer->getCreationDescription();
        constexpr u8 s_RequiredQueueSharing = static_cast<u8>(ResourceQueueSharing::GraphicsAndAsyncCompute);
        if(
            !creationDesc.isShaderBindingTable
            || creationDesc.cpuAccess != CpuAccessMode::Write
            || (static_cast<u8>(creationDesc.queueSharing) & s_RequiredQueueSharing) != s_RequiredQueueSharing
            || (buffer->m_bufferInfo.usage & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR) == 0u
            || (buffer->m_bufferInfo.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0u
            || static_cast<u64>(regionSnapshot.recordCount) > Limit<u64>::s_Max / handleSizeAligned
        )
            return false;

        const u64 regionSize = static_cast<u64>(regionSnapshot.recordCount) * handleSizeAligned;
        if(
            !VulkanDetail::IsBufferRangeInBounds(creationDesc, regionSnapshot.offset, regionSize)
            || regionSnapshot.offset > Limit<u64>::s_Max - buffer->m_deviceAddress
        )
            return false;

        const u64 deviceAddress = buffer->m_deviceAddress + regionSnapshot.offset;
        if(
            deviceAddress == 0u
            || deviceAddress % baseAlignment != 0u
            || regionSize > Limit<u64>::s_Max - deviceAddress
        )
            return false;

        outRegion.deviceAddress = deviceAddress;
        outRegion.stride = handleSizeAligned;
        outRegion.size = regionSize;
        return true;
    };

    if(
        !buildRegion(snapshot.rayGeneration, true, raygenRegion)
        || !buildRegion(snapshot.miss, false, missRegion)
        || !buildRegion(snapshot.hit, false, hitRegion)
        || !buildRegion(snapshot.callable, false, callableRegion)
    ){
        rejectCommandRecording(NWB_TEXT("dispatch rays"), NWB_TEXT("shader table regions are incoherent or not ready"));
        return;
    }

    retainResource(snapshot.rayGeneration.buffer.get());
    retainResource(snapshot.miss.buffer.get());
    retainResource(snapshot.hit.buffer.get());
    retainResource(snapshot.callable.buffer.get());

    m_context.deviceDispatch.vkCmdTraceRaysKHR(
        m_currentCmdBuf->m_cmdBuf,
        &raygenRegion,
        &missRegion,
        &hitRegion,
        &callableRegion,
        args.width,
        args.height,
        args.depth
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

