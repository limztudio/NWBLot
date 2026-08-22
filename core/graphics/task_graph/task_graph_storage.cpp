// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_storage{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidTextureRange(const TextureSubresourceSet& range)noexcept{
    return range.numMipLevels != 0u && range.numArraySlices != 0u;
}

[[nodiscard]] static u64 TextureRangeEnd(const u32 base, const u32 count, const u32 all)noexcept{
    return count == all ? Limit<u64>::s_Max : static_cast<u64>(base) + static_cast<u64>(count);
}

[[nodiscard]] static bool TextureRangesOverlap(
    const TextureSubresourceSet& lhs,
    const TextureSubresourceSet& rhs
)noexcept{
    const u64 lhsMipEnd = TextureRangeEnd(
        lhs.baseMipLevel,
        lhs.numMipLevels,
        TextureSubresourceSet::AllMipLevels
    );
    const u64 rhsMipEnd = TextureRangeEnd(
        rhs.baseMipLevel,
        rhs.numMipLevels,
        TextureSubresourceSet::AllMipLevels
    );
    const u64 lhsArrayEnd = TextureRangeEnd(
        lhs.baseArraySlice,
        lhs.numArraySlices,
        TextureSubresourceSet::AllArraySlices
    );
    const u64 rhsArrayEnd = TextureRangeEnd(
        rhs.baseArraySlice,
        rhs.numArraySlices,
        TextureSubresourceSet::AllArraySlices
    );
    return lhs.baseMipLevel < rhsMipEnd
        && rhs.baseMipLevel < lhsMipEnd
        && lhs.baseArraySlice < rhsArrayEnd
        && rhs.baseArraySlice < lhsArrayEnd;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::appendTask(
    const GpuTaskDesc& desc,
    void* const payload,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload,
    const usize payloadObjectSize
){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || m_tasks.size() >= Limit<u32>::s_Max
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size()
        || desc.externalDependencyCount > Limit<u32>::s_Max - m_externalDependencies.size()
        || desc.externalStateSourceCount > Limit<u32>::s_Max - m_externalStateSources.size()
        || desc.resourceUseCount > Limit<u32>::s_Max
        || desc.resourceSetUseCount > Limit<u32>::s_Max
        || (desc.dependencyCount > 0u && !desc.dependencies)
        || (desc.externalDependencyCount > 0u && !desc.externalDependencies)
        || (desc.externalStateSourceCount > 0u && !desc.externalStateSources)
        || (desc.resourceUseCount > 0u && !desc.resourceUses)
        || (desc.resourceSetUseCount > 0u && !desc.resourceSetUses)
        || ((payload == nullptr) != (payloadObjectSize == 0u))
    )
        return {};

    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        if(!desc.externalStateSources[sourceIndex].states)
            return {};
    }

    // Resource sets are immutable graph data. Expand them now rather than introducing an opaque aggregate into
    // hazard analysis or barrier lowering; every later stage sees the same concrete member uses as an explicit
    // declaration would have supplied.
    usize expandedResourceUseCount = desc.resourceUseCount;
    const usize remainingResourceUseCapacity = static_cast<usize>(Limit<u32>::s_Max) - m_resourceUses.size();
    if(expandedResourceUseCount > remainingResourceUseCapacity)
        return {};
    for(usize resourceSetUseIndex = 0u; resourceSetUseIndex < desc.resourceSetUseCount; ++resourceSetUseIndex){
        const GpuTaskResourceSetUse& resourceSetUse = desc.resourceSetUses[resourceSetUseIndex];
        if(!validResourceSet(resourceSetUse.resourceSet))
            return {};
        const GpuGraphResourceSetNode& resourceSet = m_resourceSets[resourceSetUse.resourceSet.index];
        if(resourceSet.memberCount > remainingResourceUseCapacity - expandedResourceUseCount)
            return {};
        expandedResourceUseCount += resourceSet.memberCount;
    }

    // Task declarations previously retained a borrowed native handoff until late packet recording. Capture each
    // source when the graph accepts the declaration instead, preserving an invalid source as invalid so its
    // established record-time diagnostic remains intact for malformed legacy callers.
    GraphicsVector<CommandListResourceStateHandoff*> externalStateSnapshots(m_arena);
    externalStateSnapshots.reserve(desc.externalStateSourceCount);
    const auto destroyExternalStateSnapshots = [&]{
        for(CommandListResourceStateHandoff* const states : externalStateSnapshots)
            DestroyArenaObject(m_arena, states);
    };
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff* const source = desc.externalStateSources[sourceIndex].states;
        CommandListResourceStateHandoff* const snapshot = NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena);
        if(!snapshot){
            destroyExternalStateSnapshots();
            return {};
        }
        if(source->valid() && !snapshot->copyFrom(*source)){
            DestroyArenaObject(m_arena, snapshot);
            destroyExternalStateSnapshots();
            return {};
        }
        externalStateSnapshots.push_back(snapshot);
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize)){
        destroyExternalStateSnapshots();
        return {};
    }

    GpuTaskNode task;
    task.identity = desc.identity;
    task.queue = desc.queue;
    task.scheduling = desc.scheduling;
    task.timing = desc.timing;
    task.markerLabelOffset = markerLabelOffset;
    task.markerLabelSize = markerLabelSize;
    task.dependencyOffset = static_cast<u32>(m_dependencies.size());
    task.dependencyCount = static_cast<u32>(desc.dependencyCount);
    task.externalDependencyOffset = static_cast<u32>(m_externalDependencies.size());
    task.externalDependencyCount = static_cast<u32>(desc.externalDependencyCount);
    task.externalStateSourceOffset = static_cast<u32>(m_externalStateSources.size());
    task.externalStateSourceCount = static_cast<u32>(desc.externalStateSourceCount);
    task.resourceUseOffset = static_cast<u32>(m_resourceUses.size());
    task.resourceUseCount = static_cast<u32>(expandedResourceUseCount);
    task.directResourceUseCount = static_cast<u32>(desc.resourceUseCount);
    task.declaredResourceSetUseCount = static_cast<u32>(desc.resourceSetUseCount);
    task.expandedResourceSetMemberUseCount = static_cast<u32>(expandedResourceUseCount - desc.resourceUseCount);
    task.payload = payload;
    task.payloadObjectSize = payloadObjectSize;
    task.recordPayload = recordPayload;
    task.acceptPayload = acceptPayload;
    task.discardPayload = discardPayload;
    task.destroyPayload = destroyPayload;
    task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration;

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    for(usize dependencyIndex = 0u; dependencyIndex < desc.externalDependencyCount; ++dependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[dependencyIndex]);
    m_externalStateSources.reserve(m_externalStateSources.size() + desc.externalStateSourceCount);
    m_externalStateSnapshots.reserve(m_externalStateSnapshots.size() + desc.externalStateSourceCount);
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff* const snapshot = externalStateSnapshots[sourceIndex];
        m_externalStateSources.push_back(GpuTaskExternalStateSource{ .states = snapshot });
        m_externalStateSnapshots.push_back(externalStateSnapshots[sourceIndex]);
    }
    for(usize useIndex = 0u; useIndex < desc.resourceUseCount; ++useIndex)
        m_resourceUses.push_back(desc.resourceUses[useIndex]);
    for(usize resourceSetUseIndex = 0u; resourceSetUseIndex < desc.resourceSetUseCount; ++resourceSetUseIndex){
        const GpuTaskResourceSetUse& resourceSetUse = desc.resourceSetUses[resourceSetUseIndex];
        const GpuGraphResourceSetNode& resourceSet = m_resourceSets[resourceSetUse.resourceSet.index];
        for(usize memberIndex = 0u; memberIndex < resourceSet.memberCount; ++memberIndex){
            m_resourceUses.push_back(GpuTaskResourceUse{
                .resource = m_resourceSetMembers[resourceSet.memberOffset + memberIndex],
                .range = resourceSetUse.range,
                .requiredState = resourceSetUse.requiredState,
                .access = resourceSetUse.access,
                .hasIndependentStateSource = resourceSetUse.hasIndependentStateSource,
            });
        }
    }

    const u32 index = static_cast<u32>(m_tasks.size());
    m_tasks.push_back(Move(task));
    return GpuTaskId{ index, m_generation };
}

void GpuTaskGraph::discardAndDestroyUnappendedPayload(
    void* const payload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
)noexcept{
    if(!payload)
        return;

    if(discardPayload)
        discardPayload(payload);
    if(destroyPayload)
        destroyPayload(m_arena, payload);
}

GpuGraphResourceId GpuTaskGraph::appendResource(const GpuGraphResourceDesc& desc){
    const bool hasInitialOwnerHandoff =
        desc.initialOwnerReleaseDestinationQueue.valid()
        || desc.initialOwnerCompletion.valid()
        || desc.initialOwnerMinimumCompletionToken.valid()
        || desc.initialOwnerStateSource != nullptr
    ;
    const bool hasMultiInitialOwnerHandoff =
        desc.initialOwnerHandoffSources != nullptr
        || desc.initialOwnerHandoffSourceCount != 0u
    ;
    const bool hasExternalFinalRelease = desc.externalFinalReleaseDestinationQueue.valid();
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphResourceType::kCount
        || (
            desc.externalFinalState != ResourceStates::Unknown
            && desc.type != GpuGraphResourceType::Texture
            && desc.type != GpuGraphResourceType::Buffer
            && desc.type != GpuGraphResourceType::AccelStruct
        )
        || (
            hasExternalFinalRelease
            && (
                desc.externalFinalState == ResourceStates::Unknown
                || (
                    desc.type != GpuGraphResourceType::Texture
                    && desc.type != GpuGraphResourceType::Buffer
                    && desc.type != GpuGraphResourceType::AccelStruct
                )
            )
        )
        || (
            hasMultiInitialOwnerHandoff
            && (
                desc.type != GpuGraphResourceType::Texture
                || !desc.initialOwnerHandoffSources
                || desc.initialOwnerHandoffSourceCount == 0u
                || hasInitialOwnerHandoff
                || desc.initialOwnerQueue.valid()
                || desc.initialState == ResourceStates::Unknown
                || desc.queueSharing != ResourceQueueSharing::Exclusive
                || desc.initialOwnerHandoffSourceCount
                    > static_cast<usize>(Limit<u32>::s_Max) - m_initialOwnerHandoffSources.size()
            )
        )
        || (
            desc.initialOwnerQueue.valid()
            && desc.type != GpuGraphResourceType::Texture
            && desc.type != GpuGraphResourceType::Buffer
            && desc.type != GpuGraphResourceType::AccelStruct
        )
        || (
            hasInitialOwnerHandoff
            && (
                !desc.initialOwnerQueue.valid()
                || !desc.initialOwnerReleaseDestinationQueue.valid()
                || desc.initialOwnerReleaseDestinationQueue == desc.initialOwnerQueue
                || !desc.initialOwnerCompletion.valid()
                || !validExternalCompletion(desc.initialOwnerCompletion)
                || !desc.initialOwnerMinimumCompletionToken.valid()
                || !desc.initialOwnerMinimumCompletionToken.matchesPhysicalQueue(
                    desc.initialOwnerQueue.index,
                    desc.initialOwnerQueue.deviceGeneration
                )
                || !desc.initialOwnerStateSource
                || desc.initialState == ResourceStates::Unknown
            )
        )
        || m_resources.size() >= Limit<u32>::s_Max
    )
        return {};

    if(hasMultiInitialOwnerHandoff){
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            if(
                !__hidden_gpu_task_graph_storage::ValidTextureRange(source.range.textureSubresources)
                || !source.sourceQueue.valid()
                || !source.destinationQueue.valid()
                || !source.completion.valid()
                || !validExternalCompletion(source.completion)
                || !source.minimumCompletionToken.valid()
                || !source.minimumCompletionToken.matchesPhysicalQueue(
                    source.sourceQueue.index,
                    source.sourceQueue.deviceGeneration
                )
                || !source.stateSource
            )
                return {};
            for(usize previousSourceIndex = 0u; previousSourceIndex < sourceIndex; ++previousSourceIndex){
                if(__hidden_gpu_task_graph_storage::TextureRangesOverlap(
                    source.range.textureSubresources,
                    desc.initialOwnerHandoffSources[previousSourceIndex].range.textureSubresources
                ))
                    return {};
            }
        }
    }

    // Initial-owner imports previously borrowed this producer snapshot until late recording. Freeze it while the
    // resource is declared instead, so the declaration owns the exact state metadata. Preserve an invalid snapshot
    // as invalid so the established record-time diagnostic remains intact for malformed legacy callers.
    CommandListResourceStateHandoff* initialOwnerStateSnapshot = nullptr;
    if(desc.initialOwnerStateSource){
        initialOwnerStateSnapshot = NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena);
        if(!initialOwnerStateSnapshot)
            return {};
        if(
            desc.initialOwnerStateSource->valid()
            && !initialOwnerStateSnapshot->copyFrom(*desc.initialOwnerStateSource)
        ){
            DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
            return {};
        }
    }

    const usize initialOwnerHandoffSourceOffset = m_initialOwnerHandoffSources.size();
    const auto discardInitialOwnerHandoffSources = [&]{
        while(m_initialOwnerHandoffSources.size() > initialOwnerHandoffSourceOffset){
            GpuTaskGraphInitialOwnerHandoffSourceView& source = m_initialOwnerHandoffSources.back();
            if(source.stateSource)
                DestroyArenaObject(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
            m_initialOwnerHandoffSources.pop_back();
        }
    };
    if(hasMultiInitialOwnerHandoff){
        m_initialOwnerHandoffSources.reserve(
            m_initialOwnerHandoffSources.size() + desc.initialOwnerHandoffSourceCount
        );
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            CommandListResourceStateHandoff* const stateSnapshot =
                NewArenaObject<CommandListResourceStateHandoff>(m_arena, m_arena)
            ;
            if(!stateSnapshot){
                discardInitialOwnerHandoffSources();
                if(initialOwnerStateSnapshot)
                    DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
                return {};
            }
            if(source.stateSource->valid() && !stateSnapshot->copyFrom(*source.stateSource)){
                DestroyArenaObject(m_arena, stateSnapshot);
                discardInitialOwnerHandoffSources();
                if(initialOwnerStateSnapshot)
                    DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
                return {};
            }
            m_initialOwnerHandoffSources.push_back(GpuTaskGraphInitialOwnerHandoffSourceView{
                .range = source.range,
                .sourceQueue = source.sourceQueue,
                .destinationQueue = source.destinationQueue,
                .completion = source.completion,
                .minimumCompletionToken = source.minimumCompletionToken,
                .stateSource = stateSnapshot,
            });
        }
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize)){
        discardInitialOwnerHandoffSources();
        if(initialOwnerStateSnapshot)
            DestroyArenaObject(m_arena, initialOwnerStateSnapshot);
        return {};
    }

    GpuGraphResourceNode resource;
    resource.identity = desc.identity;
    resource.type = desc.type;
    resource.initialState = desc.initialState;
    resource.externalFinalState = desc.externalFinalState;
    resource.externalFinalReleaseDestinationQueue = desc.externalFinalReleaseDestinationQueue;
    resource.initialOwnerQueue = desc.initialOwnerQueue;
    resource.initialOwnerReleaseDestinationQueue = desc.initialOwnerReleaseDestinationQueue;
    resource.initialOwnerCompletion = desc.initialOwnerCompletion;
    resource.initialOwnerMinimumCompletionToken = desc.initialOwnerMinimumCompletionToken;
    resource.initialOwnerStateSource = initialOwnerStateSnapshot;
    resource.initialOwnerStateSourceIdentity = desc.initialOwnerStateSource;
    resource.initialOwnerHandoffSourceOffset = static_cast<u32>(initialOwnerHandoffSourceOffset);
    resource.initialOwnerHandoffSourceCount = static_cast<u32>(desc.initialOwnerHandoffSourceCount);
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelOffset;
    resource.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_resources.size());
    m_resources.push_back(Move(resource));
    return GpuGraphResourceId{ index, m_generation };
}

GpuGraphResourceSetId GpuTaskGraph::appendResourceSet(const GpuGraphResourceSetDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (desc.memberCount != 0u && !desc.members)
        || desc.memberCount > static_cast<usize>(Limit<u32>::s_Max) - m_resourceSetMembers.size()
        || m_resourceSets.size() >= Limit<u32>::s_Max
    )
        return {};

    for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
        const GpuGraphResourceId member = desc.members[memberIndex];
        if(!validResource(member))
            return {};
        for(usize previousMemberIndex = 0u; previousMemberIndex < memberIndex; ++previousMemberIndex){
            if(desc.members[previousMemberIndex] == member)
                return {};
        }
    }

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphResourceSetNode resourceSet;
    resourceSet.identity = desc.identity;
    resourceSet.markerLabelOffset = markerLabelOffset;
    resourceSet.markerLabelSize = markerLabelSize;
    resourceSet.memberOffset = static_cast<u32>(m_resourceSetMembers.size());
    resourceSet.memberCount = static_cast<u32>(desc.memberCount);
    for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex)
        m_resourceSetMembers.push_back(desc.members[memberIndex]);

    const u32 index = static_cast<u32>(m_resourceSets.size());
    m_resourceSets.push_back(Move(resourceSet));
    return GpuGraphResourceSetId{ index, m_generation };
}

GpuGraphPipelineId GpuTaskGraph::appendPipeline(const GpuGraphPipelineDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphPipelineType::kCount
        || m_pipelines.size() >= Limit<u32>::s_Max
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphPipelineNode pipeline;
    pipeline.identity = desc.identity;
    pipeline.type = desc.type;
    pipeline.markerLabelOffset = markerLabelOffset;
    pipeline.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_pipelines.size());
    m_pipelines.push_back(Move(pipeline));
    return GpuGraphPipelineId{ index, m_generation };
}

GpuExternalCompletionId GpuTaskGraph::appendExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || m_externalCompletions.size() >= Limit<u32>::s_Max)
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuExternalCompletionNode completion;
    completion.identity = desc.identity;
    completion.markerLabelOffset = markerLabelOffset;
    completion.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_externalCompletions.size());
    m_externalCompletions.push_back(Move(completion));
    return GpuExternalCompletionId{ index, m_generation };
}

const GpuTaskGraph::GpuUploadBlobNode* GpuTaskGraph::findUploadBlob(
    const GpuUploadBlobId& blob
)const noexcept{
    if(!validUploadBlob(blob))
        return nullptr;
    return &m_uploadBlobs[blob.index];
}

bool GpuTaskGraph::appendMarkerLabel(const AStringView text, u32& outOffset, u32& outSize){
    if(
        text.empty()
        || !text.data()
        || text.size() > Limit<u32>::s_Max
        || text.size() > Limit<u32>::s_Max - m_markerText.size()
    )
        return false;

    outOffset = static_cast<u32>(m_markerText.size());
    outSize = static_cast<u32>(text.size());
    const usize nextSize = m_markerText.size() + text.size();
    m_markerText.resize(nextSize);
    NWB_MEMCPY(m_markerText.data() + outOffset, outSize, text.data(), text.size());
    return true;
}

AStringView GpuTaskGraph::markerLabel(const u32 offset, const u32 size)const{
    NWB_ASSERT(offset <= m_markerText.size());
    NWB_ASSERT(size <= m_markerText.size() - offset);
    return AStringView(reinterpret_cast<const char*>(m_markerText.data() + offset), size);
}

bool GpuTaskGraph::destroyTaskPayloads()noexcept{
    {
        ScopedLock lock(m_lifecycleMutex);
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == GpuTaskLifecycleState::Submitting
                || task.lifecycleState == GpuTaskLifecycleState::Accepting
                || task.lifecycleState == GpuTaskLifecycleState::Discarding
                || task.lifecycleState == GpuTaskLifecycleState::Recording
            ){
                return false;
            }
        }
        for(GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == GpuTaskLifecycleState::Declared
                || task.lifecycleState == GpuTaskLifecycleState::Recording
                || task.lifecycleState == GpuTaskLifecycleState::Recorded
            )
                task.lifecycleState = GpuTaskLifecycleState::Discarding;
        }
    }

    for(GpuTaskNode& task : m_tasks){
        if(task.lifecycleState == GpuTaskLifecycleState::Discarding && task.payload && task.discardPayload)
            task.discardPayload(task.payload);
    }

    {
        ScopedLock lock(m_lifecycleMutex);
        for(GpuTaskNode& task : m_tasks){
            if(task.lifecycleState != GpuTaskLifecycleState::Discarding)
                continue;
            task.lifecycleState = GpuTaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
    }

    for(GpuTaskNode& task : m_tasks){
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.destroyPayload = nullptr;
    }
    return true;
}

void GpuTaskGraph::destroyTaskStateSnapshots()noexcept{
    for(CommandListResourceStateHandoff* const states : m_externalStateSnapshots)
        DestroyArenaObject(m_arena, states);
    m_externalStateSnapshots.clear();
}

void GpuTaskGraph::destroyResourceStateSnapshots()noexcept{
    for(GpuGraphResourceNode& resource : m_resources){
        if(resource.initialOwnerStateSource)
            DestroyArenaObject(m_arena, resource.initialOwnerStateSource);
        resource.initialOwnerStateSource = nullptr;
        resource.initialOwnerStateSourceIdentity = nullptr;
    }
    for(GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(source.stateSource)
            DestroyArenaObject(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
        source.stateSource = nullptr;
    }
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

