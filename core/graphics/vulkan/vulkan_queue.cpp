// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedCommandBuffer::TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum queueType, u32 queueFamilyIndex)
    : m_context(context)
{
    // Create command pool for this buffer
    // Use TRANSIENT flag to hint that command buffers are short-lived
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkResult res = vkCreateCommandPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &cmdPool);
    if(res != VK_SUCCESS){
        cmdPool = VK_NULL_HANDLE;
        cmdBuf = VK_NULL_HANDLE;
        return;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    res = vkAllocateCommandBuffers(m_context.device, &allocInfo, &cmdBuf);
    if(res != VK_SUCCESS){
        cmdBuf = VK_NULL_HANDLE;
    }
}

TrackedCommandBuffer::~TrackedCommandBuffer(){
    if(cmdBuf){
        vkFreeCommandBuffers(m_context.device, cmdPool, 1, &cmdBuf);
        cmdBuf = VK_NULL_HANDLE;
    }
    
    if(cmdPool){
        vkDestroyCommandPool(m_context.device, cmdPool, m_context.allocationCallbacks);
        cmdPool = VK_NULL_HANDLE;
    }
    
    referencedResources.clear();
    referencedStagingBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Queue::Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex)
    : m_context(context)
    , m_queue(queue)
    , m_queueID(queueID)
    , m_queueFamilyIndex(queueFamilyIndex)
    , m_lastRecordingID(0)
    , m_lastSubmittedID(0)
    , m_lastFinishedID(0)
{
    // Create timeline semaphore for tracking
    VkSemaphoreTypeCreateInfo timelineInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;
    
    VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphoreInfo.pNext = &timelineInfo;
    
    vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &trackingSemaphore);
}

Queue::~Queue(){
    // Wait for all work to complete
    if(trackingSemaphore && m_lastSubmittedID > 0){
        VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &trackingSemaphore;
        waitInfo.pValues = &m_lastSubmittedID;
        VkResult res = vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        // Ignore errors in destructor - device may already be lost
        (void)res;
    }

    // Free all command buffers
    m_commandBuffersInFlight.clear();
    m_commandBuffersPool.clear();

    // Destroy semaphore
    if(trackingSemaphore){
        vkDestroySemaphore(m_context.device, trackingSemaphore, m_context.allocationCallbacks);
        trackingSemaphore = VK_NULL_HANDLE;
    }
}

TrackedCommandBufferPtr Queue::createCommandBuffer(){
    TrackedCommandBuffer* cmdBuf = new TrackedCommandBuffer(m_context, m_queueID, m_queueFamilyIndex);
    cmdBuf->recordingID = ++m_lastRecordingID;
    return TrackedCommandBufferPtr(cmdBuf);
}

TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer(){
    Mutex::scoped_lock lock(m_mutex);
    
    // Update completed IDs first
    updateLastFinishedID();
    
    // Retire finished command buffers to pool
    auto it = m_commandBuffersInFlight.begin();
    while(it != m_commandBuffersInFlight.end()){
        TrackedCommandBuffer* cmdBuf = it->get();
        if(cmdBuf->submissionID <= m_lastFinishedID){
            // Clear references
            cmdBuf->referencedResources.clear();
            cmdBuf->referencedStagingBuffers.clear();
            
            // Move to pool
            m_commandBuffersPool.push_back(std::move(*it));
            it = m_commandBuffersInFlight.erase(it);
        }
        else{
            ++it;
        }
    }
    
    // Try to reuse a command buffer from the pool
    if(!m_commandBuffersPool.empty()){
        TrackedCommandBufferPtr cmdBuf = std::move(m_commandBuffersPool.front());
        m_commandBuffersPool.pop_front();
        
        // Reset the command buffer
        vkResetCommandBuffer(cmdBuf->cmdBuf, 0);
        cmdBuf->recordingID = ++m_lastRecordingID;
        
        return cmdBuf;
    }
    
    // Create a new command buffer
    return createCommandBuffer();
}

void Queue::addWaitSemaphore(VkSemaphore semaphore, u64 value){
    Mutex::scoped_lock lock(m_mutex);
    m_waitSemaphores.push_back(semaphore);
    m_waitSemaphoreValues.push_back(value);
}

void Queue::addSignalSemaphore(VkSemaphore semaphore, u64 value){
    Mutex::scoped_lock lock(m_mutex);
    m_signalSemaphores.push_back(semaphore);
    m_signalSemaphoreValues.push_back(value);
}

u64 Queue::submit(ICommandList* const* ppCmd, usize numCmd){
    if(!ppCmd || numCmd == 0)
        return m_lastSubmittedID;
    
    Mutex::scoped_lock lock(m_mutex);
    
    // Increment submission ID
    u64 submissionID = ++m_lastSubmittedID;
    
    // Collect command buffers from command lists
    Vector<TrackedCommandBufferPtr> trackedBuffers;
    Vector<VkCommandBuffer> cmdBufs;
    trackedBuffers.reserve(numCmd);
    cmdBufs.reserve(numCmd);
    
    for(usize i = 0; i < numCmd; ++i){
        CommandList* cmdList = checked_cast<CommandList*>(ppCmd[i]);
        if(!cmdList || !cmdList->currentCmdBuf)
            continue;
        
        cmdBufs.push_back(cmdList->currentCmdBuf->cmdBuf);
        
        // Track this command buffer
        cmdList->currentCmdBuf->submissionID = submissionID;
        trackedBuffers.push_back(std::move(cmdList->currentCmdBuf));
    }
    
    if(cmdBufs.empty())
        return m_lastSubmittedID - 1;
    
    // Setup timeline semaphore for tracking
    VkSemaphoreSubmitInfo timelineSignal = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    timelineSignal.semaphore = trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    
    // Collect wait semaphores
    Vector<VkSemaphoreSubmitInfo> waitInfos;
    Vector<VkPipelineStageFlags2> waitStages;
    for(usize i = 0; i < m_waitSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        waitInfo.semaphore = m_waitSemaphores[i];
        waitInfo.value = m_waitSemaphoreValues[i];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }
    
    // Collect signal semaphores
    Vector<VkSemaphoreSubmitInfo> signalInfos;
    signalInfos.push_back(timelineSignal); // Always signal our tracking semaphore
    
    for(usize i = 0; i < m_signalSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo signalInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        signalInfo.semaphore = m_signalSemaphores[i];
        signalInfo.value = m_signalSemaphoreValues[i];
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }
    
    // Setup command buffer submit info
    Vector<VkCommandBufferSubmitInfo> cmdBufInfos;
    cmdBufInfos.reserve(numCmd);
    
    for(VkCommandBuffer cmdBuf : cmdBufs){
        VkCommandBufferSubmitInfo cmdBufInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
        cmdBufInfo.commandBuffer = cmdBuf;
        cmdBufInfos.push_back(cmdBufInfo);
    }
    
    // Collect the signal fence from tracked command buffers (if any)
    VkFence submitFence = VK_NULL_HANDLE;
    for(auto& tracked : trackedBuffers){
        if(tracked->signalFence != VK_NULL_HANDLE){
            submitFence = tracked->signalFence;
            tracked->signalFence = VK_NULL_HANDLE;
        }
    }
    
    // Submit to queue (using VK_KHR_synchronization2)
    // NOTE: Requires VK_KHR_synchronization2 extension to be enabled
    VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submitInfo.waitSemaphoreInfoCount = (u32)waitInfos.size();
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = (u32)cmdBufInfos.size();
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = (u32)signalInfos.size();
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    VkResult res = vkQueueSubmit2(m_queue, 1, &submitInfo, submitFence);

    // Clear wait/signal semaphores after submission
    m_waitSemaphores.clear();
    m_waitSemaphoreValues.clear();
    m_signalSemaphores.clear();
    m_signalSemaphoreValues.clear();

    if(res != VK_SUCCESS){
        // Submission failed - check for device loss
        if(res == VK_ERROR_DEVICE_LOST){
            // Device lost - critical error
        }
        // Don't move command buffers to in-flight list on failure
        return m_lastSubmittedID - 1;
    }

    // Move command buffers to in-flight list only on successful submission
    for(auto& tracked : trackedBuffers){
        m_commandBuffersInFlight.push_back(std::move(tracked));
    }

    return submissionID;
}

void Queue::updateLastFinishedID(){
    // Query the timeline semaphore value
    u64 completedValue = 0;
    vkGetSemaphoreCounterValue(m_context.device, trackingSemaphore, &completedValue);
    m_lastFinishedID = completedValue;
}

bool Queue::pollCommandList(u64 commandListID){
    Mutex::scoped_lock lock(m_mutex);
    
    updateLastFinishedID();
    return commandListID <= m_lastFinishedID;
}

bool Queue::waitCommandList(u64 commandListID, u64 timeout){
    // Wait for this semaphore value
    VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &trackingSemaphore;
    waitInfo.pValues = &commandListID;

    VkResult res = vkWaitSemaphores(m_context.device, &waitInfo, timeout);

    // Handle device loss
    if(res == VK_ERROR_DEVICE_LOST){
        return false;
    }

    if(res == VK_SUCCESS){
        Mutex::scoped_lock lock(m_mutex);
        if(commandListID > m_lastFinishedID)
            m_lastFinishedID = commandListID;
        return true;
    }

    // Timeout or other error
    return false;
}

void Queue::waitForIdle(){
    Mutex::scoped_lock lock(m_mutex);
    
    vkQueueWaitIdle(m_queue);
    
    // All submissions are now finished
    m_lastFinishedID = m_lastSubmittedID;
    
    // Retire all command buffers to pool
    for(auto& tracked : m_commandBuffersInFlight){
        tracked->referencedResources.clear();
        tracked->referencedStagingBuffers.clear();
        m_commandBuffersPool.push_back(std::move(tracked));
    }
    m_commandBuffersInFlight.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkSemaphore Device::getQueueSemaphore(CommandQueue::Enum queue){
    Queue* q = getQueue(queue);
    return q ? q->trackingSemaphore : VK_NULL_HANDLE;
}

void Device::queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(waitQueue);
    if(q){
        q->addWaitSemaphore(semaphore, value);
    }
}

void Device::queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(executionQueue);
    if(q){
        q->addSignalSemaphore(semaphore, value);
    }
}

u64 Device::queueGetCompletedInstance(CommandQueue::Enum queue){
    Queue* q = getQueue(queue);
    if(q){
        q->updateLastFinishedID();
        return q->getLastFinishedID();
    }
    return 0;
}

void Device::queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance){
    Queue* wait = getQueue(waitQueue);
    Queue* exec = getQueue(executionQueue);
    
    if(wait && exec){
        wait->addWaitSemaphore(exec->trackingSemaphore, instance);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Queue::updateTextureTileMappings(ITexture* _texture, const TextureTilesMapping* tileMappings, u32 numTileMappings){
    Texture* texture = checked_cast<Texture*>(_texture);
    
    Vector<VkSparseImageMemoryBind> sparseImageMemoryBinds;
    Vector<VkSparseMemoryBind> sparseMemoryBinds;
    
    const VkImageCreateInfo& imageInfo = texture->imageInfo;
    VkImageAspectFlags textureAspectFlags = VK_IMAGE_ASPECT_COLOR_BIT;
    
    // Determine aspect from format
    VkFormat fmt = imageInfo.format;
    if(fmt == VK_FORMAT_D32_SFLOAT || fmt == VK_FORMAT_D16_UNORM)
        textureAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT;
    else if(fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        textureAspectFlags = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    
    u32 tileWidth = 1;
    u32 tileHeight = 1;
    u32 tileDepth = 1;
    
    VkDeviceSize imageMipTailOffset = 0;
    VkDeviceSize imageMipTailStride = 1;
    
    // Get sparse format properties
    uint32_t formatPropCount = 0;
    vkGetPhysicalDeviceSparseImageFormatProperties(
        m_context.physicalDevice,
        imageInfo.format, imageInfo.imageType, imageInfo.samples,
        imageInfo.usage, imageInfo.tiling,
        &formatPropCount, nullptr
        );
    
    Vector<VkSparseImageFormatProperties> formatProps(formatPropCount);
    if(formatPropCount > 0)
        vkGetPhysicalDeviceSparseImageFormatProperties(
            m_context.physicalDevice,
            imageInfo.format, imageInfo.imageType, imageInfo.samples,
            imageInfo.usage, imageInfo.tiling,
            &formatPropCount, formatProps.data()
            );
    
    if(!formatProps.empty()){
        tileWidth = formatProps[0].imageGranularity.width;
        tileHeight = formatProps[0].imageGranularity.height;
        tileDepth = formatProps[0].imageGranularity.depth;
    }
    
    // Get sparse memory requirements
    uint32_t sparseReqCount = 0;
    vkGetImageSparseMemoryRequirements(m_context.device, texture->image, &sparseReqCount, nullptr);
    
    Vector<VkSparseImageMemoryRequirements> sparseReqs(sparseReqCount);
    if(sparseReqCount > 0)
        vkGetImageSparseMemoryRequirements(m_context.device, texture->image, &sparseReqCount, sparseReqs.data());
    
    if(!sparseReqs.empty()){
        imageMipTailOffset = sparseReqs[0].imageMipTailOffset;
        imageMipTailStride = sparseReqs[0].imageMipTailStride;
    }
    
    for(u32 i = 0; i < numTileMappings; i++){
        u32 numRegions = tileMappings[i].numTextureRegions;
        Heap* heap = tileMappings[i].heap ? checked_cast<Heap*>(tileMappings[i].heap) : nullptr;
        VkDeviceMemory deviceMemory = heap ? heap->memory : VK_NULL_HANDLE;
        
        for(u32 j = 0; j < numRegions; ++j){
            const TiledTextureCoordinate& coord = tileMappings[i].tiledTextureCoordinates[j];
            const TiledTextureRegion& region = tileMappings[i].tiledTextureRegions[j];
            
            if(region.tilesNum){
                // Packed mip tail binding (opaque bind)
                VkSparseMemoryBind bind = {};
                bind.resourceOffset = imageMipTailOffset + coord.arrayLevel * imageMipTailStride;
                bind.size = region.tilesNum * texture->tileByteSize;
                bind.memory = deviceMemory;
                bind.memoryOffset = deviceMemory ? tileMappings[i].byteOffsets[j] : 0;
                sparseMemoryBinds.push_back(bind);
            }
            else{
                // Standard mip binding
                VkSparseImageMemoryBind bind = {};
                bind.subresource.arrayLayer = coord.arrayLevel;
                bind.subresource.mipLevel = coord.mipLevel;
                bind.subresource.aspectMask = textureAspectFlags;
                bind.offset.x = coord.x * tileWidth;
                bind.offset.y = coord.y * tileHeight;
                bind.offset.z = coord.z * tileDepth;
                bind.extent.width = region.width * tileWidth;
                bind.extent.height = region.height * tileHeight;
                bind.extent.depth = region.depth * tileDepth;
                bind.memory = deviceMemory;
                bind.memoryOffset = deviceMemory ? tileMappings[i].byteOffsets[j] : 0;
                sparseImageMemoryBinds.push_back(bind);
            }
        }
    }
    
    VkBindSparseInfo bindSparseInfo = { VK_STRUCTURE_TYPE_BIND_SPARSE_INFO };
    
    VkSparseImageMemoryBindInfo sparseImageMemoryBindInfo = {};
    if(!sparseImageMemoryBinds.empty()){
        sparseImageMemoryBindInfo.image = texture->image;
        sparseImageMemoryBindInfo.bindCount = static_cast<u32>(sparseImageMemoryBinds.size());
        sparseImageMemoryBindInfo.pBinds = sparseImageMemoryBinds.data();
        bindSparseInfo.imageBindCount = 1;
        bindSparseInfo.pImageBinds = &sparseImageMemoryBindInfo;
    }
    
    VkSparseImageOpaqueMemoryBindInfo sparseOpaqueBindInfo = {};
    if(!sparseMemoryBinds.empty()){
        sparseOpaqueBindInfo.image = texture->image;
        sparseOpaqueBindInfo.bindCount = static_cast<u32>(sparseMemoryBinds.size());
        sparseOpaqueBindInfo.pBinds = sparseMemoryBinds.data();
        bindSparseInfo.imageOpaqueBindCount = 1;
        bindSparseInfo.pImageOpaqueBinds = &sparseOpaqueBindInfo;
    }
    
    vkQueueBindSparse(m_queue, 1, &bindSparseInfo, VK_NULL_HANDLE);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

