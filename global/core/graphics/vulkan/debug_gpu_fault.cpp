
#if defined(NWB_GPU_FAULT_INJECTION)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// DEBUG / TEST ONLY. Deliberately faults the GPU to exercise the device-lost -> GPU crash capture path.
#include "backend.h"

#include <global/core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_debug_gpu_fault{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compiled SPIR-V (glslc -O, vulkan1.3) for a compute shader that writes 0xDEADBEEF through a
// buffer_reference pointer taken from an 8-byte push constant. Requires bufferDeviceAddress + shaderInt64,
// both already enabled by the engine device.
inline constexpr u32 s_FaultComputeSpirv[] = {
    0x07230203, 0x00010600, 0x000d000b, 0x00000016, 0x00000000, 0x00020011, 0x00000001, 0x00020011,
    0x0000000b, 0x00020011, 0x000014e3, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x000014e4, 0x00000001, 0x0006000f, 0x00000005, 0x00000004, 0x6e69616d,
    0x00000000, 0x00000009, 0x00060010, 0x00000004, 0x00000011, 0x00000001, 0x00000001, 0x00000001,
    0x00030047, 0x00000007, 0x00000002, 0x00050048, 0x00000007, 0x00000000, 0x00000023, 0x00000000,
    0x00030047, 0x00000011, 0x00000002, 0x00050048, 0x00000011, 0x00000000, 0x00000023, 0x00000000,
    0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00040015, 0x00000006, 0x00000040,
    0x00000000, 0x0003001e, 0x00000007, 0x00000006, 0x00040020, 0x00000008, 0x00000009, 0x00000007,
    0x0004003b, 0x00000008, 0x00000009, 0x00000009, 0x00040015, 0x0000000a, 0x00000020, 0x00000001,
    0x0004002b, 0x0000000a, 0x0000000b, 0x00000000, 0x00040020, 0x0000000c, 0x00000009, 0x00000006,
    0x00030027, 0x0000000f, 0x000014e5, 0x00040015, 0x00000010, 0x00000020, 0x00000000, 0x0003001e,
    0x00000011, 0x00000010, 0x00040020, 0x0000000f, 0x000014e5, 0x00000011, 0x0004002b, 0x00000010,
    0x00000013, 0xdeadbeef, 0x00040020, 0x00000014, 0x000014e5, 0x00000010, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x00050041, 0x0000000c, 0x0000000d,
    0x00000009, 0x0000000b, 0x0004003d, 0x00000006, 0x0000000e, 0x0000000d, 0x00040078, 0x0000000f,
    0x00000012, 0x0000000e, 0x00050041, 0x00000014, 0x00000015, 0x00000012, 0x0000000b, 0x0005003e,
    0x00000015, 0x00000013, 0x00000002, 0x00000004, 0x000100fd, 0x00010038,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Device::debugTriggerGpuFault(const u64 faultDeviceAddress){
    using namespace __hidden_debug_gpu_fault;

    Optional<Queue>& graphicsQueue = m_queues[static_cast<u32>(CommandQueue::Graphics)];
    if(!graphicsQueue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: [debug] GPU fault injection skipped; no graphics queue."));
        return;
    }

    NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT("Vulkan: [debug] injecting a GPU page fault at device address 0x{:x} to force device-lost."), faultDeviceAddress);

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    auto moduleInfo = VulkanDetail::MakeVkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    moduleInfo.codeSize = sizeof(s_FaultComputeSpirv);
    moduleInfo.pCode = s_FaultComputeSpirv;
    if(vkCreateShaderModule(m_context.device, &moduleInfo, m_context.allocationCallbacks, &shaderModule) != VK_SUCCESS)
        return;

    VkPushConstantRange pushConstantRange = {};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(u64);

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    auto layoutInfo = VulkanDetail::MakeVkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if(vkCreatePipelineLayout(m_context.device, &layoutInfo, m_context.allocationCallbacks, &pipelineLayout) != VK_SUCCESS){
        vkDestroyShaderModule(m_context.device, shaderModule, m_context.allocationCallbacks);
        return;
    }

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    pipelineInfo.stage = VulkanDetail::MakeVkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    if(vkCreateComputePipelines(m_context.device, VK_NULL_HANDLE, 1, &pipelineInfo, m_context.allocationCallbacks, &pipeline) != VK_SUCCESS){
        vkDestroyPipelineLayout(m_context.device, pipelineLayout, m_context.allocationCallbacks);
        vkDestroyShaderModule(m_context.device, shaderModule, m_context.allocationCallbacks);
        return;
    }

    auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsQueue->m_queueFamilyIndex;
    if(vkCreateCommandPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &commandPool) == VK_SUCCESS){
        auto allocInfo = VulkanDetail::MakeVkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if(vkAllocateCommandBuffers(m_context.device, &allocInfo, &commandBuffer) == VK_SUCCESS){
            auto beginInfo = VulkanDetail::MakeVkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            vkBeginCommandBuffer(commandBuffer, &beginInfo);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(u64), &faultDeviceAddress);
            vkCmdDispatch(commandBuffer, 1, 1, 1);
            vkEndCommandBuffer(commandBuffer);

            auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;
            vkQueueSubmit(graphicsQueue->m_queue, 1, &submitInfo, VK_NULL_HANDLE);

            // Block until the page fault trips device-lost; waitForIdle() captures the GPU crash and ships
            // the package (vendor-neutral report + Aftermath .nv-gpudmp) through the registered sink.
            waitForIdle();
        }
    }

    // The device is lost; these objects are released by device teardown. Leave them to the lost device rather
    // than destroying objects referenced by an in-flight, faulted submission.
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

