// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "vulkan_backend.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// TrackedCommandBuffer
//-----------------------------------------------------------------------------

TrackedCommandBuffer::TrackedCommandBuffer(const VulkanContext& context, CommandQueue::Enum queueType, u32 queueFamilyIndex)
    : m_Context(context)
{
    // Create command pool for this buffer
    VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    vkCreateCommandPool(m_Context.device, &poolInfo, m_Context.allocationCallbacks, &cmdPool);
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    vkAllocateCommandBuffers(m_Context.device, &allocInfo, &cmdBuf);
}

TrackedCommandBuffer::~TrackedCommandBuffer(){
    if(cmdBuf){
        vkFreeCommandBuffers(m_Context.device, cmdPool, 1, &cmdBuf);
        cmdBuf = VK_NULL_HANDLE;
    }
    
    if(cmdPool){
        vkDestroyCommandPool(m_Context.device, cmdPool, m_Context.allocationCallbacks);
        cmdPool = VK_NULL_HANDLE;
    }
    
    referencedResources.clear();
    referencedStagingBuffers.clear();
}

//-----------------------------------------------------------------------------
// Queue - Command submission and synchronization
//-----------------------------------------------------------------------------

Queue::Queue(const VulkanContext& context, CommandQueue::Enum queueID, VkQueue queue, u32 queueFamilyIndex)
    : m_Context(context)
    , m_Queue(queue)
    , m_QueueID(queueID)
    , m_QueueFamilyIndex(queueFamilyIndex)
    , m_LastRecordingID(0)
    , m_LastSubmittedID(0)
    , m_LastFinishedID(0)
{
    // Create timeline semaphore for tracking
    VkSemaphoreTypeCreateInfo timelineInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;
    
    VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    semaphoreInfo.pNext = &timelineInfo;
    
    vkCreateSemaphore(m_Context.device, &semaphoreInfo, m_Context.allocationCallbacks, &trackingSemaphore);
}

Queue::~Queue(){
    // Wait for all work to complete
    if(trackingSemaphore && m_LastSubmittedID > 0){
        VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
        waitInfo.semaphoreCount = 1;
        waitInfo.pSemaphores = &trackingSemaphore;
        waitInfo.pValues = &m_LastSubmittedID;
        vkWaitSemaphores(m_Context.device, &waitInfo, UINT64_MAX);
    }
    
    // Free all command buffers
    m_CommandBuffersInFlight.clear();
    m_CommandBuffersPool.clear();
    
    // Destroy semaphore
    if(trackingSemaphore){
        vkDestroySemaphore(m_Context.device, trackingSemaphore, m_Context.allocationCallbacks);
        trackingSemaphore = VK_NULL_HANDLE;
    }
}

TrackedCommandBufferPtr Queue::createCommandBuffer(){
    TrackedCommandBuffer* cmdBuf = new TrackedCommandBuffer(m_Context, m_QueueID, m_QueueFamilyIndex);
    cmdBuf->recordingID = ++m_LastRecordingID;
    return TrackedCommandBufferPtr(cmdBuf);
}

TrackedCommandBufferPtr Queue::getOrCreateCommandBuffer(){
    Mutex::scoped_lock lock(m_Mutex);
    
    // Update completed IDs first
    updateLastFinishedID();
    
    // Retire finished command buffers to pool
    auto it = m_CommandBuffersInFlight.begin();
    while(it != m_CommandBuffersInFlight.end()){
        TrackedCommandBuffer* cmdBuf = it->get();
        if(cmdBuf->submissionID <= m_LastFinishedID){
            // Clear references
            cmdBuf->referencedResources.clear();
            cmdBuf->referencedStagingBuffers.clear();
            
            // Move to pool
            m_CommandBuffersPool.push_back(std::move(*it));
            it = m_CommandBuffersInFlight.erase(it);
        }
        else{
            ++it;
        }
    }
    
    // Try to reuse a command buffer from the pool
    if(!m_CommandBuffersPool.empty()){
        TrackedCommandBufferPtr cmdBuf = std::move(m_CommandBuffersPool.front());
        m_CommandBuffersPool.pop_front();
        
        // Reset the command buffer
        vkResetCommandBuffer(cmdBuf->cmdBuf, 0);
        cmdBuf->recordingID = ++m_LastRecordingID;
        
        return cmdBuf;
    }
    
    // Create a new command buffer
    return createCommandBuffer();
}

void Queue::addWaitSemaphore(VkSemaphore semaphore, u64 value){
    Mutex::scoped_lock lock(m_Mutex);
    m_WaitSemaphores.push_back(semaphore);
    m_WaitSemaphoreValues.push_back(value);
}

void Queue::addSignalSemaphore(VkSemaphore semaphore, u64 value){
    Mutex::scoped_lock lock(m_Mutex);
    m_SignalSemaphores.push_back(semaphore);
    m_SignalSemaphoreValues.push_back(value);
}

u64 Queue::submit(ICommandList* const* ppCmd, usize numCmd){
    if(!ppCmd || numCmd == 0)
        return m_LastSubmittedID;
    
    Mutex::scoped_lock lock(m_Mutex);
    
    // Increment submission ID
    u64 submissionID = ++m_LastSubmittedID;
    
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
        return m_LastSubmittedID - 1;
    
    // Setup timeline semaphore for tracking
    VkSemaphoreSubmitInfo timelineSignal = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
    timelineSignal.semaphore = trackingSemaphore;
    timelineSignal.value = submissionID;
    timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    
    // Collect wait semaphores
    Vector<VkSemaphoreSubmitInfo> waitInfos;
    Vector<VkPipelineStageFlags2> waitStages;
    for(usize i = 0; i < m_WaitSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        waitInfo.semaphore = m_WaitSemaphores[i];
        waitInfo.value = m_WaitSemaphoreValues[i];
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }
    
    // Collect signal semaphores
    Vector<VkSemaphoreSubmitInfo> signalInfos;
    signalInfos.push_back(timelineSignal); // Always signal our tracking semaphore
    
    for(usize i = 0; i < m_SignalSemaphores.size(); ++i){
        VkSemaphoreSubmitInfo signalInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
        signalInfo.semaphore = m_SignalSemaphores[i];
        signalInfo.value = m_SignalSemaphoreValues[i];
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
    
    // Submit to queue (using VK_KHR_synchronization2)
    VkSubmitInfo2 submitInfo = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
    submitInfo.waitSemaphoreInfoCount = (u32)waitInfos.size();
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();
    submitInfo.commandBufferInfoCount = (u32)cmdBufInfos.size();
    submitInfo.pCommandBufferInfos = cmdBufInfos.data();
    submitInfo.signalSemaphoreInfoCount = (u32)signalInfos.size();
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();
    
    VkResult res = vkQueueSubmit2(m_Queue, 1, &submitInfo, VK_NULL_HANDLE);
    
    // Clear wait/signal semaphores after submission
    m_WaitSemaphores.clear();
    m_WaitSemaphoreValues.clear();
    m_SignalSemaphores.clear();
    m_SignalSemaphoreValues.clear();
    
    // Move command buffers to in-flight list
    for(auto& tracked : trackedBuffers){
        m_CommandBuffersInFlight.push_back(std::move(tracked));
    }
    
    if(res != VK_SUCCESS){
        // Submission failed
        return m_LastSubmittedID - 1;
    }
    
    return submissionID;
}

void Queue::updateLastFinishedID(){
    // Query the timeline semaphore value
    u64 completedValue = 0;
    vkGetSemaphoreCounterValue(m_Context.device, trackingSemaphore, &completedValue);
    m_LastFinishedID = completedValue;
}

bool Queue::pollCommandList(u64 commandListID){
    Mutex::scoped_lock lock(m_Mutex);
    
    updateLastFinishedID();
    return commandListID <= m_LastFinishedID;
}

bool Queue::waitCommandList(u64 commandListID, u64 timeout){
    // Wait for this semaphore value
    VkSemaphoreWaitInfo waitInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &trackingSemaphore;
    waitInfo.pValues = &commandListID;
    
    VkResult res = vkWaitSemaphores(m_Context.device, &waitInfo, timeout);
    
    if(res == VK_SUCCESS){
        Mutex::scoped_lock lock(m_Mutex);
        if(commandListID > m_LastFinishedID)
            m_LastFinishedID = commandListID;
    }
    
    return res == VK_SUCCESS;
}

void Queue::waitForIdle(){
    Mutex::scoped_lock lock(m_Mutex);
    
    vkQueueWaitIdle(m_Queue);
    
    // All submissions are now finished
    m_LastFinishedID = m_LastSubmittedID;
    
    // Retire all command buffers to pool
    for(auto& tracked : m_CommandBuffersInFlight){
        tracked->referencedResources.clear();
        tracked->referencedStagingBuffers.clear();
        m_CommandBuffersPool.push_back(std::move(tracked));
    }
    m_CommandBuffersInFlight.clear();
}

//-----------------------------------------------------------------------------
// Device - Queue Semaphore Interface
//-----------------------------------------------------------------------------

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


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
