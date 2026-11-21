// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/command.h>

#include <global/allocation_size.h>
#include <global/append_rollback.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_storage{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UnappendedPayloadDestroyScope final : NoCopy{
public:
    UnappendedPayloadDestroyScope(GraphicsArena& arena, void* const payload, const GpuTaskPayloadDestroyThunk destroyPayload)noexcept
        : m_arena(arena)
        , m_payload(payload)
        , m_destroyPayload(destroyPayload)
    {}
    ~UnappendedPayloadDestroyScope()noexcept{
        if(m_payload && m_destroyPayload)
            m_destroyPayload(m_arena, m_payload);
    }


private:
    GraphicsArena& m_arena;
    void* m_payload = nullptr;
    GpuTaskPayloadDestroyThunk m_destroyPayload = nullptr;
};

struct GpuAppendRollbackFailure final{
    void operator()()const noexcept{
        FOUNDATION_FATAL_ASSERT_MSG(false, "GPU graph declaration rollback cannot restore removed storage");
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::TaskPayloadDestroyScope::TaskPayloadDestroyScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::TaskPayloadDestroyScope::~TaskPayloadDestroyScope()noexcept{
    if(m_active)
        m_graph.destroyTaskPayloadObjects();
}

void GpuTaskGraph::TaskPayloadDestroyScope::activateWithinLock()noexcept{
    if(m_active)
        FOUNDATION_FATAL_ASSERT_MSG(false, "GPU task payload destruction ownership cannot be activated twice");
    m_active = true;
}

void* GpuTaskGraph::allocatePooledTaskPayloadSlow(const usize byteSize, const usize alignment){
    const usize alignMask = alignment - 1u;
    if(m_payloadPoolCurrentBlock < m_payloadPoolBlocks.size()){
        for(usize blockIndex = m_payloadPoolCurrentBlock + 1u; blockIndex < m_payloadPoolBlocks.size(); ++blockIndex){
            TaskPayloadPoolBlock& block = m_payloadPoolBlocks[blockIndex];
            const usize alignedOffset = (block.used + alignMask) & ~alignMask;
            if(alignedOffset <= block.size && byteSize <= block.size - alignedOffset){
                m_payloadPoolCurrentBlock = blockIndex;
                block.used = alignedOffset + byteSize;
                return block.data + alignedOffset;
            }
        }
    }

    const usize blockBytes = Max(s_PayloadPoolBlockBytes, byteSize);
    void* const memory = m_arena.allocate(s_PayloadPoolMaxAlign, blockBytes);
    if(!memory)
        return nullptr;

    const usize newIndex = m_payloadPoolBlocks.size();
    m_payloadPoolBlocks.push_back(TaskPayloadPoolBlock{
        .data = static_cast<u8*>(memory),
        .size = blockBytes,
        .used = byteSize,
    });
    m_payloadPoolCurrentBlock = newIndex;
    return memory;
}

void GpuTaskGraph::rewindTaskPayloadPool()noexcept{
    for(TaskPayloadPoolBlock& block : m_payloadPoolBlocks)
        block.used = 0u;
    m_payloadPoolCurrentBlock = 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<GpuTaskId> GpuTaskGraph::appendTaskWithinMutation(
    const GpuTaskDesc& desc,
    const GpuTaskCommandRequirements& commands,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload,
    const usize payloadObjectSize,
    const DeclarationMutationScope& mutationAccess,
    void* const payload
){
    if(!mutationAccess.validFor(*this))
        return MakeUnexpected(Failure{});

    const b8 appendPreludeDependency = m_normalExecutionPrelude.valid() && desc.dependencyCount == 0u && !desc.scheduling.joinsAcceptedQueueFrontier && !desc.scheduling.isRecoverySubmission;
    const usize preludeDependencyCount = appendPreludeDependency ? 1u : 0u;
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || m_tasks.size() >= Limit<u32>::s_Max
        || m_dependencies.size() > Limit<u32>::s_Max - preludeDependencyCount
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size() - preludeDependencyCount
        || desc.externalDependencyCount > Limit<u32>::s_Max - m_externalDependencies.size()
        || desc.externalStateSourceCount > Limit<u32>::s_Max - m_externalStateSources.size()
        || desc.resourceUseCount > Limit<u32>::s_Max
        || desc.resourceSetUseCount > Limit<u32>::s_Max
        || desc.resourceVersionUseCount > Limit<u32>::s_Max - m_resourceVersionUses.size()
        || (desc.dependencyCount > 0u && !desc.dependencies)
        || (desc.externalDependencyCount > 0u && !desc.externalDependencies)
        || (desc.externalStateSourceCount > 0u && !desc.externalStateSources)
        || (desc.resourceUseCount > 0u && !desc.resourceUses)
        || (desc.resourceSetUseCount > 0u && !desc.resourceSetUses)
        || (desc.resourceVersionUseCount > 0u && !desc.resourceVersionUses)
        || ((payload == nullptr) != (payloadObjectSize == 0u))
    )
        return MakeUnexpected(Failure{});

    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        if(!desc.externalStateSources[sourceIndex].states || !desc.externalStateSources[sourceIndex].states->valid() || desc.externalStateSources[sourceIndex].applicableConsumerQueueClass > CommandQueue::kCount)
            return MakeUnexpected(Failure{});
    }

    usize expandedResourceUseCount = desc.resourceUseCount;
    const usize remainingResourceUseCapacity = static_cast<usize>(Limit<u32>::s_Max) - m_resourceUses.size();
    if(expandedResourceUseCount > remainingResourceUseCapacity)
        return MakeUnexpected(Failure{});
    for(usize resourceSetUseIndex = 0u; resourceSetUseIndex < desc.resourceSetUseCount; ++resourceSetUseIndex){
        const GpuTaskResourceSetUse& resourceSetUse = desc.resourceSetUses[resourceSetUseIndex];
        if(!validResourceSet(resourceSetUse.resourceSet))
            return MakeUnexpected(Failure{});
        const GpuGraphResourceSetNode& resourceSet = m_resourceSets[resourceSetUse.resourceSet.index];
        if(resourceSet.memberCount > remainingResourceUseCapacity - expandedResourceUseCount)
            return MakeUnexpected(Failure{});
        expandedResourceUseCount += resourceSet.memberCount;
    }

    Optional<GraphicsVector<GlobalUniquePtr<CommandListResourceStateHandoff>>> externalStateSnapshots;
    if(desc.externalStateSourceCount != 0u){
        auto& snapshots = externalStateSnapshots.emplace(m_arena);
        snapshots.reserve(desc.externalStateSourceCount);
        for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
            const CommandListResourceStateHandoff* const source = desc.externalStateSources[sourceIndex].states;
            GlobalUniquePtr<CommandListResourceStateHandoff> snapshot = acquireStateSnapshot();
            if(!snapshot)
                return MakeUnexpected(Failure{});
            if(!snapshot->copyFrom(*source))
                return MakeUnexpected(Failure{});
            snapshots.push_back(Move(snapshot));
        }
    }

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    if(desc.dependencyCount + preludeDependencyCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_dependencies, m_dependencies.size() + desc.dependencyCount + preludeDependencyCount);
    if(desc.externalDependencyCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_externalDependencies, m_externalDependencies.size() + desc.externalDependencyCount);
    if(desc.externalStateSourceCount != 0u){
        ContainerDetail::ReserveGrowingCapacity(m_externalStateSources, m_externalStateSources.size() + desc.externalStateSourceCount);
        ContainerDetail::ReserveGrowingCapacity(m_externalStateSnapshots, m_externalStateSnapshots.size() + desc.externalStateSourceCount);
        ContainerDetail::ReserveGrowingCapacity(m_reusableStateSnapshots, m_reusableStateSnapshots.size() + m_externalStateSnapshots.size() + m_initialOwnerHandoffStateSnapshots.size() + desc.externalStateSourceCount);
    }
    if(expandedResourceUseCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_resourceUses, m_resourceUses.size() + expandedResourceUseCount);
    if(desc.resourceVersionUseCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_resourceVersionUses, m_resourceVersionUses.size() + desc.resourceVersionUseCount);
    ContainerDetail::ReserveGrowingCapacity(m_tasks, m_tasks.size() + 1u);

    static_assert(std::is_nothrow_default_constructible_v<GpuTaskNode>, "GpuTaskNode default construction must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuTaskId>, "GpuTaskId copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuExternalCompletionId>, "GpuExternalCompletionId copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuTaskExternalStateSource>, "GpuTaskExternalStateSource copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<NotNull<CommandListResourceStateHandoff*>>, "State snapshot pointer copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuTaskResourceUse>, "GpuTaskResourceUse copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuTaskResourceVersionUse>, "GpuTaskResourceVersionUse copy must remain no-throw");

    const MarkerLabelRange markerLabelRange = appendMarkerLabel(desc.markerLabel);
    const u32 index = static_cast<u32>(m_tasks.size());
    GpuTaskNode& task = m_tasks.emplace_back();
    task.identity = desc.identity;
    task.commands = commands;
    task.scheduling = desc.scheduling;
    task.timing = desc.timing;
    task.markerLabelOffset = markerLabelRange.offset;
    task.markerLabelSize = markerLabelRange.size;
    task.dependencyOffset = static_cast<u32>(m_dependencies.size());
    task.dependencyCount = static_cast<u32>(desc.dependencyCount + preludeDependencyCount);
    task.externalDependencyOffset = static_cast<u32>(m_externalDependencies.size());
    task.externalDependencyCount = static_cast<u32>(desc.externalDependencyCount);
    task.externalStateSourceOffset = static_cast<u32>(m_externalStateSources.size());
    task.externalStateSourceCount = static_cast<u32>(desc.externalStateSourceCount);
    task.resourceUseOffset = static_cast<u32>(m_resourceUses.size());
    task.resourceUseCount = static_cast<u32>(expandedResourceUseCount);
    task.resourceVersionUseOffset = static_cast<u32>(m_resourceVersionUses.size());
    task.resourceVersionUseCount = static_cast<u32>(desc.resourceVersionUseCount);
    task.directResourceUseCount = static_cast<u32>(desc.resourceUseCount);
    task.declaredResourceSetUseCount = static_cast<u32>(desc.resourceSetUseCount);
    task.expandedResourceSetMemberUseCount = static_cast<u32>(expandedResourceUseCount - desc.resourceUseCount);
    task.payload = payload;
    task.payloadObjectSize = payloadObjectSize;
    task.recordPayload = recordPayload;
    task.acceptPayload = acceptPayload;
    task.discardPayload = discardPayload;
    task.destroyPayload = destroyPayload;
    task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration.load(MemoryOrder::relaxed);

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    if(appendPreludeDependency)
        m_dependencies.push_back(m_normalExecutionPrelude);
    for(usize externalDependencyIndex = 0u; externalDependencyIndex < desc.externalDependencyCount; ++externalDependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[externalDependencyIndex]);
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff& snapshot = *(*externalStateSnapshots)[sourceIndex];
        m_externalStateSources.push_back(GpuTaskExternalStateSource{ .states = &snapshot, .applicableConsumerQueueClass = desc.externalStateSources[sourceIndex].applicableConsumerQueueClass, });
        m_externalStateSnapshots.push_back(MakeNotNull((*externalStateSnapshots)[sourceIndex].get()));
    }
    for(usize resourceUseIndex = 0u; resourceUseIndex < desc.resourceUseCount; ++resourceUseIndex)
        m_resourceUses.push_back(desc.resourceUses[resourceUseIndex]);
    for(usize resourceVersionUseIndex = 0u; resourceVersionUseIndex < desc.resourceVersionUseCount; ++resourceVersionUseIndex)
        m_resourceVersionUses.push_back(desc.resourceVersionUses[resourceVersionUseIndex]);
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

    if(externalStateSnapshots){
        for(GlobalUniquePtr<CommandListResourceStateHandoff>& snapshot : *externalStateSnapshots)
            snapshot.release();
    }
    if(payload != nullptr)
        m_hasTaskPayloads = true;
    if(destroyPayload != nullptr)
        m_hasNonTrivialTaskPayloads = true;
    if(discardPayload != nullptr)
        m_hasDiscardCallbacks = true;
    if(desc.scheduling.joinsAcceptedQueueFrontier || desc.scheduling.isRecoverySubmission)
        m_hasAcceptedQueueFrontierOrRecoveryTask = true;
    m_publishedTaskCount = static_cast<u32>(m_tasks.size());
    bumpDeclarationRevision();
    return GpuTaskId{ .generation = m_generation, .index = index };
}

void GpuTaskGraph::discardAndDestroyUnappendedPayload(void* const payload, const GpuTaskDiscardedThunk discardPayload, const GpuTaskPayloadDestroyThunk destroyPayload){
    if(!payload)
        return;

    __hidden_gpu_task_graph_storage::UnappendedPayloadDestroyScope payloadDestroy(m_arena, payload, destroyPayload);
    if(discardPayload)
        discardPayload(payload);
}

Expected<GpuGraphResourceId> GpuTaskGraph::appendResourceWithinMutation(const GpuGraphResourceDesc& desc, const ResourceQueueAdmissionSnapshot* const queueAdmission, const ResourceBinding& binding, const DeclarationMutationScope& mutationAccess){
    if(!mutationAccess.validFor(*this))
        return MakeUnexpected(Failure{});

    const usize queueFamilyIndexCount = queueAdmission ? queueAdmission->queueFamilyIndexCount : 0u;
    const b8 hasInitialOwnerHandoff = desc.initialOwnerHandoffSources != nullptr || desc.initialOwnerHandoffSourceCount != 0u;
    const b8 hasExternalFinalRelease = desc.externalFinalReleaseDestinationQueue.valid();
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || desc.type >= GpuGraphResourceType::Count
        || !ResourceQueueSharing::IsValid(desc.queueSharing)
        || (queueAdmission && (!queueAdmission->valid() || queueAdmission->admittedQueueClasses != desc.queueSharing || queueFamilyIndexCount > static_cast<usize>(Limit<u32>::s_Max) - m_queueFamilyIndices.size()))
        || (desc.initialAvailabilityCompletion.valid() && (desc.type == GpuGraphResourceType::HazardDomain || !validExternalCompletion(desc.initialAvailabilityCompletion)))
        || (desc.externalFinalState != ResourceStates::Unknown && desc.type != GpuGraphResourceType::Texture && desc.type != GpuGraphResourceType::Buffer && desc.type != GpuGraphResourceType::AccelStruct)
        || (
            hasExternalFinalRelease
            && (
                desc.externalFinalState == ResourceStates::Unknown
                || desc.queueSharing != ResourceQueueSharing::Exclusive
                || (desc.type != GpuGraphResourceType::Texture && desc.type != GpuGraphResourceType::Buffer && desc.type != GpuGraphResourceType::AccelStruct)
            )
        )
        || (
            hasInitialOwnerHandoff
            && (
                (desc.type != GpuGraphResourceType::Texture && desc.type != GpuGraphResourceType::Buffer && desc.type != GpuGraphResourceType::AccelStruct)
                || !desc.initialOwnerHandoffSources
                || desc.initialOwnerHandoffSourceCount == 0u
                || desc.initialOwnerQueue.valid()
                || desc.initialState == ResourceStates::Unknown
                || desc.queueSharing != ResourceQueueSharing::Exclusive
                || desc.initialOwnerHandoffSourceCount
                    > static_cast<usize>(Limit<u32>::s_Max) - m_initialOwnerHandoffSources.size()
            )
        )
        || (desc.initialOwnerQueue.valid() && desc.type != GpuGraphResourceType::Texture && desc.type != GpuGraphResourceType::Buffer && desc.type != GpuGraphResourceType::AccelStruct)
        || m_resources.size() >= Limit<u32>::s_Max
    )
        return MakeUnexpected(Failure{});

    if(hasInitialOwnerHandoff){
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            if(
                (desc.type == GpuGraphResourceType::Texture && !source.range.textureSubresources.hasExtent())
                || (desc.type == GpuGraphResourceType::Buffer && !source.range.bufferRange.hasExtent())
                || (desc.type == GpuGraphResourceType::AccelStruct && (source.range.textureSubresources != s_AllSubresources || source.range.bufferRange != s_EntireBuffer))
                || !source.sourceQueue.valid()
                || !source.destinationQueue.valid()
                || !source.completion.valid()
                || !validExternalCompletion(source.completion)
                || !source.minimumCompletionToken.valid()
                || !source.minimumCompletionToken.matchesPhysicalQueue(source.sourceQueue.index, source.sourceQueue.deviceGeneration)
                || !source.stateSource
                || !source.stateSource->valid()
            )
                return MakeUnexpected(Failure{});
            for(usize previousSourceIndex = 0u; previousSourceIndex < sourceIndex; ++previousSourceIndex){
                const GpuGraphInitialOwnerHandoffSourceDesc& previousSource = desc.initialOwnerHandoffSources[previousSourceIndex];
                if(source.completion == previousSource.completion && source.sourceQueue != previousSource.sourceQueue)
                    return MakeUnexpected(Failure{});
                const b8 overlaps = desc.type == GpuGraphResourceType::Texture ? source.range.textureSubresources.overlaps(previousSource.range.textureSubresources) : desc.type == GpuGraphResourceType::AccelStruct
                        || source.range.bufferRange.overlaps(previousSource.range.bufferRange)
                ;
                if(overlaps)
                    return MakeUnexpected(Failure{});
            }
        }
    }

    const usize initialOwnerHandoffSourceOffset = m_initialOwnerHandoffSources.size();
    Optional<GraphicsVector<GlobalUniquePtr<CommandListResourceStateHandoff>>> initialOwnerHandoffStateSnapshots;
    if(hasInitialOwnerHandoff){
        auto& snapshots = initialOwnerHandoffStateSnapshots.emplace(m_arena);
        snapshots.reserve(desc.initialOwnerHandoffSourceCount);
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            GlobalUniquePtr<CommandListResourceStateHandoff> stateSnapshot = acquireStateSnapshot();
            if(!stateSnapshot)
                return MakeUnexpected(Failure{});
            if(!stateSnapshot->copyFrom(*source.stateSource))
                return MakeUnexpected(Failure{});
            snapshots.push_back(Move(stateSnapshot));
        }
    }

    ResourcePointerKey pointerKey{ nullptr, desc.type };
    if(binding.texture)
        pointerKey.pointer = binding.texture->get();
    else if(binding.buffer)
        pointerKey.pointer = binding.buffer->get();
    else if(binding.accelStruct)
        pointerKey.pointer = binding.accelStruct->get();
    m_resourceImportIndexes.prepareResourceIndexes(m_resources, pointerKey);

    if(hasInitialOwnerHandoff){
        ContainerDetail::ReserveGrowingCapacity(m_initialOwnerHandoffSources, m_initialOwnerHandoffSources.size() + desc.initialOwnerHandoffSourceCount);
        ContainerDetail::ReserveGrowingCapacity(m_initialOwnerHandoffStateSnapshots, m_initialOwnerHandoffStateSnapshots.size() + desc.initialOwnerHandoffSourceCount);
        ContainerDetail::ReserveGrowingCapacity(m_reusableStateSnapshots, m_reusableStateSnapshots.size() + m_externalStateSnapshots.size() + m_initialOwnerHandoffStateSnapshots.size() + desc.initialOwnerHandoffSourceCount);
    }
    if(queueFamilyIndexCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_queueFamilyIndices, m_queueFamilyIndices.size() + queueFamilyIndexCount);
    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_resources, m_resources.size() + 1u);

    static_assert(std::is_nothrow_default_constructible_v<GpuGraphResourceNode>, "GpuGraphResourceNode default construction must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<GpuTaskGraphInitialOwnerHandoffSourceView>, "Initial owner handoff copy must remain no-throw");
    static_assert(std::is_nothrow_copy_constructible_v<NotNull<CommandListResourceStateHandoff*>>, "State snapshot pointer copy must remain no-throw");

    const usize prevInitialOwnerSourceCount = m_initialOwnerHandoffSources.size();
    const usize prevInitialOwnerSnapshotCount = m_initialOwnerHandoffStateSnapshots.size();
    const usize prevQueueFamilyIndexCount = m_queueFamilyIndices.size();
    const usize prevMarkerTextSize = m_markerText.size();
    const usize prevResourceCount = m_resources.size();

    const MarkerLabelRange markerLabelRange = appendMarkerLabel(desc.markerLabel);

    for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
        const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
        m_initialOwnerHandoffSources.push_back(GpuTaskGraphInitialOwnerHandoffSourceView{
            .range = source.range,
            .sourceQueue = source.sourceQueue,
            .destinationQueue = source.destinationQueue,
            .completion = source.completion,
            .minimumCompletionToken = source.minimumCompletionToken,
            .stateSource = (*initialOwnerHandoffStateSnapshots)[sourceIndex].get(),
        });
        m_initialOwnerHandoffStateSnapshots.push_back(MakeNotNull((*initialOwnerHandoffStateSnapshots)[sourceIndex].get()));
    }

    const u32 index = static_cast<u32>(m_resources.size());
    GpuGraphResourceNode& resource = m_resources.emplace_back();
    resource.identity = desc.identity;
    if(binding.texture)
        resource.texture = *binding.texture;
    else if(binding.buffer)
        resource.buffer = *binding.buffer;
    else if(binding.accelStruct)
        resource.accelStruct = *binding.accelStruct;
    resource.deviceGeneration = binding.deviceGeneration;
    resource.type = desc.type;
    resource.initialState = desc.initialState;
    resource.externalFinalState = desc.externalFinalState;
    resource.externalFinalReleaseDestinationQueue = desc.externalFinalReleaseDestinationQueue;
    resource.initialOwnerQueue = desc.initialOwnerQueue;
    resource.initialAvailabilityCompletion = desc.initialAvailabilityCompletion;
    resource.directConsumerQueue = desc.directConsumerQueue;
    resource.initialOwnerHandoffSourceOffset = static_cast<u32>(initialOwnerHandoffSourceOffset);
    resource.initialOwnerHandoffSourceCount = static_cast<u32>(desc.initialOwnerHandoffSourceCount);
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelRange.offset;
    resource.markerLabelSize = markerLabelRange.size;
    if(queueAdmission){
        resource.queueFamilyIndexOffset = static_cast<u32>(m_queueFamilyIndices.size());
        resource.queueFamilyIndexCount = queueAdmission->queueFamilyIndexCount;
        resource.usesConcurrentSharing = queueAdmission->usesConcurrentSharing;
        resource.hasQueueAdmission = true;
        for(usize queueFamilyIndex = 0u; queueFamilyIndex < queueFamilyIndexCount; ++queueFamilyIndex)
            m_queueFamilyIndices.push_back(queueAdmission->queueFamilyIndices[queueFamilyIndex]);
    }

    const NameHash& identityKey = desc.identity.identityHash();
    GpuGraphResourceImportIndex::Publication publication;
    if(!m_resourceImportIndexes.publishResource(identityKey, pointerKey, index, publication)){
        m_resourceImportIndexes.rollbackResource(identityKey, pointerKey, publication);
        const auto rollbackTail = [](auto& container, const usize prevSize)noexcept{
            while(container.size() > prevSize)
                container.pop_back();
        };
        rollbackTail(m_resources, prevResourceCount);
        rollbackTail(m_markerText, prevMarkerTextSize);
        rollbackTail(m_queueFamilyIndices, prevQueueFamilyIndexCount);
        rollbackTail(m_initialOwnerHandoffStateSnapshots, prevInitialOwnerSnapshotCount);
        rollbackTail(m_initialOwnerHandoffSources, prevInitialOwnerSourceCount);
        return MakeUnexpected(Failure{});
    }
    if(initialOwnerHandoffStateSnapshots){
        for(GlobalUniquePtr<CommandListResourceStateHandoff>& snapshot : *initialOwnerHandoffStateSnapshots)
            snapshot.release();
    }
    m_publishedResourceCount = static_cast<u32>(m_resources.size());
    bumpDeclarationRevision();
    return GpuGraphResourceId{ .generation = m_generation, .index = index };
}

Expected<GpuGraphResourceVersionId> GpuTaskGraph::appendResourceVersion(const GpuGraphResourceVersionDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(m_resourceVersions.size() >= Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});

    GpuGraphResourceVersionNode version;
    version.resource = desc.resource;
    version.range = desc.range;
    version.origin = desc.origin;

    const u32 index = static_cast<u32>(m_resourceVersions.size());
    m_resourceVersions.push_back(version);
    bumpDeclarationRevision();
    return GpuGraphResourceVersionId{ .generation = m_generation, .index = index };
}

Expected<GpuGraphResourceSetId> GpuTaskGraph::appendResourceSet(const GpuGraphResourceSetDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || (desc.memberCount != 0u && !desc.members)
        || desc.memberCount > static_cast<usize>(Limit<u32>::s_Max) - m_resourceSetMembers.size()
        || m_resourceSets.size() >= Limit<u32>::s_Max
    )
        return MakeUnexpected(Failure{});

    {
        constexpr usize s_InlineMemberCount = 32u;
        Array<u32, s_InlineMemberCount> inlineMembers;
        Optional<HashSet<u32, Alloc::ScratchArena>> indexedMembers;
        for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
            const GpuGraphResourceId member = desc.members[memberIndex];
            if(!validResource(member))
                return MakeUnexpected(Failure{});

            if(memberIndex < s_InlineMemberCount){
                for(usize previousMemberIndex = 0u; previousMemberIndex < memberIndex; ++previousMemberIndex){
                    if(inlineMembers[previousMemberIndex] == member.index)
                        return MakeUnexpected(Failure{});
                }
                inlineMembers[memberIndex] = member.index;
            }
            else{
                if(!indexedMembers){
                    indexedMembers.emplace(AddSize(desc.memberCount, desc.memberCount), m_declarationScratch);
                    indexedMembers->insert(inlineMembers.begin(), inlineMembers.end());
                }
                if(!indexedMembers->insert(member.index).second)
                    return MakeUnexpected(Failure{});
            }
        }
    }

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    if(desc.memberCount != 0u)
        ContainerDetail::ReserveGrowingCapacity(m_resourceSetMembers, m_resourceSetMembers.size() + desc.memberCount);
    ContainerDetail::ReserveGrowingCapacity(m_resourceSets, m_resourceSets.size() + 1u);
    m_resourceImportIndexes.prepareResourceSetIndex(m_resourceSets);

    AppendedContainerRollbackScope markerRollback(m_markerText, __hidden_gpu_task_graph_storage::GpuAppendRollbackFailure{});
    AppendedContainerRollbackScope memberRollback(m_resourceSetMembers, __hidden_gpu_task_graph_storage::GpuAppendRollbackFailure{});
    AppendedContainerRollbackScope setRollback(m_resourceSets, __hidden_gpu_task_graph_storage::GpuAppendRollbackFailure{});

    const MarkerLabelRange markerLabelRange = appendMarkerLabel(desc.markerLabel);

    GpuGraphResourceSetNode resourceSet;
    resourceSet.identity = desc.identity;
    resourceSet.markerLabelOffset = markerLabelRange.offset;
    resourceSet.markerLabelSize = markerLabelRange.size;
    resourceSet.memberOffset = static_cast<u32>(m_resourceSetMembers.size());
    resourceSet.memberCount = static_cast<u32>(desc.memberCount);
    if(desc.memberCount != 0u)
        m_resourceSetMembers.insert(m_resourceSetMembers.end(), desc.members, desc.members + desc.memberCount);

    const NameHash& identityKey = desc.identity.identityHash();
    b8 identityInserted = false;
    ScopeExit indexRollback([&]()noexcept{ m_resourceImportIndexes.rollbackResourceSet(identityKey, identityInserted); });
    const u32 index = static_cast<u32>(m_resourceSets.size());
    m_resourceSets.emplace_back(Move(resourceSet));
    if(!m_resourceImportIndexes.publishResourceSet(identityKey, index, identityInserted))
        return MakeUnexpected(Failure{});
    markerRollback.commit();
    memberRollback.commit();
    setRollback.commit();
    indexRollback.release();
    bumpDeclarationRevision();
    return GpuGraphResourceSetId{ .generation = m_generation, .index = index };
}

Expected<GpuExternalCompletionId> GpuTaskGraph::appendExternalCompletion(const GpuExternalCompletionDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return MakeUnexpected(Failure{});

    if(!desc.identity || desc.markerLabel.empty() || desc.markerLabel.size() > Limit<u32>::s_Max || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size() || m_externalCompletions.size() >= Limit<u32>::s_Max)
        return MakeUnexpected(Failure{});

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_externalCompletions, m_externalCompletions.size() + 1u);
    prepareExternalCompletionIndex();

    AppendedContainerRollbackScope markerRollback(m_markerText, __hidden_gpu_task_graph_storage::GpuAppendRollbackFailure{});
    AppendedContainerRollbackScope completionRollback(m_externalCompletions, __hidden_gpu_task_graph_storage::GpuAppendRollbackFailure{});

    const MarkerLabelRange markerLabelRange = appendMarkerLabel(desc.markerLabel);

    GpuExternalCompletionNode completion;
    completion.identity = desc.identity;
    completion.token = desc.token;
    completion.markerLabelOffset = markerLabelRange.offset;
    completion.markerLabelSize = markerLabelRange.size;
    completion.hasToken = desc.token.valid() && desc.token.hasPhysicalQueueIdentity();
    const u16 tokenDeviceGeneration = completion.hasToken ? desc.token.deviceGeneration : 0u;

    const NameHash& identityKey = desc.identity.identityHash();
    b8 identityInserted = false;
    ScopeExit indexRollback([&]()noexcept{
        if(identityInserted)
            m_externalCompletionIdentityIndex->erase(identityKey);
    });
    const u32 index = static_cast<u32>(m_externalCompletions.size());
    m_externalCompletions.emplace_back(Move(completion));
    if(m_externalCompletionIdentityIndex){
        identityInserted = m_externalCompletionIdentityIndex->emplace(identityKey, index).second;
        if(!identityInserted)
            return MakeUnexpected(Failure{});
    }
    markerRollback.commit();
    completionRollback.commit();
    indexRollback.release();
    if(tokenDeviceGeneration != 0u)
        m_externalCompletionDeviceGeneration = tokenDeviceGeneration;
    bumpDeclarationRevision();
    return GpuExternalCompletionId{ .generation = m_generation, .index = index };
}

GlobalUniquePtr<CommandListResourceStateHandoff> GpuTaskGraph::acquireStateSnapshot(){
    if(!m_reusableStateSnapshots.empty()){
        const NotNull<CommandListResourceStateHandoff*> reused = m_reusableStateSnapshots.back();
        m_reusableStateSnapshots.pop_back();
        reused->reset();
        return GlobalUniquePtr<CommandListResourceStateHandoff>(reused.get(), typename GlobalUniquePtr<CommandListResourceStateHandoff>::deleter_type(m_arena));
    }
    return MakeGlobalUnique<CommandListResourceStateHandoff>(m_arena, m_arena);
}

const GpuTaskGraph::GpuUploadBlobNode* GpuTaskGraph::findUploadBlob(const GpuUploadBlobId& blob)const noexcept{
    if(!validUploadBlob(blob))
        return nullptr;
    return &m_uploadBlobs[blob.index];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::MarkerLabelRange GpuTaskGraph::appendMarkerLabel(const AStringView text){
    FOUNDATION_ASSERT(!text.empty() && text.size() <= Limit<u32>::s_Max - m_markerText.size());

    const MarkerLabelRange range{ static_cast<u32>(m_markerText.size()), static_cast<u32>(text.size()) };
    const u8* const bytes = reinterpret_cast<const u8*>(text.data());
    m_markerText.insert(m_markerText.end(), bytes, bytes + text.size());
    return range;
}

AStringView GpuTaskGraph::markerLabel(const u32 offset, const u32 size)const noexcept{
    FOUNDATION_ASSERT(offset <= m_markerText.size());
    FOUNDATION_ASSERT(size <= m_markerText.size() - offset);
    return AStringView(reinterpret_cast<const char*>(m_markerText.data() + offset), size);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

