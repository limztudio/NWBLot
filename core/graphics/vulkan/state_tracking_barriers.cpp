// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "state_tracking_detail.h"

#include <core/common/log.h>
#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanStateTrackingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void NormalizeBarrierScopeForQueueCapabilities(
    const GpuQueueCapability::Mask capabilities,
    VkPipelineStageFlags2& stageMask,
    VkAccessFlags2& accessMask
)noexcept{
    const u8 capabilityBits = static_cast<u8>(capabilities);
    const bool graphicsCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    const bool computeCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) != 0u;
    const bool transferCapable = (capabilityBits & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u;
    const bool hadAccess = accessMask != 0u;
    if(!graphicsCapable && !computeCapable && !transferCapable){
        stageMask = VK_PIPELINE_STAGE_2_NONE;
        accessMask = 0u;
        return;
    }

    constexpr VkPipelineStageFlags2 s_UniversalStageMask =
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
        | VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
        | VK_PIPELINE_STAGE_2_HOST_BIT
        | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    ;
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
    constexpr VkPipelineStageFlags2 s_ComputeOnlyStageMask =
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        | VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
        | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT
    ;
    constexpr VkPipelineStageFlags2 s_GraphicsOrComputeStageMask =
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
        | VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT
        | VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT
        | VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT
    ;
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
    constexpr VkPipelineStageFlags2 s_BuildInputShaderReadStageMask =
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
        | VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT
    ;
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
    constexpr VkPipelineStageFlags2 s_TransferReadAccessStageMask =
        (s_TransferStageMask & ~(VK_PIPELINE_STAGE_2_CLEAR_BIT | VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR))
        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    ;
    constexpr VkPipelineStageFlags2 s_TransferWriteAccessStageMask =
        (s_TransferStageMask & ~VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR)
        | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
    ;
    const bool allCommands = (stageMask & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) != 0u;
    const bool allGraphics = graphicsCapable && (stageMask & VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT) != 0u;
    const auto stripUnless = [&](const VkAccessFlags2 affectedAccess, const bool supported){
        if(!supported)
            accessMask &= ~affectedAccess;
    };

    stripUnless(
        VK_ACCESS_2_INDEX_READ_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & (VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT)) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & (VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
        graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_FRAGMENT_SHADING_RATE_ATTACHMENT_READ_BIT_KHR,
        graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT,
        graphicsCapable
            && (
                allCommands
                || allGraphics
                || (stageMask & (VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_SUBPASS_SHADER_BIT_HUAWEI)) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_SHADER_READ_BIT,
        (graphicsCapable || computeCapable)
            && (
                allCommands
                || (stageMask & (s_ShaderPipelineStageMask | s_BuildInputShaderReadStageMask)) != 0u
            )
    );
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
            && (
                allCommands
                || (stageMask & (
                    VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
                    | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
                    | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                    | VK_PIPELINE_STAGE_2_COPY_INDIRECT_BIT_KHR
                )) != 0u
            )
    );
    stripUnless(
        VK_ACCESS_2_TRANSFER_READ_BIT,
        allCommands || (stageMask & s_TransferReadAccessStageMask) != 0u
    );
    stripUnless(
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        allCommands || (stageMask & s_TransferWriteAccessStageMask) != 0u
    );
    stripUnless(
        VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
        allCommands
            || (stageMask & (
                VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR
                | s_AccelerationStructureReadShaderStageMask
            )) != 0u
    );
    stripUnless(
        VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        allCommands
            || (stageMask & (
                VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
                | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR
            )) != 0u
    );
    stripUnless(
        VK_ACCESS_2_MICROMAP_READ_BIT_EXT,
        computeCapable
            && (stageMask & (VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR)) != 0u
    );
    stripUnless(
        VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT,
        computeCapable && (stageMask & VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT) != 0u
    );
    stripUnless(
        VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT,
        (graphicsCapable || computeCapable)
            && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_COMMAND_PREPROCESS_READ_BIT_EXT | VK_ACCESS_2_COMMAND_PREPROCESS_WRITE_BIT_EXT,
        (graphicsCapable || computeCapable)
            && (allCommands || (stageMask & VK_PIPELINE_STAGE_2_COMMAND_PREPROCESS_BIT_EXT) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_FRAGMENT_DENSITY_MAP_READ_BIT_EXT,
        graphicsCapable && (allCommands || allGraphics || (stageMask & VK_PIPELINE_STAGE_2_FRAGMENT_DENSITY_PROCESS_BIT_EXT) != 0u)
    );
    stripUnless(
        VK_ACCESS_2_INVOCATION_MASK_READ_BIT_HUAWEI,
        graphicsCapable && (stageMask & VK_PIPELINE_STAGE_2_INVOCATION_MASK_BIT_HUAWEI) != 0u
    );
    stripUnless(
        VK_ACCESS_2_MEMORY_DECOMPRESSION_READ_BIT_EXT | VK_ACCESS_2_MEMORY_DECOMPRESSION_WRITE_BIT_EXT,
        (graphicsCapable || computeCapable)
            && (stageMask & VK_PIPELINE_STAGE_2_MEMORY_DECOMPRESSION_BIT_EXT) != 0u
    );

    if(hadAccess && accessMask == 0u)
        stageMask = VK_PIPELINE_STAGE_2_NONE;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkImageMemoryBarrier2 BuildTextureStateBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    auto barrier = VulkanDetail::MakeVkStruct<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.oldLayout = oldState != ResourceStates::Unknown ? VulkanDetail::GetVkImageLayout(oldState) : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VulkanDetail::GetVkImageLayout(stateBits);
    // Ordinary transitions use Vulkan's explicit ignored queue-family sentinel.
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange = VulkanDetail::BuildImageSubresourceRange(subresources, aspectMask);

    return barrier;
}

ResourceStates::Mask NormalizeOwnershipState(const ResourceStates::Mask state){
    return state != ResourceStates::Unknown ? state : ResourceStates::Common;
}

VkImageMemoryBarrier2 BuildTextureOwnershipReleaseBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    return barrier;
}

VkImageMemoryBarrier2 BuildTextureOwnershipAcquireBarrier(
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = BuildTextureStateBarrier(image, aspectMask, subresources, resolvedState, resolvedState, rayTracingStageAvailable);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipReleaseBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.dstAccessMask = 0u;
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

VkBufferMemoryBarrier2 BuildBufferOwnershipAcquireBarrier(
    const VkBuffer buffer,
    const ResourceStates::Mask state,
    const u32 sourceQueueFamily,
    const u32 destinationQueueFamily,
    const bool rayTracingStageAvailable
){
    const ResourceStates::Mask resolvedState = NormalizeOwnershipState(state);
    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
    barrier.srcAccessMask = 0u;
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(resolvedState, rayTracingStageAvailable);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(resolvedState);
    barrier.srcQueueFamilyIndex = sourceQueueFamily;
    barrier.dstQueueFamilyIndex = destinationQueueFamily;
    barrier.buffer = buffer;
    barrier.offset = 0u;
    barrier.size = VK_WHOLE_SIZE;
    return barrier;
}

bool NeedsTextureStateBarrier(const ResourceStates::Mask oldState, const ResourceStates::Mask stateBits, const bool uavBarrierEnabled){
    return oldState != stateBits || (oldState == stateBits && uavBarrierEnabled);
}

bool ImageBarrierOverlapsTextureSubresources(
    const VkImageMemoryBarrier2& barrier,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources
)noexcept{
    if(barrier.image != image || (barrier.subresourceRange.aspectMask & aspectMask) == 0u)
        return false;
    if(
        barrier.subresourceRange.levelCount == 0u
        || barrier.subresourceRange.layerCount == 0u
        || subresources.numMipLevels == 0u
        || subresources.numArraySlices == 0u
    )
        return false;

    const u64 barrierMipEnd = static_cast<u64>(barrier.subresourceRange.baseMipLevel) + barrier.subresourceRange.levelCount;
    const u64 attachmentMipEnd = static_cast<u64>(subresources.baseMipLevel) + subresources.numMipLevels;
    if(
        static_cast<u64>(barrier.subresourceRange.baseMipLevel) >= attachmentMipEnd
        || static_cast<u64>(subresources.baseMipLevel) >= barrierMipEnd
    )
        return false;

    const u64 barrierLayerEnd = static_cast<u64>(barrier.subresourceRange.baseArrayLayer) + barrier.subresourceRange.layerCount;
    const u64 attachmentLayerEnd = static_cast<u64>(subresources.baseArraySlice) + subresources.numArraySlices;
    return
        static_cast<u64>(barrier.subresourceRange.baseArrayLayer) < attachmentLayerEnd
        && static_cast<u64>(subresources.baseArraySlice) < barrierLayerEnd
    ;
}

void AppendTextureStateBarrier(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const ArraySlice arraySlice,
    const MipLevel mipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    barriers.push_back(BuildTextureStateBarrier(
        image,
        aspectMask,
        TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
        oldState,
        stateBits,
        rayTracingStageAvailable
    ));
}

void AppendTextureStateBarriersBefore(
    Vector<VkImageMemoryBarrier2, Alloc::GlobalArena>& barriers,
    const VkImage image,
    const VkImageAspectFlags aspectMask,
    const TextureSubresourceSet& subresources,
    const MipLevel mipEnd,
    const ArraySlice currentArraySlice,
    const MipLevel currentMipLevel,
    const ResourceStates::Mask oldState,
    const ResourceStates::Mask stateBits,
    const bool rayTracingStageAvailable
){
    for(ArraySlice arraySlice = subresources.baseArraySlice; arraySlice <= currentArraySlice; ++arraySlice){
        const MipLevel previousMipEnd = arraySlice == currentArraySlice ? currentMipLevel : mipEnd;
        for(MipLevel mipLevel = subresources.baseMipLevel; mipLevel < previousMipEnd; ++mipLevel)
            AppendTextureStateBarrier(barriers, image, aspectMask, arraySlice, mipLevel, oldState, stateBits, rayTracingStageAvailable);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CommandList::executePipelineBarrier(const VkDependencyInfo& depInfo){
    if(!validateCommandRecordingScope(NWB_TEXT("pipeline barrier")))
        return;

    Framebuffer* resumeFramebuffer = nullptr;
    if(m_renderPassActive){
        resumeFramebuffer = m_renderPassFramebuffer;

        const auto attachmentOverlapsBarrier = [&](const FramebufferAttachment& attachment){
            if(!attachment.texture)
                return false;

            const TextureSubresourceSet resolvedSubresources = attachment.subresources.resolve(
                attachment.texture->m_desc,
                TextureSubresourceMipResolve::Single
            );
            for(u32 index = 0u; index < depInfo.imageMemoryBarrierCount; ++index){
                if(VulkanStateTrackingDetail::ImageBarrierOverlapsTextureSubresources(
                    depInfo.pImageMemoryBarriers[index],
                    attachment.texture->m_image,
                    attachment.texture->m_aspectMask,
                    resolvedSubresources
                ))
                    return true;
            }
            return false;
        };

        const FramebufferDesc& framebufferDesc = resumeFramebuffer->m_desc;
        bool attachmentTransitioned = attachmentOverlapsBarrier(framebufferDesc.depthAttachment)
            || attachmentOverlapsBarrier(framebufferDesc.shadingRateAttachment)
        ;
        for(const FramebufferAttachment& attachment : framebufferDesc.colorAttachments)
            attachmentTransitioned = attachmentTransitioned || attachmentOverlapsBarrier(attachment);
        if(attachmentTransitioned)
            resumeFramebuffer = nullptr;

        endDynamicRendering();
        m_renderPassActive = false;
        m_renderPassFramebuffer = nullptr;
    }

    // Normalize against the exact selected queue rather than its broad API class. Auxiliary queues may expose a
    // strict Graphics-only, Compute-only, or Transfer-only capability set even when their class shares a facade.
    // Cross-queue semaphore waits make an unsupported producer scope available before this command list; lower that
    // side to NONE instead of naming an illegal local access.
    VkDependencyInfo queueCompatibleDepInfo = depInfo;
    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_StateHandoffArena);
    Vector<VkMemoryBarrier2, Alloc::ScratchArena> queueCompatibleMemoryBarriers{scratchArena};
    Vector<VkImageMemoryBarrier2, Alloc::ScratchArena> queueCompatibleImageBarriers{scratchArena};
    Vector<VkBufferMemoryBarrier2, Alloc::ScratchArena> queueCompatibleBufferBarriers{scratchArena};
    const GpuPhysicalQueueInfo* const exactQueueInfo = m_device.getPhysicalQueueInfo(m_creationDesc.physicalQueue);
    const GpuQueueCapability::Mask exactQueueCapabilities = exactQueueInfo
        ? exactQueueInfo->capabilities
        : GpuQueueCapability::None
    ;
    const u8 exactCapabilityBits = static_cast<u8>(exactQueueCapabilities);
    const bool graphicsCapable = (exactCapabilityBits & static_cast<u8>(GpuQueueCapability::Graphics)) != 0u;
    const bool computeCapable = (exactCapabilityBits & static_cast<u8>(GpuQueueCapability::Compute)) != 0u;
    const bool universalGraphicsCompute = graphicsCapable && computeCapable;
    if(!universalGraphicsCompute){
        queueCompatibleMemoryBarriers.reserve(depInfo.memoryBarrierCount);
        for(u32 index = 0u; index < depInfo.memoryBarrierCount; ++index){
            VkMemoryBarrier2 barrier = depInfo.pMemoryBarriers[index];
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.srcStageMask, barrier.srcAccessMask);
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleMemoryBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pMemoryBarriers = queueCompatibleMemoryBarriers.data();

        queueCompatibleImageBarriers.reserve(depInfo.imageMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.imageMemoryBarrierCount; ++index){
            VkImageMemoryBarrier2 barrier = depInfo.pImageMemoryBarriers[index];
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.srcStageMask, barrier.srcAccessMask);
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleImageBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pImageMemoryBarriers = queueCompatibleImageBarriers.data();

        queueCompatibleBufferBarriers.reserve(depInfo.bufferMemoryBarrierCount);
        for(u32 index = 0u; index < depInfo.bufferMemoryBarrierCount; ++index){
            VkBufferMemoryBarrier2 barrier = depInfo.pBufferMemoryBarriers[index];
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.srcStageMask, barrier.srcAccessMask);
            VulkanStateTrackingDetail::NormalizeBarrierScopeForQueueCapabilities(exactQueueCapabilities, barrier.dstStageMask, barrier.dstAccessMask);
            queueCompatibleBufferBarriers.push_back(barrier);
        }
        queueCompatibleDepInfo.pBufferMemoryBarriers = queueCompatibleBufferBarriers.data();
    }

    vkCmdPipelineBarrier2(m_currentCmdBuf->m_cmdBuf, &queueCompatibleDepInfo);

    if(resumeFramebuffer){
        RenderPassParameters params = {};
        if(beginDynamicRendering(resumeFramebuffer, params)){
            m_renderPassActive = true;
            m_renderPassFramebuffer = resumeFramebuffer;
        }
        else
            rejectCommandRecording(
                NWB_TEXT("pipeline barrier"),
                NWB_TEXT("dynamic rendering could not resume after the barrier")
            );
    }
}

void CommandList::commitBarriers(){
    if(!validateCommandRecordingScope(NWB_TEXT("commit barriers")))
        return;

    if(m_pendingImageBarriers.empty() && m_pendingBufferBarriers.empty())
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(m_pendingImageBarriers.size());
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<u32>(m_pendingBufferBarriers.size());
    depInfo.pBufferMemoryBarriers = m_pendingBufferBarriers.data();

    executePipelineBarrier(depInfo);

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::setTextureState(Texture* textureResource, TextureSubresourceSet subresources, ResourceStates::Mask stateBits){
    if(!textureResource)
        return;
    constexpr const tchar* s_OperationName = NWB_TEXT("set texture state");
    if(!validateCommandRecordingScope(s_OperationName))
        return;
    if(!validateTextureForGpuState(textureResource, stateBits, s_OperationName))
        return;

    Texture& texture = *textureResource;
    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(s_OperationName, NWB_TEXT("subresource range is empty or outside the texture"));
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(&texture);

    ResourceStates::Mask oldState = ResourceStates::Unknown;
    bool firstSubresource = true;
    bool needsBarrier = false;
    bool usePerSubresourceBarriers = false;
    const bool uavBarrierEnabled = ResourceStates::HasUnorderedAccess(stateBits) && m_stateTracker.isUavBarrierEnabledForTexture(texture);
    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;
    const usize subresourceCount = static_cast<usize>(resolvedSubresources.numMipLevels) * static_cast<usize>(resolvedSubresources.numArraySlices);
    usize firstBarrierIndex = m_pendingImageBarriers.size();

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            ResourceStates::Mask subresourceOldState = permanentState;
            if(
                permanentState == ResourceStates::Unknown
                && !m_stateTracker.getResolvedTransientTextureState(texture, arraySlice, mipLevel, subresourceOldState)
            ){
                rejectCommandRecording(
                    NWB_TEXT("set texture state"),
                    NWB_TEXT("tracked texture subresource state could not be resolved")
                );
                return;
            }

            if(firstSubresource){
                oldState = subresourceOldState;
                firstSubresource = false;
            }
            else if(subresourceOldState != oldState && !usePerSubresourceBarriers){
                usePerSubresourceBarriers = true;
                firstBarrierIndex = m_pendingImageBarriers.size();
                ::ContainerDetail::ReserveAdditionalCapacity(m_pendingImageBarriers, subresourceCount);
                if(VulkanStateTrackingDetail::NeedsTextureStateBarrier(oldState, stateBits, uavBarrierEnabled)){
                    VulkanStateTrackingDetail::AppendTextureStateBarriersBefore(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        resolvedSubresources,
                        mipEnd,
                        arraySlice,
                        mipLevel,
                        oldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }

            const bool subresourceNeedsBarrier = VulkanStateTrackingDetail::NeedsTextureStateBarrier(
                subresourceOldState,
                stateBits,
                uavBarrierEnabled
            );
            if(subresourceNeedsBarrier){
                needsBarrier = true;
                if(usePerSubresourceBarriers){
                    VulkanStateTrackingDetail::AppendTextureStateBarrier(
                        m_pendingImageBarriers,
                        texture.m_image,
                        texture.m_aspectMask,
                        arraySlice,
                        mipLevel,
                        subresourceOldState,
                        stateBits,
                        m_context.extensions.KHR_ray_tracing_pipeline
                    );
                }
            }
        }
    }

    retainResource(&texture);
    if(!needsBarrier)
        return;

    if(!usePerSubresourceBarriers){
        const VkImageMemoryBarrier2 barrier = VulkanStateTrackingDetail::BuildTextureStateBarrier(
            texture.m_image,
            texture.m_aspectMask,
            resolvedSubresources,
            oldState,
            stateBits,
            m_context.extensions.KHR_ray_tracing_pipeline
        );

        if(permanentState == ResourceStates::Unknown)
            m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

        if(!m_enableAutomaticBarriers){
            m_pendingImageBarriers.push_back(barrier);
            return;
        }

        auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;

        executePipelineBarrier(depInfo);
        return;
    }

    if(permanentState == ResourceStates::Unknown)
        m_stateTracker.beginTrackingResolvedTransientTexture(texture, resolvedSubresources, stateBits);

    if(!m_enableAutomaticBarriers)
        return;

    const usize newBarrierCount = m_pendingImageBarriers.size() - firstBarrierIndex;
    if(newBarrierCount == 0)
        return;

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.imageMemoryBarrierCount = static_cast<u32>(newBarrierCount);
    depInfo.pImageMemoryBarriers = m_pendingImageBarriers.data() + firstBarrierIndex;

    executePipelineBarrier(depInfo);
    m_pendingImageBarriers.resize(firstBarrierIndex);
}

void CommandList::setBufferState(Buffer* bufferResource, ResourceStates::Mask stateBits){
    if(!bufferResource)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("set buffer state")))
        return;
    if(!validateBufferForGpuState(bufferResource, stateBits, NWB_TEXT("set buffer state")))
        return;

    Buffer& buffer = *bufferResource;
    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(&buffer);

    ResourceStates::Mask oldState = permanentState;
    if(permanentState == ResourceStates::Unknown && !m_stateTracker.getTransientBufferState(buffer, oldState)){
        rejectCommandRecording(NWB_TEXT("set buffer state"), NWB_TEXT("tracked buffer state could not be resolved"));
        return;
    }

    if(
        VulkanDetail::HasBufferDeviceWriteState(oldState)
        || VulkanDetail::HasBufferDeviceWriteState(stateBits)
    )
        registerHostReadbackBuffer(buffer);

    const bool needsUavBarrier =
        oldState == stateBits
        && ResourceStates::HasUnorderedAccess(stateBits)
        && m_stateTracker.isUavBarrierEnabledForBuffer(buffer)
    ;

    if(oldState == stateBits && !needsUavBarrier)
        return;
    retainResource(&buffer);

    auto barrier = VulkanDetail::MakeVkStruct<VkBufferMemoryBarrier2>(VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    barrier.srcStageMask = VulkanDetail::GetVkPipelineStageFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.srcAccessMask = VulkanDetail::GetVkAccessFlags(oldState != ResourceStates::Unknown ? oldState : ResourceStates::Common);
    barrier.dstStageMask = VulkanDetail::GetVkPipelineStageFlags(stateBits, m_context.extensions.KHR_ray_tracing_pipeline);
    barrier.dstAccessMask = VulkanDetail::GetVkAccessFlags(stateBits);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer.m_buffer;
    barrier.offset = 0;
    barrier.size = VK_WHOLE_SIZE;

    if(permanentState == ResourceStates::Unknown)
        m_stateTracker.beginTrackingTransientBuffer(buffer, stateBits);

    if(!m_enableAutomaticBarriers){
        m_pendingBufferBarriers.push_back(barrier);
        return;
    }

    auto depInfo = VulkanDetail::MakeVkStruct<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers = &barrier;

    executePipelineBarrier(depInfo);
}

void CommandList::setAccelStructState(RayTracingAccelStruct* accelStructResource, ResourceStates::Mask stateBits){
    if(!accelStructResource)
        return;

    auto* as = accelStructResource;
    if(as->m_buffer)
        setBufferState(as->m_buffer.get(), stateBits);
}

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const RenderLane::Enum destinationLane
){
    releaseTextureOwnership(textureResource, subresources, m_device.resolveRenderLane(destinationLane));
}

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const CommandQueue::Enum destinationQueue
){
    releaseTextureOwnership(textureResource, subresources, m_device.getPrimaryPhysicalQueue(destinationQueue));
}

void CommandList::releaseTextureOwnership(
    Texture* textureResource,
    TextureSubresourceSet subresources,
    const GpuPhysicalQueueId destinationQueue
){
    if(!textureResource)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("release texture ownership")))
        return;

    Texture& texture = *textureResource;
    if(!m_device.isTextureReadyForGpuUse(&texture)){
        rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("texture is not ready for GPU access"));
        return;
    }
    if(m_stateTracker.isPermanentTexture(texture)){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("permanently tracked textures cannot transfer ownership")
        );
        return;
    }
    if(m_device.usesConcurrentQueueSharing(texture.m_desc.queueSharing)){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("concurrently shared textures do not have exclusive ownership")
        );
        return;
    }

    const TextureSubresourceSet resolvedSubresources = subresources.resolve(texture.m_desc, TextureSubresourceMipResolve::Range);
    if(!VulkanDetail::IsTextureSubresourceRangeValid(resolvedSubresources)){
        rejectCommandRecording(
            NWB_TEXT("release texture ownership"),
            NWB_TEXT("subresource range is empty or outside the texture")
        );
        return;
    }

    if(!m_device.getQueue(destinationQueue)){
        rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("destination queue is unavailable"));
        return;
    }

    const MipLevel mipEnd = resolvedSubresources.baseMipLevel + resolvedSubresources.numMipLevels;
    const ArraySlice arrayEnd = resolvedSubresources.baseArraySlice + resolvedSubresources.numArraySlices;

    // Releases require explicit image layouts; never invent one for untouched images. Validate every affected
    // subresource before publishing any tracked state or destination.
    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            if(state == ResourceStates::Unknown){
                rejectCommandRecording(NWB_TEXT("release texture ownership"), NWB_TEXT("final resource state is unknown"));
                return;
            }
            const TextureSubresourceStateKey key{ &texture, mipLevel, arraySlice };
            const auto existing = m_textureOwnershipReleaseDestinations.find(key);
            if(existing != m_textureOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
                rejectCommandRecording(
                    NWB_TEXT("release texture ownership"),
                    NWB_TEXT("subresource already targets a conflicting destination queue")
                );
                return;
            }
        }
    }

    for(ArraySlice arraySlice = resolvedSubresources.baseArraySlice; arraySlice < arrayEnd; ++arraySlice){
        for(MipLevel mipLevel = resolvedSubresources.baseMipLevel; mipLevel < mipEnd; ++mipLevel){
            const ResourceStates::Mask state = m_stateTracker.getTextureState(&texture, arraySlice, mipLevel);
            m_stateTracker.beginTrackingTexture(
                &texture,
                TextureSubresourceSet(mipLevel, 1u, arraySlice, 1u),
                state
            );
            m_textureOwnershipReleaseDestinations.insert_or_assign(TextureSubresourceStateKey{ &texture, mipLevel, arraySlice }, destinationQueue);
        }
    }
    retainResource(&texture);
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const RenderLane::Enum destinationLane){
    if(!bufferResource)
        return;
    releaseBufferOwnership(bufferResource, m_device.resolveRenderLane(destinationLane));
}

void CommandList::releaseBufferOwnership(Buffer* bufferResource, const CommandQueue::Enum destinationQueue){
    if(!bufferResource)
        return;
    releaseBufferOwnership(bufferResource, m_device.getPrimaryPhysicalQueue(destinationQueue));
}

void CommandList::releaseBufferOwnership(
    Buffer* bufferResource,
    const GpuPhysicalQueueId destinationQueue
){
    if(!bufferResource)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("release buffer ownership")))
        return;

    Buffer& buffer = *bufferResource;
    if(!m_device.isBufferReadyForGpuUse(&buffer)){
        rejectCommandRecording(NWB_TEXT("release buffer ownership"), NWB_TEXT("buffer is not ready for GPU access"));
        return;
    }
    if(m_stateTracker.isPermanentBuffer(buffer)){
        rejectCommandRecording(
            NWB_TEXT("release buffer ownership"),
            NWB_TEXT("permanently tracked buffers cannot transfer ownership")
        );
        return;
    }
    if(m_device.usesConcurrentQueueSharing(buffer.m_desc.queueSharing)){
        rejectCommandRecording(
            NWB_TEXT("release buffer ownership"),
            NWB_TEXT("concurrently shared buffers do not have exclusive ownership")
        );
        return;
    }

    if(!m_device.getQueue(destinationQueue)){
        rejectCommandRecording(NWB_TEXT("release buffer ownership"), NWB_TEXT("destination queue is unavailable"));
        return;
    }

    const auto existing = m_bufferOwnershipReleaseDestinations.find(&buffer);
    if(existing != m_bufferOwnershipReleaseDestinations.end() && existing.value() != destinationQueue){
        rejectCommandRecording(
            NWB_TEXT("release buffer ownership"),
            NWB_TEXT("buffer already targets a conflicting destination queue")
        );
        return;
    }

    // Exports need concrete buffer state; tracked state takes precedence over descriptor initial state.
    ResourceStates::Mask state = m_stateTracker.getBufferState(&buffer);
    if(state == ResourceStates::Unknown)
        state = buffer.m_desc.initialState;
    if(state == ResourceStates::Unknown){
        rejectCommandRecording(NWB_TEXT("release buffer ownership"), NWB_TEXT("final resource state is unknown"));
        return;
    }
    m_stateTracker.beginTrackingBuffer(&buffer, state);

    m_bufferOwnershipReleaseDestinations.insert_or_assign(&buffer, destinationQueue);
    retainResource(&buffer);
}

void CommandList::setPermanentTextureState(Texture* texture, ResourceStates::Mask stateBits){
    if(!texture)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("set permanent texture state")))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(NWB_TEXT("set permanent texture state"), NWB_TEXT("permanent state cannot be unknown"));
        return;
    }
    if(!m_device.isTextureReadyForGpuUse(texture)){
        rejectCommandRecording(NWB_TEXT("set permanent texture state"), NWB_TEXT("texture is not ready for GPU access"));
        return;
    }
    if(texture->m_desc.keepInitialState && texture->m_desc.initialState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent texture state"),
            NWB_TEXT("permanent state conflicts with the retained initial state")
        );
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentTextureState(texture);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent texture state"),
            NWB_TEXT("a different permanent state is already tracked")
        );
        return;
    }
    for(auto it = m_textureOwnershipReleaseDestinations.begin(); it != m_textureOwnershipReleaseDestinations.end(); ++it){
        if(it->first.texture == texture){
            rejectCommandRecording(
                NWB_TEXT("set permanent texture state"),
                NWB_TEXT("texture already has a pending ownership release")
            );
            return;
        }
    }

    setTextureState(texture, s_AllSubresources, stateBits);
    if(m_commandRecordingFailed)
        return;
    retainResource(texture);
    m_stateTracker.setPermanentTextureState(*texture, stateBits);
}

void CommandList::setPermanentBufferState(Buffer* buffer, ResourceStates::Mask stateBits){
    if(!buffer)
        return;
    if(!validateCommandRecordingScope(NWB_TEXT("set permanent buffer state")))
        return;
    if(stateBits == ResourceStates::Unknown){
        rejectCommandRecording(NWB_TEXT("set permanent buffer state"), NWB_TEXT("permanent state cannot be unknown"));
        return;
    }
    if(!m_device.isBufferReadyForGpuUse(buffer)){
        rejectCommandRecording(NWB_TEXT("set permanent buffer state"), NWB_TEXT("buffer is not ready for GPU access"));
        return;
    }
    if(buffer->m_desc.keepInitialState && buffer->m_desc.initialState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent buffer state"),
            NWB_TEXT("permanent state conflicts with the retained initial state")
        );
        return;
    }

    const ResourceStates::Mask permanentState = m_stateTracker.getPermanentBufferState(buffer);
    if(permanentState != ResourceStates::Unknown && permanentState != stateBits){
        rejectCommandRecording(
            NWB_TEXT("set permanent buffer state"),
            NWB_TEXT("a different permanent state is already tracked")
        );
        return;
    }
    if(m_bufferOwnershipReleaseDestinations.find(buffer) != m_bufferOwnershipReleaseDestinations.end()){
        rejectCommandRecording(
            NWB_TEXT("set permanent buffer state"),
            NWB_TEXT("buffer already has a pending ownership release")
        );
        return;
    }

    setBufferState(buffer, stateBits);
    if(m_commandRecordingFailed)
        return;
    retainResource(buffer);
    m_stateTracker.setPermanentBufferState(*buffer, stateBits);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

