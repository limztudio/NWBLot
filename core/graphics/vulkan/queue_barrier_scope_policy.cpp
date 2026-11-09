// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "queue_barrier_scope_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


void QueueBarrierScopePolicy::Normalize(const GpuQueueCapability::Mask capabilities, VkPipelineStageFlags2& stageMask, VkAccessFlags2& accessMask)noexcept{
    const u8 capabilityBits = static_cast<u8>(capabilities);
    const b8 graphicsCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    const b8 computeCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) != 0u;
    const b8 transferCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u;
    const b8 hadAccess = accessMask != 0u;
    if(!graphicsCapable && !computeCapable && !transferCapable){
        stageMask = VK_PIPELINE_STAGE_2_NONE;
        accessMask = 0u;
        return;
    }

    constexpr VkPipelineStageFlags2 s_UniversalStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT | VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    constexpr VkPipelineStageFlags2 s_TransferStageMask =
        VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT
        | VK_PIPELINE_STAGE_2_TRANSFER_BIT
        | VK_PIPELINE_STAGE_2_COPY_BIT
        | VK_PIPELINE_STAGE_2_RESOLVE_BIT
        | VK_PIPELINE_STAGE_2_BLIT_BIT
        | VK_PIPELINE_STAGE_2_CLEAR_BIT
        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR
        | VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR
        | VK_PIPELINE_STAGE_2_CONVERT_COOPERATIVE_VECTOR_MATRIX_BIT_NV
    ;
    constexpr VkPipelineStageFlags2 s_GraphicsOnlyStageMask =
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT
        | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT
        | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT
        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
        | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
        | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT
        | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT
        | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT
        | VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT
        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR
        | VK_PIPELINE_STAGE_2_FRAGMENT_DENSITY_PROCESS_BIT_EXT
        | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI
        | VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI
        | VK_PIPELINE_STAGE_2_CLUSTER_CULLING_SHADER_BIT_HUAWEI
    ;
    constexpr VkPipelineStageFlags2 s_ComputeOnlyStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    constexpr VkPipelineStageFlags2 s_GraphicsOrComputeStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT | VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT | VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT;
    VkPipelineStageFlags2 allowedStageMask = s_UniversalStageMask | s_TransferStageMask;
    if(graphicsCapable)
        allowedStageMask |= s_GraphicsOnlyStageMask | s_GraphicsOrComputeStageMask;
    if(computeCapable)
        allowedStageMask |= s_ComputeOnlyStageMask | s_GraphicsOrComputeStageMask;
    stageMask &= allowedStageMask;

    if(stageMask == VK_PIPELINE_STAGE_2_NONE){
        accessMask = 0u;
        return;
    }

    constexpr VkPipelineStageFlags2 s_ShaderPipelineStageMask =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT
        | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT
        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
        | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT
        | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI
        | VK_PIPELINE_STAGE_2_CLUSTER_CULLING_SHADER_BIT_HUAWEI
    ;
    constexpr VkPipelineStageFlags2 s_BuildInputShaderReadStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT;
    constexpr VkPipelineStageFlags2 s_AccelerationStructureReadShaderStageMask =
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT
        | VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT
        | VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT
        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        | VK_PIPELINE_STAGE_2_TASK_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT
        | VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI
        | VK_PIPELINE_STAGE_2_CLUSTER_CULLING_SHADER_BIT_HUAWEI
    ;
    constexpr VkPipelineStageFlags2 s_TransferReadAccessStageMask = (s_TransferStageMask & ~(VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR)) | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    constexpr VkPipelineStageFlags2 s_TransferWriteAccessStageMask = (s_TransferStageMask & ~VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR) | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    const b8 allCommands = (stageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0u;
    const b8 allGraphics = graphicsCapable && (stageMask & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) != 0u;
    const auto stripUnless = [&](const VkAccessFlags2 affectedAccess, const b8 supported)noexcept{
        if(!supported)
            accessMask &= ~affectedAccess;
    };

    stripUnless(VK_ACCESS_2_INDEX_READ_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & (VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT)) != 0u));
    stripUnless(VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & (VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)) != 0u));
    stripUnless(VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0u));
    stripUnless(VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0u));
    stripUnless(VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0u));
    stripUnless(VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0u));
    stripUnless(VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT, graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT) != 0u));
    stripUnless(VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR, graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) != 0u));
    stripUnless(VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT, graphicsCapable && (allCommands || allGraphics || (stageMask & (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI)) != 0u));
    stripUnless(VK_ACCESS_2_SHADER_READ_BIT, (graphicsCapable || computeCapable) && (allCommands || (stageMask & (s_ShaderPipelineStageMask | s_BuildInputShaderReadStageMask)) != 0u));
    stripUnless(
        VK_ACCESS_2_UNIFORM_READ_BIT
            | VK_ACCESS_2_SHADER_WRITE_BIT
            | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
            | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        (graphicsCapable || computeCapable)
            && (allCommands || (stageMask & s_ShaderPipelineStageMask) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        (graphicsCapable || computeCapable || transferCapable)
            && (allCommands || (stageMask & (VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR)) != 0u)
    );
    stripUnless(VK_ACCESS_2_TRANSFER_READ_BIT, allCommands || (stageMask & s_TransferReadAccessStageMask) != 0u);
    stripUnless(VK_ACCESS_2_TRANSFER_WRITE_BIT, allCommands || (stageMask & s_TransferWriteAccessStageMask) != 0u);
    stripUnless(
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        allCommands
            || (stageMask & (VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR | s_AccelerationStructureReadShaderStageMask)) != 0u
    );
    stripUnless(VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR, allCommands || (stageMask & (VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR)) != 0u);
    stripUnless(VK_ACCESS_2_MICROMAP_READ_BIT_EXT, computeCapable && (stageMask & (VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR)) != 0u);
    stripUnless(VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT, computeCapable && (stageMask & VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT) != 0u);
    stripUnless(VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT, (graphicsCapable || computeCapable) && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT) != 0u));
    stripUnless(VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT | VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT, (graphicsCapable || computeCapable) && (allCommands || (stageMask & VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT) != 0u));
    stripUnless(VK_ACCESS_2_FRAGMENT_DENSITY_MAP_READ_BIT_EXT, graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_FRAGMENT_DENSITY_PROCESS_BIT_EXT) != 0u));
    stripUnless(VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI, graphicsCapable && (stageMask & VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI) != 0u);
    stripUnless(VK_ACCESS_2_MEMORY_DECOMPRESSION_READ_BIT_EXT | VK_ACCESS_2_MEMORY_DECOMPRESSION_WRITE_BIT_EXT, (graphicsCapable || computeCapable) && (stageMask & VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT) != 0u);

    if(hadAccess && accessMask == 0u)
        stageMask = VK_PIPELINE_STAGE_2_NONE;
}


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

