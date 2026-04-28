// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandList::CommandList(Device& device, const CommandListParameters& params)
    : RefCounter<ICommandList>(device.m_context.threadPool)
    , m_desc(params)
    , m_stateTracker(MakeCustomUnique<StateTracker>(device.m_context.objectArena, device.m_context))
    , m_device(device)
    , m_context(device.m_context)
    , m_aftermathMarkerTracker()
    , m_pendingImageBarriers(Alloc::CustomAllocator<VkImageMemoryBarrier2>(device.m_context.objectArena))
    , m_pendingBufferBarriers(Alloc::CustomAllocator<VkBufferMemoryBarrier2>(device.m_context.objectArena))
    , m_pendingCompactions(Alloc::CustomAllocator<RefCountPtr<AccelStruct, ArenaRefDeleter<AccelStruct>>>(device.m_context.objectArena))
{
    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().registerAftermathMarkerTracker(m_aftermathMarkerTracker);
}
CommandList::~CommandList(){
    discardUnsubmittedUploadChunks();

    if(m_device.isAftermathEnabled())
        m_device.getAftermathCrashDumpHelper().unRegisterAftermathMarkerTracker(m_aftermathMarkerTracker);
}

void CommandList::discardUnsubmittedUploadChunks(){
    if(!m_currentCmdBuf)
        return;

    if(m_currentCmdBuf->m_signalFenceQuery)
        m_currentCmdBuf->m_signalFenceQuery->m_started = false;
    m_currentCmdBuf->m_signalFence = VK_NULL_HANDLE;
    m_currentCmdBuf->m_signalFenceQuery = nullptr;

    if(!m_device.m_uploadManager && !m_device.m_scratchManager)
        return;

    TrackedCommandBuffer* owner = m_currentCmdBuf.get();
    const u64 reusableVersion = m_device.queueGetCompletedInstance(m_desc.queueType);

    if(m_device.m_uploadManager)
        m_device.m_uploadManager->discardChunks(m_desc.queueType, owner, reusableVersion);
    if(m_device.m_scratchManager)
        m_device.m_scratchManager->discardChunks(m_desc.queueType, owner, reusableVersion);
}

void CommandList::open(){
    VkResult res = VK_SUCCESS;

    discardUnsubmittedUploadChunks();
    m_currentCmdBuf.reset();

    Queue* queue = m_device.getQueue(m_desc.queueType);
    if(!queue){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Requested queue is not available"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Requested queue is not available"));
        m_currentCmdBuf = nullptr;
        return;
    }

    m_currentCmdBuf = queue->getOrCreateCommandBuffer();
    if(!m_currentCmdBuf || m_currentCmdBuf->m_cmdBuf == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to acquire command buffer"));
        m_currentCmdBuf = nullptr;
        return;
    }

    auto beginInfo = VulkanDetail::MakeVkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    res = vkBeginCommandBuffer(m_currentCmdBuf->m_cmdBuf, &beginInfo);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to begin command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to begin command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf = nullptr;
        return;
    }

    m_stateTracker->reset();
}

void CommandList::close(){
    VkResult res = VK_SUCCESS;

    if(!m_currentCmdBuf){
        clearState();
        return;
    }

    endActiveRenderPass();
    m_stateTracker->appendKeepInitialStateBarriers(m_pendingImageBarriers, m_pendingBufferBarriers);
    commitBarriers();

    res = vkEndCommandBuffer(m_currentCmdBuf->m_cmdBuf);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to end command buffer recording: {}"), ResultToString(res));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to end command buffer recording"));
        discardUnsubmittedUploadChunks();
        m_currentCmdBuf.reset();
        clearState();
        return;
    }

    clearState();
}

void CommandList::clearState(){
    if(m_currentCmdBuf && m_renderPassActive)
        endActiveRenderPass();

    m_stateTracker->reset();

    m_currentGraphicsState = {};
    m_currentComputeState = {};
    m_currentMeshletState = {};
    m_currentRayTracingState = {};
    m_renderPassActive = false;
    m_renderPassFramebuffer = nullptr;

    m_pendingImageBarriers.clear();
    m_pendingBufferBarriers.clear();
}

void CommandList::retainResource(IResource* resource){
    if(resource)
        m_currentCmdBuf->m_referencedResources.emplace_back(resource, ArenaRefDeleter<IResource>(&m_context.objectArena));
}

void CommandList::retainStagingBuffer(IBuffer* buffer){
    if(buffer)
        m_currentCmdBuf->m_referencedStagingBuffers.emplace_back(buffer, ArenaRefDeleter<IBuffer>(&m_context.objectArena));
}

bool CommandList::validateIndirectBuffer(IBuffer* bufferResource, u64 offsetBytes, u64 commandSizeBytes, u32 commandCount, const tchar* commandName)const{
    if(!bufferResource){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: No indirect buffer bound for {}"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: No indirect buffer bound"));
        return false;
    }

    auto* buffer = checked_cast<Buffer*>(bufferResource);
    if(!buffer->m_desc.isDrawIndirectArgs){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute {}: buffer was not created with indirect-argument usage"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to execute indirect command: buffer was not created with indirect-argument usage"));
        return false;
    }
    if((offsetBytes & 3u) != 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute {}: offset is not 4-byte aligned"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to execute indirect command: offset is not 4-byte aligned"));
        return false;
    }

    const u64 totalBytes = commandSizeBytes * commandCount;
    if(!VulkanDetail::IsBufferRangeInBounds(buffer->m_desc, offsetBytes, totalBytes)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to execute {}: indirect argument range is outside the buffer"), commandName);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to execute indirect command: indirect argument range is outside the buffer"));
        return false;
    }

    return true;
}

bool CommandList::prepareDrawIndirect(
    const u32 offsetBytes,
    const u32 drawCount,
    const u64 commandSizeBytes,
    const tchar* operationLabel,
    const tchar* commandName,
    const bool requireIndexBuffer,
    Buffer*& outIndirectBuffer
)const{
    outIndirectBuffer = nullptr;
    if(drawCount == 0)
        return false;
    if(!m_renderPassActive || !m_currentGraphicsState.pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: no graphics pipeline and active render pass are bound"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: no graphics pipeline and active render pass are bound"), operationLabel);
        return false;
    }
    if(requireIndexBuffer && !m_currentGraphicsState.indexBuffer.buffer){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: no index buffer is bound"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: no index buffer is bound"), operationLabel);
        return false;
    }
    if(drawCount > m_context.physicalDeviceProperties.limits.maxDrawIndirectCount){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to {}: draw count exceeds device limit"), operationLabel);
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to {}: draw count exceeds device limit"), operationLabel);
        return false;
    }
    if(!validateIndirectBuffer(m_currentGraphicsState.indirectParams, offsetBytes, commandSizeBytes, drawCount, commandName))
        return false;

    outIndirectBuffer = checked_cast<Buffer*>(m_currentGraphicsState.indirectParams);
    return true;
}

void CommandList::copyTextureToBuffer(IBuffer* destResource, u64 destOffsetBytes, u32 destRowPitch, ITexture* srcResource, const TextureSlice& srcSlice){
    auto* dest = checked_cast<Buffer*>(destResource);
    auto* src = checked_cast<Texture*>(srcResource);
    if(!dest || !src){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: resource is invalid"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: resource is invalid"));
        return;
    }

    if(!VulkanDetail::IsTextureSliceInBounds(src->m_desc, srcSlice)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: source slice is outside the texture"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: source slice is outside the texture"));
        return;
    }
    if(src->m_desc.sampleCount != 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: source texture must be single-sampled"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: source texture must be single-sampled"));
        return;
    }
    const TextureSlice resolvedSrc = srcSlice.resolve(src->m_desc);

    const FormatInfo& formatInfo = GetFormatInfo(src->m_desc.format);
    VkImageAspectFlags aspectMask = 0;
    if(!VulkanDetail::GetBufferImageCopyAspectMask(formatInfo, NWB_TEXT("copy texture to buffer"), aspectMask)){
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: combined depth/stencil buffer-image copies are not supported"));
        return;
    }

    const u32 formatBlockWidth = GetFormatBlockWidth(formatInfo);
    const u32 formatBlockHeight = GetFormatBlockHeight(formatInfo);
    if(formatBlockWidth == 0 || formatBlockHeight == 0 || formatInfo.bytesPerBlock == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: invalid row pitch"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: invalid row pitch"));
        return;
    }

    const u64 blocksX = Max<u64>((static_cast<u64>(resolvedSrc.width) + formatBlockWidth - 1u) / formatBlockWidth, 1ull);
    const u64 blocksY = Max<u64>((static_cast<u64>(resolvedSrc.height) + formatBlockHeight - 1u) / formatBlockHeight, 1ull);
    if(blocksX > Limit<u64>::s_Max / formatInfo.bytesPerBlock){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: natural row pitch overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: natural row pitch overflows"));
        return;
    }

    const u64 naturalRowPitch = blocksX * formatInfo.bytesPerBlock;
    if(destRowPitch > 0 && (destRowPitch < naturalRowPitch || (destRowPitch % formatInfo.bytesPerBlock) != 0)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: invalid row pitch"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: invalid row pitch"));
        return;
    }

    u64 bufferRowLength = 0;
    if(destRowPitch > 0){
        bufferRowLength = (static_cast<u64>(destRowPitch) / formatInfo.bytesPerBlock) * formatBlockWidth;
        if(bufferRowLength > UINT32_MAX){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: row pitch exceeds Vulkan buffer image copy limits"));
            NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: row pitch exceeds Vulkan buffer image copy limits"));
            return;
        }
    }

    const u64 effectiveRowPitch = destRowPitch > 0 ? destRowPitch : naturalRowPitch;
    if(blocksY > UINT64_MAX / effectiveRowPitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        return;
    }
    const u64 slicePitch = effectiveRowPitch * blocksY;
    const u64 depthOffset = static_cast<u64>(resolvedSrc.depth - 1);
    if(depthOffset > UINT64_MAX / slicePitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        return;
    }
    const u64 depthBytes = depthOffset * slicePitch;
    const u64 rowBytes = static_cast<u64>(blocksY - 1) * effectiveRowPitch;
    if(depthBytes > UINT64_MAX - rowBytes || depthBytes + rowBytes > UINT64_MAX - naturalRowPitch){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range size overflows"));
        return;
    }
    const u64 requiredSize = depthBytes + rowBytes + naturalRowPitch;
    const BufferDesc& destDesc = dest->getDescription();
    if(destOffsetBytes > destDesc.byteSize || requiredSize > destDesc.byteSize - destOffsetBytes){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination offset {} size {} is outside buffer size {}"),
            destOffsetBytes,
            requiredSize,
            destDesc.byteSize
        );
        NWB_ASSERT_MSG(false, NWB_TEXT("Vulkan: Failed to copy texture to buffer: destination range is outside the buffer"));
        return;
    }

    VkBufferImageCopy region{};
    region.bufferOffset = destOffsetBytes;
    region.bufferRowLength = static_cast<u32>(bufferRowLength);
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = resolvedSrc.mipLevel;
    region.imageSubresource.baseArrayLayer = resolvedSrc.arraySlice;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { static_cast<int32_t>(resolvedSrc.x), static_cast<int32_t>(resolvedSrc.y), static_cast<int32_t>(resolvedSrc.z) };
    region.imageExtent = { resolvedSrc.width, resolvedSrc.height, resolvedSrc.depth };

    setTextureState(srcResource, TextureSubresourceSet(resolvedSrc.mipLevel, 1u, resolvedSrc.arraySlice, 1u), ResourceStates::CopySource);
    setBufferState(destResource, ResourceStates::CopyDest);

    vkCmdCopyImageToBuffer(m_currentCmdBuf->m_cmdBuf, src->m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dest->m_buffer, 1, &region);

    retainResource(srcResource);
    retainResource(destResource);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

