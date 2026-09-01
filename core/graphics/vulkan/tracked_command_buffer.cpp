// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TrackedCommandBuffer::TrackedCommandBuffer(
    Queue& queue,
    const VulkanContext& context,
    const u32 queueFamilyIndex,
    const VkCommandPool commandPool,
    const bool ownsCommandPool,
    Futex* const sharedCommandPoolMutex
)
    : RefCounter<GraphicsResource>(context.threadPool)
    , m_cmdPool(commandPool)
    , m_ownsCmdPool(ownsCommandPool)
    , m_sharedCommandPoolMutex(sharedCommandPoolMutex)
    , m_referencedResources(context.objectArena)
    , m_referencedBuffers(context.objectArena)
    , m_referencedTextures(context.objectArena)
    , m_referencedStagingBuffers(context.objectArena)
    , m_referencedDescriptorHeaps(context.objectArena)
    , m_retainedBufferStateCommits(context.objectArena)
    , m_retainedTextureStateCommits(context.objectArena)
    , m_pendingAccelStructBuildCommits(context.objectArena)
    , m_pendingOpacityMicromapBuildCommits(context.objectArena)
    , m_timerQueryRecordingClaims(context.objectArena)
    , m_context(context)
    , m_queue(queue)
{
    if(m_ownsCmdPool){
        auto poolInfo = VulkanDetail::MakeVkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

        const VkResult createResult = m_context.deviceDispatch.vkCreateCommandPool(
            m_context.device,
            &poolInfo,
            m_context.allocationCallbacks,
            &m_cmdPool
        );
        if(createResult != VK_SUCCESS){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create private command pool: {}"), ResultToString(createResult));
            m_cmdPool = VK_NULL_HANDLE;
            return;
        }
    }
    else if(m_cmdPool == VK_NULL_HANDLE){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Cannot allocate a command buffer without a worker command pool."));
        return;
    }

    auto allocInfo = VulkanDetail::MakeVkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    const VkResult allocateResult = m_context.deviceDispatch.vkAllocateCommandBuffers(m_context.device, &allocInfo, &m_cmdBuf);
    if(allocateResult != VK_SUCCESS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to allocate command buffer: {}"), ResultToString(allocateResult));
        m_cmdBuf = VK_NULL_HANDLE;
        if(m_ownsCmdPool)
            m_context.deviceDispatch.vkDestroyCommandPool(m_context.device, m_cmdPool, m_context.allocationCallbacks);
        m_cmdPool = VK_NULL_HANDLE;
        m_ownsCmdPool = false;
    }
}

TrackedCommandBuffer::~TrackedCommandBuffer(){
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

void TrackedCommandBuffer::retainResource(GraphicsResource& resource){
    for(const Handle<GraphicsResource>& retainedResource : m_referencedResources){
        if(retainedResource.get() == &resource)
            return;
    }

    m_referencedResources.emplace_back(&resource, Handle<GraphicsResource>::deleter_type(&m_context.objectArena));
}

TrackedCommandBuffer::TimerQueryRecordingClaim& TrackedCommandBuffer::findOrAppendTimerQueryRecordingClaim(TimerQuery& query){
    for(TimerQueryRecordingClaim& claim : m_timerQueryRecordingClaims){
        if(claim.query == &query)
            return claim;
    }

    retainResource(query);
    m_timerQueryRecordingClaims.emplace_back();
    TimerQueryRecordingClaim& claim = m_timerQueryRecordingClaims.back();
    claim.query = &query;
    claim.queryIncarnation = query.m_incarnation;
    return claim;
}

bool TrackedCommandBuffer::recordsTimerQueryBegin(const TimerQuery& query, const u64 generation)const noexcept{
    for(const TimerQueryRecordingClaim& claim : m_timerQueryRecordingClaims){
        if(
            claim.query == &query
            && claim.generation == generation
            && claim.recordingID == m_recordingID
            && claim.recordsBegin
        )
            return true;
    }
    return false;
}

bool TrackedCommandBuffer::validateTimerQueryRecordingClaims(
    const Queue& submissionQueue,
    const QueueSubmissionWait* const localWaits,
    const usize localWaitCount,
    TrackedCommandBuffer* const* const precedingCommandBuffers,
    const usize precedingCommandBufferCount
)const noexcept{
    for(const TimerQueryRecordingClaim& claim : m_timerQueryRecordingClaims){
        TimerQuery* const query = claim.query;
        if(
            !query
            || claim.queryIncarnation == 0u
            || query->m_incarnation != claim.queryIncarnation
            || claim.recordingID == 0u
            || claim.recordingID != m_recordingID
            || claim.queue != submissionQueue.m_physicalQueue
        )
            return false;

        ScopedLock queryLock(query->m_mutex);
        const QueueSubmissionToken currentPrerequisite = query->m_completedCycleSubmission.valid()
            ? query->m_completedCycleSubmission
            : query->m_resetAuthorizationSubmission
        ;
        const bool prerequisiteMatches =
            currentPrerequisite.queue == claim.prerequisiteSubmission.queue
            && currentPrerequisite.value == claim.prerequisiteSubmission.value
            && currentPrerequisite.physicalQueueIndex == claim.prerequisiteSubmission.physicalQueueIndex
            && currentPrerequisite.deviceGeneration == claim.prerequisiteSubmission.deviceGeneration
        ;
        if(
            (claim.recordsReset || claim.recordsBegin)
            && (
                !prerequisiteMatches
                || !submissionQueue.coversTimerQueryPrerequisite(
                    claim.prerequisiteSubmission,
                    claim.prerequisiteObservedComplete,
                    localWaits,
                    localWaitCount
                )
            )
        )
            return false;
        const bool resetOwnerMatches =
            query->m_resetRecordingOwner.commandBuffer == this
            && query->m_resetRecordingOwner.recordingID == claim.recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
            && query->m_resetRecordingAuthorizationGeneration == claim.resetRecordingAuthorizationGeneration
        ;
        const bool beginOwnerMatches =
            query->m_beginRecordingOwner.commandBuffer == this
            && query->m_beginRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ;
        const bool endOwnerMatches =
            query->m_endRecordingOwner.commandBuffer == this
            && query->m_endRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ;
        if(claim.recordsReset && !claim.recordsBegin && !resetOwnerMatches)
            return false;
        if(
            claim.recordsBegin
            && (
                !beginOwnerMatches
                || query->m_cycleInvalidated
                || query->m_cycleQueue != claim.queue
                || (
                    claim.consumesResetAuthorization
                    && (
                        !query->m_resetAuthorizationAvailable
                        || query->m_resetAuthorizationGeneration != claim.consumedResetAuthorizationGeneration
                        || query->m_resetAuthorizationSubmission.queue != claim.resetAuthorizationSubmission.queue
                        || query->m_resetAuthorizationSubmission.value != claim.resetAuthorizationSubmission.value
                        || query->m_resetAuthorizationSubmission.physicalQueueIndex != claim.resetAuthorizationSubmission.physicalQueueIndex
                        || query->m_resetAuthorizationSubmission.deviceGeneration != claim.resetAuthorizationSubmission.deviceGeneration
                    )
                )
            )
        )
            return false;
        if(claim.recordsEnd){
            if(
                !endOwnerMatches
                || query->m_cycleInvalidated
                || query->m_cycleQueue != claim.queue
            )
                return false;
            if(query->m_beginAccepted || claim.recordsBegin)
                continue;

            bool orderedAfterBatchBegin = false;
            for(usize precedingIndex = 0u; precedingIndex < precedingCommandBufferCount; ++precedingIndex){
                TrackedCommandBuffer* const preceding = precedingCommandBuffers[precedingIndex];
                if(
                    preceding
                    && preceding == query->m_beginRecordingOwner.commandBuffer
                    && preceding->m_recordingID == query->m_beginRecordingOwner.recordingID
                    && preceding->recordsTimerQueryBegin(*query, claim.generation)
                ){
                    orderedAfterBatchBegin = true;
                    break;
                }
            }
            if(!orderedAfterBatchBegin)
                return false;
        }
    }
    return true;
}

void TrackedCommandBuffer::commitTimerQueryRecordingClaims(const QueueSubmissionToken& submissionToken)noexcept{
    for(const TimerQueryRecordingClaim& claim : m_timerQueryRecordingClaims){
        TimerQuery* const query = claim.query;
        if(!query || claim.queryIncarnation == 0u || query->m_incarnation != claim.queryIncarnation)
            continue;

        ScopedLock queryLock(query->m_mutex);
        const bool resetOwnerMatches =
            query->m_resetRecordingOwner.commandBuffer == this
            && query->m_resetRecordingOwner.recordingID == claim.recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
            && query->m_resetRecordingAuthorizationGeneration == claim.resetRecordingAuthorizationGeneration
        ;
        const bool beginOwnerMatches =
            query->m_beginRecordingOwner.commandBuffer == this
            && query->m_beginRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ;
        const bool endOwnerMatches =
            query->m_endRecordingOwner.commandBuffer == this
            && query->m_endRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ;

        if(claim.recordsReset && !claim.recordsBegin && resetOwnerMatches){
            query->m_resetRecordingOwner = {};
            query->m_resetAuthorizationGeneration = query->m_resetRecordingAuthorizationGeneration;
            query->m_resetRecordingAuthorizationGeneration = 0u;
            query->m_timestampQueue = {};
            query->m_timestampValidBits = 0u;
            query->m_completedCycleSubmission = {};
            query->m_completedCycleGeneration = 0u;
            query->m_resetAuthorizationSubmission = submissionToken;
            query->m_resetAuthorizationAvailable = true;
            query->m_recordingActive = false;
        }
        if(claim.recordsBegin && beginOwnerMatches){
            query->m_beginRecordingOwner = {};
            query->m_beginAccepted = true;
            query->m_lastAcceptedRecordingGeneration = claim.generation;
            query->m_timestampQueue = query->m_cycleQueue;
            query->m_timestampValidBits = query->m_cycleValidBits;
            query->m_completedCycleSubmission = {};
            query->m_completedCycleGeneration = 0u;
            query->m_resetAuthorizationSubmission = {};
            query->m_resetAuthorizationGeneration = 0u;
            query->m_resetAuthorizationAvailable = false;
            query->m_recordingActive = true;
        }
        if(claim.recordsEnd && endOwnerMatches && (query->m_beginAccepted || claim.recordsBegin)){
            query->m_endRecordingOwner = {};
            query->m_recordingActive = false;
            query->m_completedCycleSubmission = submissionToken;
            query->m_completedCycleGeneration = claim.generation;
            query->m_cycleBaselineQueue = {};
            query->m_cycleQueue = {};
            query->m_cycleBaselineValidBits = 0u;
            query->m_cycleValidBits = 0u;
            query->m_cycleBaselineCompletion = {};
            query->m_cycleBaselineCompletionGeneration = 0u;
            query->m_cycleBaselineActive = false;
            query->m_beginRecordingOwner = {};
            query->m_cycleGeneration = 0u;
            query->m_beginAccepted = false;
            query->m_cycleInvalidated = false;
        }
    }
    m_timerQueryRecordingClaims.clear();
}

void TrackedCommandBuffer::discardTimerQueryRecordingClaims()noexcept{
    for(const TimerQueryRecordingClaim& claim : m_timerQueryRecordingClaims){
        TimerQuery* const query = claim.query;
        if(!query || claim.queryIncarnation == 0u || query->m_incarnation != claim.queryIncarnation)
            continue;
        ScopedLock queryLock(query->m_mutex);
        if(
            claim.recordsReset
            && !claim.recordsBegin
            && query->m_resetRecordingOwner.commandBuffer == this
            && query->m_resetRecordingOwner.recordingID == claim.recordingID
            && query->m_resetRecordingAuthorizationGeneration != 0u
            && query->m_resetRecordingAuthorizationGeneration == claim.resetRecordingAuthorizationGeneration
        ){
            query->m_resetRecordingOwner = {};
            query->m_resetRecordingAuthorizationGeneration = 0u;
        }
        if(
            claim.recordsEnd
            && query->m_endRecordingOwner.commandBuffer == this
            && query->m_endRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ){
            query->m_endRecordingOwner = {};
        }
        if(
            claim.recordsBegin
            && query->m_beginRecordingOwner.commandBuffer == this
            && query->m_beginRecordingOwner.recordingID == claim.recordingID
            && query->m_cycleGeneration == claim.generation
        ){
            query->m_timestampQueue = query->m_cycleBaselineQueue;
            query->m_timestampValidBits = query->m_cycleBaselineValidBits;
            query->m_completedCycleSubmission = query->m_cycleBaselineCompletion;
            query->m_completedCycleGeneration = query->m_cycleBaselineCompletionGeneration;
            query->m_recordingActive = query->m_cycleBaselineActive;
            query->m_beginRecordingOwner = {};
            query->m_beginAccepted = false;
            query->m_cycleInvalidated = query->m_endRecordingOwner.commandBuffer != nullptr;
        }
        if(
            query->m_cycleGeneration == claim.generation
            && !query->m_beginRecordingOwner.commandBuffer
            && !query->m_endRecordingOwner.commandBuffer
            && !query->m_beginAccepted
        ){
            query->m_cycleBaselineQueue = {};
            query->m_cycleQueue = {};
            query->m_cycleBaselineValidBits = 0u;
            query->m_cycleValidBits = 0u;
            query->m_cycleBaselineCompletion = {};
            query->m_cycleBaselineCompletionGeneration = 0u;
            query->m_cycleBaselineActive = false;
            query->m_cycleGeneration = 0u;
            query->m_cycleInvalidated = false;
        }
    }
    m_timerQueryRecordingClaims.clear();
}

void TrackedCommandBuffer::retainBuffer(Buffer& buffer){
    retainResource(buffer);
    trackRetainedBuffer(buffer);
}

void TrackedCommandBuffer::trackRetainedBuffer(Buffer& buffer){
    for(Buffer* const retainedBuffer : m_referencedBuffers){
        if(retainedBuffer == &buffer)
            return;
    }

    m_referencedBuffers.push_back(&buffer);
}

void TrackedCommandBuffer::appendRetainedBufferStateCommit(Buffer& buffer){
    retainBuffer(buffer);
    for(const RetainedBufferStateCommit& commit : m_retainedBufferStateCommits){
        if(commit.buffer == &buffer)
            return;
    }
    m_retainedBufferStateCommits.push_back(RetainedBufferStateCommit{ .buffer = &buffer });
}

void TrackedCommandBuffer::commitRetainedBufferStateCommits()noexcept{
    for(const RetainedBufferStateCommit& commit : m_retainedBufferStateCommits){
        if(commit.buffer)
            commit.buffer->setRetainedStateKnown(true);
    }
    m_retainedBufferStateCommits.clear();
}

void TrackedCommandBuffer::discardRetainedBufferStateCommits()noexcept{
    m_retainedBufferStateCommits.clear();
}

void TrackedCommandBuffer::retainTexture(Texture& texture){
    retainResource(texture);
    trackRetainedTexture(texture);
}

void TrackedCommandBuffer::trackRetainedTexture(Texture& texture){
    for(Texture* const retainedTexture : m_referencedTextures){
        if(retainedTexture == &texture)
            return;
    }

    m_referencedTextures.push_back(&texture);
}

void TrackedCommandBuffer::appendRetainedTextureStateCommit(
    Texture& texture,
    const MipLevel mipLevel,
    const ArraySlice arraySlice
){
    // The closing barrier and its deferred state publication outlive CommandList::clearState(). Keep the texture
    // alive with the command buffer until Queue::submit accepts or discards the command buffer.
    retainTexture(texture);

    m_retainedTextureStateCommits.push_back(RetainedTextureStateCommit{
        .texture = &texture,
        .mipLevel = mipLevel,
        .arraySlice = arraySlice,
    });
}

void TrackedCommandBuffer::commitRetainedTextureStateCommits()noexcept{
    for(const RetainedTextureStateCommit& commit : m_retainedTextureStateCommits){
        if(commit.texture)
            commit.texture->setRetainedSubresourceStateKnown(commit.arraySlice, commit.mipLevel, true);
    }
    m_retainedTextureStateCommits.clear();
}

void TrackedCommandBuffer::discardRetainedTextureStateCommits()noexcept{
    m_retainedTextureStateCommits.clear();
}

void TrackedCommandBuffer::appendPendingAccelStructBuildCommit(
    AccelStruct& accelStruct,
    const VkAccelerationStructureTypeKHR accelStructType,
    const VkBuildAccelerationStructureFlagsKHR buildFlags,
    const AccelStructGeometryBuildSignature* const geometrySignatures,
    const usize geometrySignatureCount
){
    NWB_ASSERT(geometrySignatureCount <= UINT32_MAX);
    NWB_ASSERT(geometrySignatureCount == 0u || geometrySignatures);
    if(geometrySignatureCount > UINT32_MAX || (geometrySignatureCount != 0u && !geometrySignatures))
        return;

    PendingAccelStructBuildCommit commit{m_context.objectArena};
    commit.accelStruct = &accelStruct;
    commit.accelStructType = accelStructType;
    commit.buildFlags = buildFlags;
    if(geometrySignatureCount != 0u)
        commit.geometrySignatures.assign(geometrySignatures, geometrySignatures + geometrySignatureCount);

    retainResource(accelStruct);
    m_pendingAccelStructBuildCommits.push_back(Move(commit));
}

bool TrackedCommandBuffer::getPendingAccelStructBuildSignature(
    const AccelStruct& accelStruct,
    VkAccelerationStructureTypeKHR& outAccelStructType,
    VkBuildAccelerationStructureFlagsKHR& outBuildFlags,
    const AccelStructGeometryBuildSignature*& outGeometrySignatures,
    usize& outGeometrySignatureCount
)const{
    for(usize commitIndex = m_pendingAccelStructBuildCommits.size(); commitIndex > 0u; --commitIndex){
        const PendingAccelStructBuildCommit& commit = m_pendingAccelStructBuildCommits[commitIndex - 1u];
        if(commit.accelStruct == &accelStruct){
            outAccelStructType = commit.accelStructType;
            outBuildFlags = commit.buildFlags;
            outGeometrySignatures = commit.geometrySignatures.empty() ? nullptr : commit.geometrySignatures.data();
            outGeometrySignatureCount = commit.geometrySignatures.size();
            return true;
        }
    }

    return false;
}

void TrackedCommandBuffer::commitPendingAccelStructBuildCommits()noexcept{
    for(PendingAccelStructBuildCommit& commit : m_pendingAccelStructBuildCommits){
        if(!commit.accelStruct)
            continue;

        AccelStruct& accelStruct = *commit.accelStruct;
        ScopedLock lock(accelStruct.m_acceptedBuildSignatureMutex);
        accelStruct.m_acceptedBuildGeometrySignatures = Move(commit.geometrySignatures);
        accelStruct.m_acceptedBuildType = commit.accelStructType;
        accelStruct.m_acceptedBuildFlags = commit.buildFlags;
        accelStruct.m_hasAcceptedBuild = true;
    }
    m_pendingAccelStructBuildCommits.clear();
}

void TrackedCommandBuffer::discardPendingAccelStructBuildCommits()noexcept{
    m_pendingAccelStructBuildCommits.clear();
}

void TrackedCommandBuffer::appendPendingOpacityMicromapBuildCommit(OpacityMicromap& opacityMicromap){
    retainResource(opacityMicromap);
    m_pendingOpacityMicromapBuildCommits.push_back(PendingOpacityMicromapBuildCommit{
        .opacityMicromap = &opacityMicromap,
    });
}

bool TrackedCommandBuffer::hasPendingOpacityMicromapBuild(const OpacityMicromap& opacityMicromap)const{
    for(usize commitIndex = m_pendingOpacityMicromapBuildCommits.size(); commitIndex > 0u; --commitIndex){
        if(m_pendingOpacityMicromapBuildCommits[commitIndex - 1u].opacityMicromap == &opacityMicromap)
            return true;
    }

    return false;
}

void TrackedCommandBuffer::commitPendingOpacityMicromapBuildCommits()noexcept{
    for(const PendingOpacityMicromapBuildCommit& commit : m_pendingOpacityMicromapBuildCommits){
        if(commit.opacityMicromap)
            commit.opacityMicromap->m_acceptedConstructed.store(true, MemoryOrder::release);
    }
    m_pendingOpacityMicromapBuildCommits.clear();
}

void TrackedCommandBuffer::discardPendingOpacityMicromapBuildCommits()noexcept{
    m_pendingOpacityMicromapBuildCommits.clear();
}

void TrackedCommandBuffer::clearTrackedReferences()noexcept{
    discardTimerQueryRecordingClaims();
    discardRetainedBufferStateCommits();
    discardRetainedTextureStateCommits();
    discardPendingAccelStructBuildCommits();
    discardPendingOpacityMicromapBuildCommits();

    for(GpuDescriptorHeap* heap : m_referencedDescriptorHeaps){
        if(heap)
            heap->discardCommandBufferUse(*this);
    }
    m_referencedDescriptorHeaps.clear();
    m_descriptorBufferManager = nullptr;
    m_descriptorBufferGeneration = 0u;

    m_referencedBuffers.clear();
    m_referencedTextures.clear();
    m_referencedResources.clear();
    m_referencedStagingBuffers.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

