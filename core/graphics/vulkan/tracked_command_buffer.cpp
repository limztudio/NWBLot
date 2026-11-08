// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


TrackedCommandBuffer::~TrackedCommandBuffer()noexcept{
    abandonPendingAccelStructBuildCommits();

    if(m_cmdBuf && m_cmdPool){
        if(m_sharedCommandPoolMutex){
            ScopedLock lock(*m_sharedCommandPoolMutex);

            m_context.deviceDispatch.vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        }
        else
            m_context.deviceDispatch.vkFreeCommandBuffers(m_context.device, m_cmdPool, 1, &m_cmdBuf);
        m_cmdBuf = VK_NULL_HANDLE;
    }

    if(m_ownsCmdPool && m_cmdPool){
        m_context.deviceDispatch.vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
    }
    m_cmdPool = VK_NULL_HANDLE;
    m_ownsCmdPool = false;

    clearTrackedReferences();
    m_queue.unregisterCommandBuffer(*this);
}

Expected<void, VkResult> TrackedCommandBuffer::initialize(const u32 queueFamilyIndex){
    if(m_ownsCmdPool){
        auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        const VkResult createResult = m_context.deviceDispatch.vkCreateCommandPool(m_context.device, &poolInfo, m_context.allocationCallbacks, &m_cmdPool);
        if(createResult != VK_SUCCESS){
            m_cmdPool = VK_NULL_HANDLE;
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create private command pool: {}"), ResultToString(createResult));
            return MakeUnexpected(createResult);
        }
    }
    else if(m_cmdPool == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot allocate a command buffer without a worker command pool."));
        return MakeUnexpected(VK_ERROR_INITIALIZATION_FAILED);
    }

    auto allocInfo = VulkanDetail::MakeVkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    const VkResult allocateResult = m_context.deviceDispatch.vkAllocateCommandBuffers(m_context.device, &allocInfo, &m_cmdBuf);
    if(allocateResult != VK_SUCCESS){
        m_cmdBuf = VK_NULL_HANDLE;
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate command buffer: {}"), ResultToString(allocateResult));
        if(m_ownsCmdPool)
            m_context.deviceDispatch.vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
        m_cmdPool = VK_NULL_HANDLE;
        m_ownsCmdPool = false;
        return MakeUnexpected(allocateResult);
    }
    return {};
}

TrackedCommandBuffer::TimerQueryRecordingClaim& TrackedCommandBuffer::findOrAppendTimerQueryRecordingClaim(TimerQuery& query){
    return m_timerQueryClaims.findOrAppendTimerQueryRecordingClaim(query);
}

TrackedCommandBuffer::TimerQueryRecordingClaim* TrackedCommandBuffer::findTimerQueryRecordingClaim(TimerQuery& query, const u64 generation)noexcept{
    return m_timerQueryClaims.findTimerQueryRecordingClaim(query, generation);
}

b8 TrackedCommandBuffer::validateTimerQueryRecordingClaims(const Queue& submissionQueue, const Span<const QueueSubmissionWait> localWaits, const Span<const NotNull<TrackedCommandBuffer*>> precedingCommandBuffers)const noexcept{
    return m_timerQueryClaims.validateTimerQueryRecordingClaims(submissionQueue, localWaits, precedingCommandBuffers);
}

void TrackedCommandBuffer::commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept{
    m_timerQueryClaims.commitTimerQueryRecordingClaims(submissionToken);
}

void TrackedCommandBuffer::discardTimerQueryRecordingClaims()noexcept{
    m_timerQueryClaims.discardTimerQueryRecordingClaims();
}

void TrackedCommandBuffer::commitRetainedBufferStateCommits()noexcept{
    static_assert(noexcept(m_resourceReferences.m_bufferStateCommits.clear()), "accepted buffer-state commit release must be non-throwing");
    for(const RetainedBufferStateCommit& commit : m_resourceReferences.m_bufferStateCommits){
        commit.buffer->setRetainedStateKnown(true);
    }
    m_resourceReferences.discardBufferStateCommits();
}

void TrackedCommandBuffer::appendRetainedTextureStateCommit(Texture& texture, const MipLevel mipLevel, const ArraySlice arraySlice){
    // The closing barrier and its deferred state publication outlive CommandList::clearState().
    // Keep the texture alive with the command buffer until Queue::submit accepts or discards the command buffer.
    m_resourceReferences.retainTexture(texture);

    m_retainedTextureStateCommits.push_back(RetainedTextureStateCommit{ .texture = MakeNotNull(&texture), .mipLevel = mipLevel, .arraySlice = arraySlice, });
}

void TrackedCommandBuffer::commitRetainedTextureStateCommits()noexcept{
    static_assert(noexcept(m_retainedTextureStateCommits.clear()), "accepted texture-state commit release must be non-throwing");
    for(const RetainedTextureStateCommit& commit : m_retainedTextureStateCommits){
        commit.texture->setRetainedSubresourceStateKnown(commit.arraySlice, commit.mipLevel, true);
    }
    m_retainedTextureStateCommits.clear();
}

b8 TrackedCommandBuffer::appendPendingAccelStructBuildCommit(AccelStruct& accelStruct, const VkAccelerationStructureTypeKHR accelStructType, const VkBuildAccelerationStructureFlagsKHR buildFlags, const Span<const AccelStructGeometryBuildSignature> geometrySignatures, const Span<const RayTracingInstanceDesc> instances){
    return m_accelStructJournal.appendPendingAccelStructBuildCommit(accelStruct, accelStructType, buildFlags, geometrySignatures, instances);
}

Expected<NotNull<const AccelStructBuildSignatureRole*>> TrackedCommandBuffer::getPendingAccelStructBuildSignature(const AccelStruct& accelStruct)const noexcept{
    return m_accelStructJournal.getPendingAccelStructBuildSignature(accelStruct);
}

b8 TrackedCommandBuffer::validatePendingAccelStructBuildCommits()const noexcept{
    return m_accelStructJournal.validatePendingAccelStructBuildCommits();
}

void TrackedCommandBuffer::commitPendingAccelStructBuildCommits()noexcept{
    m_accelStructJournal.commitPendingAccelStructBuildCommits();
}

void TrackedCommandBuffer::retainTopLevelAccelStruct(AccelStruct& accelStruct){
    m_accelStructJournal.retainTopLevelAccelStruct(accelStruct);
}

void TrackedCommandBuffer::retainAcceptedTopLevelAccelStructReferences(){
    m_accelStructJournal.retainAcceptedTopLevelAccelStructReferences();
}

void TrackedCommandBuffer::retainPendingTopLevelAccelStructReferences(const TrackedCommandBuffer& buildCommandBuffer){
    m_accelStructJournal.retainPendingTopLevelAccelStructReferences(buildCommandBuffer.m_accelStructJournal);
}

void TrackedCommandBuffer::abandonPendingAccelStructBuildCommits()noexcept{
    m_accelStructJournal.abandonPendingAccelStructBuildCommits();
}

void TrackedCommandBuffer::appendPendingOpacityMicromapBuildCommit(OpacityMicromap& opacityMicromap){
    m_resourceReferences.retainResource(opacityMicromap);
    m_pendingOpacityMicromapBuildCommits.push_back(PendingOpacityMicromapBuildCommit{ .opacityMicromap = MakeNotNull(&opacityMicromap), });
}

b8 TrackedCommandBuffer::hasPendingOpacityMicromapBuild(const OpacityMicromap& opacityMicromap)const noexcept{
    for(usize commitIndex = m_pendingOpacityMicromapBuildCommits.size(); commitIndex > 0u; --commitIndex){
        if(m_pendingOpacityMicromapBuildCommits[commitIndex - 1u].opacityMicromap.get() == &opacityMicromap)
            return true;
    }

    return false;
}

void TrackedCommandBuffer::commitPendingOpacityMicromapBuildCommits()noexcept{
    static_assert(noexcept(m_pendingOpacityMicromapBuildCommits.clear()), "accepted opacity-micromap commit release must be non-throwing");
    for(const PendingOpacityMicromapBuildCommit& commit : m_pendingOpacityMicromapBuildCommits){
        commit.opacityMicromap->m_acceptedConstructed.store(true, MemoryOrder::release);
    }
    m_pendingOpacityMicromapBuildCommits.clear();
}

void TrackedCommandBuffer::discardPendingRecordingCommits()noexcept{
    discardTimerQueryRecordingClaims();
    m_resourceReferences.discardBufferStateCommits();
    m_retainedTextureStateCommits.clear();
    abandonPendingAccelStructBuildCommits();
    m_pendingOpacityMicromapBuildCommits.clear();
}

void TrackedCommandBuffer::clearTrackedReferences()noexcept{
    discardPendingRecordingCommits();

    for(const auto heap : m_referencedDescriptorHeaps){

        heap->discardCommandBufferUse(*this);
    }
    m_referencedDescriptorHeaps.clear();
    m_descriptorBufferManager = nullptr;
    m_descriptorBufferGeneration = 0u;

    m_accelStructJournal.clearTrackedReferences();
    m_resourceReferences.clear();
    m_referencedStagingBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

