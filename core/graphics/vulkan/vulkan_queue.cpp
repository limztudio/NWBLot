// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedCommandBuffer::TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum, u32 queueFamilyIndex)
    : RefCounter<IResource>(context.threadPool)
    , m_referencedResources(Alloc::CustomAllocator<RefCountPtr<IResource, ArenaRefDeleter<IResource>>>(context.objectArena))
    , m_referencedStagingBuffers(Alloc::CustomAllocator<RefCountPtr<IBuffer, ArenaRefDeleter<IBuffer>>>(context.objectArena))
    , m_referencedAccelStructHandles(Alloc::CustomAllocator<VkAccelerationStructureKHR>(context.objectArena))
    , m_context(context)
{
    VkResult res = VK_SUCCESS;

    // Use TRANSIENT flag to hint that command buffers are short-lived
    auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    res = vkCreateCommandPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &m_cmdPool);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create command pool: {}"), ResultToString(res));
        m_cmdPool = VK_NULL_HANDLE;
        m_cmdBuf = VK_NULL_HANDLE;
        return;
    }

    auto allocInfo = VulkanDetail::MakeVkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    res = vkAllocateCommandBuffers(m_context.device, &allocInfo, &m_cmdBuf);
    if(res != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate command buffer: {}"), ResultToString(res));
        m_cmdBuf = VK_NULL_HANDLE;
        vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
        m_cmdPool = VK_NULL_HANDLE;
    }
}

TrackedCommandBuffer::~TrackedCommandBuffer(){
    if(m_cmdBuf){
        vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        m_cmdBuf = VK_NULL_HANDLE;
    }

    if(m_cmdPool){
        vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
        m_cmdPool = VK_NULL_HANDLE;
    }

    clearTrackedReferences();
}

void TrackedCommandBuffer::clearTrackedReferences(){
    for(const auto handle : m_referencedAccelStructHandles){
        if(handle != VK_NULL_HANDLE)
            vkDestroyAccelerationStructureKHR(m_context.device, handle, m_context.allocationCallbacks);
    }
    m_referencedAccelStructHandles.clear();
    m_referencedResources.clear();
    m_referencedStagingBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Queue::Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex)
    : m_context(context)
    , m_queue(queue)
    , m_queueID(queueID)
    , m_queueFamilyIndex(queueFamilyIndex)
    , m_waitSemaphores(Alloc::CustomAllocator<VkSemaphore>(context.objectArena))
    , m_waitSemaphoreValues(Alloc::CustomAllocator<u64>(context.objectArena))
    , m_signalSemaphores(Alloc::CustomAllocator<VkSemaphore>(context.objectArena))
    , m_signalSemaphoreValues(Alloc::CustomAllocator<u64>(context.objectArena))
    , m_lastRecordingID(0)
    , m_lastSubmittedID(0)
    , m_lastFinishedID(0)
    , m_commandBuffersInFlight(Alloc::CustomAllocator<TrackedCommandBufferPtr>(context.objectArena))
    , m_commandBuffersPool(Alloc::CustomAllocator<TrackedCommandBufferPtr>(context.objectArena))
{
    VkResult res = VK_SUCCESS;

    auto timelineInfo = VulkanDetail::MakeVkStruct<VkSemaphoreTypeCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO);
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;

    auto semaphoreInfo = VulkanDetail::MakeVkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &timelineInfo;

    res = vkCreateSemaphore(m_context.device, &semaphoreInfo, m_context.allocationCallbacks, &m_trackingSemaphore);
    if(res != VK_SUCCESS){
        m_trackingSemaphore = VK_NULL_HANDLE;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create queue timeline semaphore: {}"), ResultToString(res));
    }
}
Queue::~Queue(){
    VkResult res = VK_SUCCESS;

    if(m_trackingSemaphore && m_lastSubmittedID > 0){
        auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &m_trackingSemaphore;
        waitInfo.pValues = &m_lastSubmittedID;

        res = vkWaitSemaphores(m_context.device, &waitInfo, UINT64_MAX);
        static_cast<void>(res);
    }

    m_commandBuffersInFlight.clear();
    m_commandBuffersPool.clear();

    if(m_trackingSemaphore){
        vkDestroySemaphore(m_context.device, m_trackingSemaphore, m_context.allocationCallbacks);
        m_trackingSemaphore = VK_NULL_HANDLE;
    }
}

TrackedCommandBufferPtr Queue::createCommandBuffer(){
    auto* cmdBuf = NewArenaObject<TrackedCommandBuffer>(m_context.objectArena, m_context, m_queueID, m_queueFamilyIndex);
    if(!cmdBuf->m_cmdBuf){
        DestroyArenaObject(m_context.objectArena, cmdBuf);
        return nullptr;
    }

    cmdBuf->m_recordingID = ++m_lastRecordingID;
    return TrackedCommandBufferPtr(cmdBuf, TrackedCommandBufferPtr::deleter_type(&m_context.objectArena), AdoptRef);
}

TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer(){
    VkResult res = VK_SUCCESS;

    ScopedLock lock(m_mutex);

    updateLastFinishedID();

    auto it = m_commandBuffersInFlight.begin();
    while(it != m_commandBuffersInFlight.end()){
        TrackedCommandBuffer* cmdBuf = it->get();
        if(cmdBuf->m_submissionID <= m_lastFinishedID){
            cmdBuf->clearTrackedReferences();

            m_commandBuffersPool.push_back(Move(*it));
            it = m_commandBuffersInFlight.erase(it);
        }
        else
            ++it;
    }

    if(!m_commandBuffersPool.empty()){
        TrackedCommandBufferPtr cmdBuf = Move(m_commandBuffersPool.front());
        m_commandBuffersPool.pop_front();

        if(!cmdBuf || cmdBuf->m_cmdBuf == VK_NULL_HANDLE)
            return createCommandBuffer();

        res = vkResetCommandBuffer(cmdBuf->m_cmdBuf, 0);
        if(res != VK_SUCCESS){
            NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to reset command buffer, creating a new one: {}"), ResultToString(res));
            return createCommandBuffer();
        }

        cmdBuf->m_recordingID = ++m_lastRecordingID;

        return cmdBuf;
    }

    return createCommandBuffer();
}

void Queue::addWaitSemaphore(VkSemaphore semaphore, u64 value){
    if(!semaphore)
        return;

    ScopedLock lock(m_mutex);
    m_waitSemaphores.push_back(semaphore);
    m_waitSemaphoreValues.push_back(value);
}

void Queue::addSignalSemaphore(VkSemaphore semaphore, u64 value){
    if(!semaphore)
        return;

    ScopedLock lock(m_mutex);
    m_signalSemaphores.push_back(semaphore);
    m_signalSemaphoreValues.push_back(value);
}

u64 Queue::submit(ICommandList* const* ppCmd, usize numCmd, bool* outSubmitted){
    VkResult res = VK_SUCCESS;

    ScopedLock lock(m_mutex);
    if(outSubmitted)
        *outSubmitted = false;

    Alloc::ScratchArena<> scratchArena;

    bool hasCommands = ppCmd && numCmd > 0;
    bool hasPendingSemaphores = !m_waitSemaphores.empty() || !m_signalSemaphores.empty();
    auto clearPendingSemaphores = [this](){
        m_waitSemaphores.clear();
        m_waitSemaphoreValues.clear();
        m_signalSemaphores.clear();
        m_signalSemaphoreValues.clear();
    };

    if(hasCommands && numCmd > UINT32_MAX){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list count exceeds Vulkan limit"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if(
        m_waitSemaphores.size() > static_cast<usize>(Limit<u32>::s_Max)
        || m_signalSemaphores.size() >= static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queued semaphore count exceeds Vulkan limit"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if((hasCommands || hasPendingSemaphores) && m_lastSubmittedID == Limit<u64>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: queue submission ID exhausted"));
        clearPendingSemaphores();
        return m_lastSubmittedID;
    }
    if(hasCommands){
        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = checked_cast<CommandList*>(ppCmd[i]);
            if(cmdList && cmdList->m_currentCmdBuf && cmdList->m_desc.queueType != m_queueID){
                NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command lists: command list queue type does not match the execution queue"));
                clearPendingSemaphores();
                return m_lastSubmittedID;
            }
        }
    }

    Vector<TrackedCommandBufferPtr, Alloc::ScratchAllocator<TrackedCommandBufferPtr>> trackedBuffers{ Alloc::ScratchAllocator<TrackedCommandBufferPtr>(scratchArena) };
    Vector<VkCommandBufferSubmitInfo, Alloc::ScratchAllocator<VkCommandBufferSubmitInfo>> cmdBufInfos{ Alloc::ScratchAllocator<VkCommandBufferSubmitInfo>(scratchArena) };

    if(hasCommands){
        trackedBuffers.reserve(numCmd);
        cmdBufInfos.reserve(numCmd);

        for(usize i = 0; i < numCmd; ++i){
            auto* cmdList = checked_cast<CommandList*>(ppCmd[i]);
            if(!cmdList || !cmdList->m_currentCmdBuf)
                continue;

            auto cmdBufInfo = VulkanDetail::MakeVkStruct<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
            cmdBufInfo.commandBuffer = cmdList->m_currentCmdBuf->m_cmdBuf;
            cmdBufInfos.push_back(cmdBufInfo);

            cmdList->m_currentCmdBuf->m_submissionID = m_lastSubmittedID + 1;
            trackedBuffers.push_back(Move(cmdList->m_currentCmdBuf));
        }
    }

    if(cmdBufInfos.empty() && !hasPendingSemaphores)
        return m_lastSubmittedID;

    const auto clearTrackedSignalFence = [](TrackedCommandBuffer& tracked){
        if(tracked.m_signalFenceQuery)
            tracked.m_signalFenceQuery->m_started = false;
        tracked.m_signalFence = VK_NULL_HANDLE;
        tracked.m_signalFenceQuery = nullptr;
    };

    if(m_trackingSemaphore == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Queue submission skipped because timeline semaphore is unavailable."));
        clearPendingSemaphores();

        for(auto& tracked : trackedBuffers){
            if(!tracked)
                continue;
            clearTrackedSignalFence(*tracked);
            tracked->clearTrackedReferences();
            m_commandBuffersPool.push_back(Move(tracked));
        }
        return m_lastSubmittedID;
    }

    u64 submissionID = ++m_lastSubmittedID;

    auto timelineSignal = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    timelineSignal.semaphore = m_trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    Vector<VkSemaphoreSubmitInfo, Alloc::ScratchAllocator<VkSemaphoreSubmitInfo>> waitInfos{ Alloc::ScratchAllocator<VkSemaphoreSubmitInfo>(scratchArena) };
    waitInfos.reserve(m_waitSemaphores.size());
    for(usize i = 0; i < m_waitSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        waitInfo.semaphore = m_waitSemaphores[i];
        waitInfo.value = m_waitSemaphoreValues[i];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }

    Vector<VkSemaphoreSubmitInfo, Alloc::ScratchAllocator<VkSemaphoreSubmitInfo>> signalInfos{ Alloc::ScratchAllocator<VkSemaphoreSubmitInfo>(scratchArena) };
    signalInfos.reserve(1u + m_signalSemaphores.size());
    signalInfos.push_back(timelineSignal);

    for(usize i = 0; i < m_signalSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo signalInfo = VulkanDetail::MakeVkStruct<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
        signalInfo.semaphore = m_signalSemaphores[i];
        signalInfo.value = m_signalSemaphoreValues[i];
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    VkFence submitFence = VK_NULL_HANDLE;
    EventQuery* submitFenceQuery = nullptr;
    for(auto& tracked : trackedBuffers){
        if(tracked->m_signalFence != VK_NULL_HANDLE){
            if(submitFenceQuery)
                submitFenceQuery->m_started = false;
            submitFence = tracked->m_signalFence;
            submitFenceQuery = tracked->m_signalFenceQuery;
            tracked->m_signalFence = VK_NULL_HANDLE;
            tracked->m_signalFenceQuery = nullptr;
        }
    }

    // Requires VK_KHR_synchronization2 extension to be enabled
    auto submitInfo = VulkanDetail::MakeVkStruct<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdBufInfos.size());
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    res = vkQueueSubmit2(m_queue, 1, &submitInfo, submitFence);

    clearPendingSemaphores();

    if(res != VK_SUCCESS){
        m_lastSubmittedID = submissionID - 1;
        if(submitFenceQuery)
            submitFenceQuery->m_started = false;

        if(res == VK_ERROR_DEVICE_LOST){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Device was lost during queue submission."));
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to submit command buffers to queue: {}"), ResultToString(res));
        }

        for(auto& tracked : trackedBuffers){
            if(!tracked)
                continue;
            // Submission failed; command lists were not executed.
            tracked->clearTrackedReferences();
            m_commandBuffersPool.push_back(Move(tracked));
        }

        return m_lastSubmittedID;
    }

    if(submitFenceQuery)
        submitFenceQuery->m_started = true;

    for(auto& tracked : trackedBuffers){
        m_commandBuffersInFlight.push_back(Move(tracked));
    }
    if(outSubmitted)
        *outSubmitted = true;

    return submissionID;
}

void Queue::updateLastFinishedID(){
    VkResult res = VK_SUCCESS;

    if(!m_trackingSemaphore){
        m_lastFinishedID = m_lastSubmittedID;
        return;
    }

    u64 completedValue = 0;
    res = vkGetSemaphoreCounterValue(m_context.device, m_trackingSemaphore, &completedValue);
    if(res == VK_SUCCESS)
        m_lastFinishedID = completedValue;
    else
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Failed to query queue timeline semaphore value: {}"), ResultToString(res));
}

bool Queue::pollCommandList(u64 commandListID){
    ScopedLock lock(m_mutex);

    updateLastFinishedID();
    return commandListID <= m_lastFinishedID;
}

bool Queue::waitCommandList(u64 commandListID, u64 timeout){
    VkResult res = VK_SUCCESS;

    if(commandListID == 0)
        return true;

    if(!m_trackingSemaphore)
        return false;

    auto waitInfo = VulkanDetail::MakeVkStruct<VkSemaphoreWaitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO);
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_trackingSemaphore;
    waitInfo.pValues = &commandListID;

    res = vkWaitSemaphores(m_context.device, &waitInfo, timeout);
    if(res == VK_ERROR_DEVICE_LOST){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Device was lost while waiting for command list."));
        return false;
    }
    else if(res == VK_SUCCESS){
        ScopedLock lock(m_mutex);
        if(commandListID > m_lastFinishedID)
            m_lastFinishedID = commandListID;
        return true;
    }

    return false;
}

void Queue::waitForIdle(){
    VkResult res = VK_SUCCESS;

    ScopedLock lock(m_mutex);

    res = vkQueueWaitIdle(m_queue);
    if(res != VK_SUCCESS)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Queue wait-for-idle failed: {}"), ResultToString(res));
    if(res == VK_SUCCESS){
        m_lastFinishedID = m_lastSubmittedID;

        for(auto& tracked : m_commandBuffersInFlight){
            tracked->clearTrackedReferences();
            m_commandBuffersPool.push_back(Move(tracked));
        }
        m_commandBuffersInFlight.clear();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


VkSemaphore Device::getQueueSemaphore(CommandQueue::Enum queue){
    Queue* q = getQueue(queue);
    return q ? q->m_trackingSemaphore : VK_NULL_HANDLE;
}

void Device::queueWaitForSemaphore(CommandQueue::Enum waitQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(waitQueue);
    if(q)
        q->addWaitSemaphore(semaphore, value);
}

void Device::queueSignalSemaphore(CommandQueue::Enum executionQueue, VkSemaphore semaphore, u64 value){
    Queue* q = getQueue(executionQueue);
    if(q)
        q->addSignalSemaphore(semaphore, value);
}

u64 Device::queueGetCompletedInstance(CommandQueue::Enum queue){
    Queue* q = getQueue(queue);
    if(q){
        ScopedLock lock(q->m_mutex);
        q->updateLastFinishedID();
        return q->m_lastFinishedID;
    }
    return 0;
}

void Device::queueWaitForCommandList(CommandQueue::Enum waitQueue, CommandQueue::Enum executionQueue, u64 instance){
    Queue* wait = getQueue(waitQueue);
    Queue* exec = getQueue(executionQueue);

    if(wait && exec)
        wait->addWaitSemaphore(exec->m_trackingSemaphore, instance);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Queue::updateTextureTileMappings(ITexture* textureResource, const TextureTilesMapping* tileMappings, u32 numTileMappings){
    VkResult res = VK_SUCCESS;

    if(!textureResource || !tileMappings || numTileMappings == 0)
        return;

    auto* texture = checked_cast<Texture*>(textureResource);
    Alloc::ThreadPool& workerPool = m_context.threadPool;
    const bool useParallelPool = workerPool.isParallelEnabled();
    const usize mappingCount = static_cast<usize>(numTileMappings);

    Alloc::ScratchArena<> scratchArena(8192);

    Vector<VkSparseImageMemoryBind, Alloc::ScratchAllocator<VkSparseImageMemoryBind>> sparseImageMemoryBinds{ Alloc::ScratchAllocator<VkSparseImageMemoryBind>(scratchArena) };
    Vector<VkSparseMemoryBind, Alloc::ScratchAllocator<VkSparseMemoryBind>> sparseMemoryBinds{ Alloc::ScratchAllocator<VkSparseMemoryBind>(scratchArena) };

    const VkImageCreateInfo& imageInfo = texture->m_imageInfo;
    const VkImageAspectFlags textureAspectFlags = texture->m_aspectMask;

    u32 tileWidth = 1;
    u32 tileHeight = 1;
    u32 tileDepth = 1;

    VkDeviceSize imageMipTailOffset = 0;
    VkDeviceSize imageMipTailStride = 1;

    uint32_t formatPropCount = 0;
    vkGetPhysicalDeviceSparseImageFormatProperties(
        m_context.physicalDevice,
        imageInfo.format, imageInfo.imageType, imageInfo.samples,
        imageInfo.usage, imageInfo.tiling,
        &formatPropCount, nullptr
    );

    Vector<VkSparseImageFormatProperties, Alloc::ScratchAllocator<VkSparseImageFormatProperties>> formatProps(formatPropCount, Alloc::ScratchAllocator<VkSparseImageFormatProperties>(scratchArena));
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

    SparseImageMemoryRequirementsVector sparseReqs{ Alloc::ScratchAllocator<VkSparseImageMemoryRequirements>(scratchArena) };
    VulkanDetail::GetImageSparseMemoryRequirements(m_context.device, texture->m_image, sparseReqs);

    if(!sparseReqs.empty()){
        imageMipTailOffset = sparseReqs[0].imageMipTailOffset;
        imageMipTailStride = sparseReqs[0].imageMipTailStride;
    }

    struct MappingBindCounts{
        usize opaqueCount = 0;
        usize imageCount = 0;
        usize opaqueBase = 0;
        usize imageBase = 0;
    };
    Vector<MappingBindCounts, Alloc::ScratchAllocator<MappingBindCounts>> mappingCounts(mappingCount, Alloc::ScratchAllocator<MappingBindCounts>(scratchArena));

    auto countMappingBinds = [&](usize i){
        MappingBindCounts& counts = mappingCounts[i];
        counts.opaqueCount = 0;
        counts.imageCount = 0;

        const TextureTilesMapping& mapping = tileMappings[i];
        if(mapping.numTextureRegions == 0 || !mapping.tiledTextureRegions || !mapping.tiledTextureCoordinates)
            return;
        if(mapping.heap && !mapping.byteOffsets)
            return;

        for(u32 j = 0; j < mapping.numTextureRegions; ++j){
            const TiledTextureRegion& region = mapping.tiledTextureRegions[j];
            if(region.tilesNum)
                ++counts.opaqueCount;
            else
                ++counts.imageCount;
        }
    };

    if(useParallelPool && mappingCount >= s_ParallelTileCountThreshold)
        workerPool.parallelFor(static_cast<usize>(0), mappingCount, s_TileCountGrainSize, countMappingBinds);
    else{
        for(usize i = 0; i < mappingCount; ++i)
            countMappingBinds(i);
    }

    usize totalOpaqueBinds = 0;
    usize totalImageBinds = 0;
    for(usize i = 0; i < mappingCount; ++i){
        MappingBindCounts& counts = mappingCounts[i];
        counts.opaqueBase = totalOpaqueBinds;
        counts.imageBase = totalImageBinds;
        if(counts.opaqueCount > Limit<usize>::s_Max - totalOpaqueBinds){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: opaque sparse bind count overflows"));
            return;
        }
        if(counts.imageCount > Limit<usize>::s_Max - totalImageBinds){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: image sparse bind count overflows"));
            return;
        }
        totalOpaqueBinds += counts.opaqueCount;
        totalImageBinds += counts.imageCount;
    }

    if(totalOpaqueBinds > static_cast<usize>(Limit<u32>::s_Max) || totalImageBinds > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: sparse bind count exceeds Vulkan limit"));
        return;
    }
    if(totalOpaqueBinds > Limit<usize>::s_Max - totalImageBinds){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: total sparse bind count overflows"));
        return;
    }
    const usize totalBindings = totalOpaqueBinds + totalImageBinds;

    sparseMemoryBinds.resize(totalOpaqueBinds);
    sparseImageMemoryBinds.resize(totalImageBinds);

    auto buildMappingBinds = [&](usize i){
        const TextureTilesMapping& mapping = tileMappings[i];
        const MappingBindCounts& counts = mappingCounts[i];
        if(mapping.numTextureRegions == 0 || !mapping.tiledTextureRegions || !mapping.tiledTextureCoordinates)
            return;
        if(mapping.heap && !mapping.byteOffsets)
            return;

        Heap* heap = mapping.heap ? checked_cast<Heap*>(mapping.heap) : nullptr;
        VkDeviceMemory deviceMemory = heap ? heap->m_memory : VK_NULL_HANDLE;
        const VkDeviceSize heapMemoryOffset = heap ? heap->m_memoryOffset : 0;

        usize opaqueWrite = counts.opaqueBase;
        usize imageWrite = counts.imageBase;

        for(u32 j = 0; j < mapping.numTextureRegions; ++j){
            const TiledTextureCoordinate& coord = mapping.tiledTextureCoordinates[j];
            const TiledTextureRegion& region = mapping.tiledTextureRegions[j];

            if(region.tilesNum){
                VkSparseMemoryBind bind = {};
                bind.resourceOffset = imageMipTailOffset + coord.arrayLevel * imageMipTailStride;
                bind.size = region.tilesNum * texture->m_tileByteSize;
                bind.memory = deviceMemory;
                bind.memoryOffset = deviceMemory ? heapMemoryOffset + mapping.byteOffsets[j] : 0;
                sparseMemoryBinds[opaqueWrite] = bind;
                ++opaqueWrite;
            }
            else{
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
                bind.memoryOffset = deviceMemory ? heapMemoryOffset + mapping.byteOffsets[j] : 0;
                sparseImageMemoryBinds[imageWrite] = bind;
                ++imageWrite;
            }
        }
    };

    if(useParallelPool && totalBindings >= s_ParallelTileMappingThreshold)
        workerPool.parallelFor(static_cast<usize>(0), mappingCount, s_TileMappingGrainSize, buildMappingBinds);
    else{
        for(usize i = 0; i < mappingCount; ++i)
            buildMappingBinds(i);
    }

    auto bindSparseInfo = VulkanDetail::MakeVkStruct<VkBindSparseInfo>(VK_STRUCTURE_TYPE_BIND_SPARSE_INFO);

    VkSparseImageMemoryBindInfo sparseImageMemoryBindInfo = {};
    if(!sparseImageMemoryBinds.empty()){
        sparseImageMemoryBindInfo.image = texture->m_image;
        sparseImageMemoryBindInfo.bindCount = static_cast<u32>(sparseImageMemoryBinds.size());
        sparseImageMemoryBindInfo.pBinds = sparseImageMemoryBinds.data();
        bindSparseInfo.imageBindCount = 1;
        bindSparseInfo.pImageBinds = &sparseImageMemoryBindInfo;
    }

    VkSparseImageOpaqueMemoryBindInfo sparseOpaqueBindInfo = {};
    if(!sparseMemoryBinds.empty()){
        sparseOpaqueBindInfo.image = texture->m_image;
        sparseOpaqueBindInfo.bindCount = static_cast<u32>(sparseMemoryBinds.size());
        sparseOpaqueBindInfo.pBinds = sparseMemoryBinds.data();
        bindSparseInfo.imageOpaqueBindCount = 1;
        bindSparseInfo.pImageOpaqueBinds = &sparseOpaqueBindInfo;
    }

    {
        ScopedLock lock(m_mutex);
        res = vkQueueBindSparse(m_queue, 1, &bindSparseInfo, VK_NULL_HANDLE);
    }
    if(res != VK_SUCCESS)
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to update texture tile mappings: {}"), ResultToString(res));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

