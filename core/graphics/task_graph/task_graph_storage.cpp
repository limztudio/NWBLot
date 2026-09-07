// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/rhi/command.h>
#include <global/allocation_size.h>
#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_storage{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UnappendedPayloadDestroyScope final : NoCopy{
public:
    UnappendedPayloadDestroyScope(
        GraphicsArena& arena,
        void* const payload,
        const GpuTaskPayloadDestroyThunk destroyPayload
    )noexcept
        : m_arena(arena)
        , m_payload(payload)
        , m_destroyPayload(destroyPayload)
    {}
    ~UnappendedPayloadDestroyScope(){
        if(m_payload && m_destroyPayload)
            m_destroyPayload(m_arena, m_payload);
    }


private:
    GraphicsArena& m_arena;
    void* m_payload = nullptr;
    GpuTaskPayloadDestroyThunk m_destroyPayload = nullptr;
};

template<typename ContainerT>
class AppendedContainerRollbackScope final : NoCopy{
public:
    explicit AppendedContainerRollbackScope(ContainerT& container)noexcept
        : m_container(container)
        , m_initialSize(container.size())
    {}
    ~AppendedContainerRollbackScope()noexcept{
        static_assert(noexcept(m_container.size()));
        static_assert(noexcept(m_container.pop_back()));
        if(m_committed)
            return;
        if(m_container.size() < m_initialSize){
            NWB_FATAL_ASSERT_MSG(false, "GPU graph declaration rollback cannot restore removed storage");
            TerminateInvariant();
        }
        while(m_container.size() > m_initialSize)
            m_container.pop_back();
    }


public:
    void commit()noexcept{ m_committed = true; }


private:
    ContainerT& m_container;
    usize m_initialSize = 0u;
    bool m_committed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::TaskPayloadDestroyScope::TaskPayloadDestroyScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::TaskPayloadDestroyScope::~TaskPayloadDestroyScope(){
    if(m_active)
        m_graph.destroyTaskPayloadObjects();
}

void GpuTaskGraph::TaskPayloadDestroyScope::activateWithinLock()noexcept{
    if(m_active){
        NWB_FATAL_ASSERT_MSG(false, "GPU task payload destruction ownership cannot be activated twice");
        TerminateInvariant();
    }
    m_active = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::appendTaskWithinMutation(
    const GpuTaskDesc& desc,
    void* const payload,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload,
    const usize payloadObjectSize,
    const DeclarationMutationScope& mutationAccess
){
    if(!mutationAccess.validFor(*this))
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || m_tasks.size() >= Limit<u32>::s_Max
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size()
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
        return {};

    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        if(
            !desc.externalStateSources[sourceIndex].states
            || !desc.externalStateSources[sourceIndex].states->valid()
            || desc.externalStateSources[sourceIndex].applicableConsumerQueueClass > CommandQueue::kCount
        )
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

    // Task declarations capture every valid native handoff when the graph accepts the declaration. Invalid sources
    // cannot become valid after this immutable copy, so reject them at the declaration boundary.
    Optional<GraphicsVector<GlobalUniquePtr<CommandListResourceStateHandoff>>> externalStateSnapshots;
    if(desc.externalStateSourceCount != 0u){
        auto& snapshots = externalStateSnapshots.emplace(m_arena);
        snapshots.reserve(desc.externalStateSourceCount);
        for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
            const CommandListResourceStateHandoff* const source = desc.externalStateSources[sourceIndex].states;
            GlobalUniquePtr<CommandListResourceStateHandoff> snapshot =
                MakeGlobalUnique<CommandListResourceStateHandoff>(m_arena, m_arena)
            ;
            if(!snapshot)
                return {};
            if(!snapshot->copyFrom(*source))
                return {};
            snapshots.push_back(Move(snapshot));
        }
    }

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_dependencies, m_dependencies.size() + desc.dependencyCount);
    ContainerDetail::ReserveGrowingCapacity(
        m_externalDependencies,
        m_externalDependencies.size() + desc.externalDependencyCount
    );
    ContainerDetail::ReserveGrowingCapacity(
        m_externalStateSources,
        m_externalStateSources.size() + desc.externalStateSourceCount
    );
    ContainerDetail::ReserveGrowingCapacity(
        m_externalStateSnapshots,
        m_externalStateSnapshots.size() + desc.externalStateSourceCount
    );
    ContainerDetail::ReserveGrowingCapacity(m_resourceUses, m_resourceUses.size() + expandedResourceUseCount);
    ContainerDetail::ReserveGrowingCapacity(m_resourceVersionUses, m_resourceVersionUses.size() + desc.resourceVersionUseCount);
    ContainerDetail::ReserveGrowingCapacity(m_tasks, m_tasks.size() + 1u);

    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope markerRollback(m_markerText);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope dependencyRollback(m_dependencies);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope externalDependencyRollback(m_externalDependencies);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope externalStateSourceRollback(m_externalStateSources);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope externalStateSnapshotRollback(m_externalStateSnapshots);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope resourceUseRollback(m_resourceUses);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope resourceVersionUseRollback(m_resourceVersionUses);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope taskRollback(m_tasks);

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

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
    task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration;

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    for(usize dependencyIndex = 0u; dependencyIndex < desc.externalDependencyCount; ++dependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[dependencyIndex]);
    for(usize sourceIndex = 0u; sourceIndex < desc.externalStateSourceCount; ++sourceIndex){
        const CommandListResourceStateHandoff* const snapshot = (*externalStateSnapshots)[sourceIndex].get();
        m_externalStateSources.push_back(GpuTaskExternalStateSource{
            .states = snapshot,
            .applicableConsumerQueueClass = desc.externalStateSources[sourceIndex].applicableConsumerQueueClass,
        });
        m_externalStateSnapshots.push_back((*externalStateSnapshots)[sourceIndex].get());
    }
    for(usize useIndex = 0u; useIndex < desc.resourceUseCount; ++useIndex)
        m_resourceUses.push_back(desc.resourceUses[useIndex]);
    for(usize useIndex = 0u; useIndex < desc.resourceVersionUseCount; ++useIndex)
        m_resourceVersionUses.push_back(desc.resourceVersionUses[useIndex]);
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
    if(externalStateSnapshots){
        for(GlobalUniquePtr<CommandListResourceStateHandoff>& snapshot : *externalStateSnapshots)
            snapshot.release();
    }
    markerRollback.commit();
    dependencyRollback.commit();
    externalDependencyRollback.commit();
    externalStateSourceRollback.commit();
    externalStateSnapshotRollback.commit();
    resourceUseRollback.commit();
    resourceVersionUseRollback.commit();
    taskRollback.commit();
    m_declarationRevision = allocateGeneration();
    return GpuTaskId{ index, m_generation };
}

void GpuTaskGraph::discardAndDestroyUnappendedPayload(
    void* const payload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
){
    if(!payload)
        return;

    __hidden_gpu_task_graph_storage::UnappendedPayloadDestroyScope payloadDestroy(m_arena, payload, destroyPayload);
    if(discardPayload)
        discardPayload(payload);
}

GpuGraphResourceId GpuTaskGraph::appendResourceWithinMutation(
    const GpuGraphResourceDesc& desc,
    const ResourceQueueAdmissionSnapshot* const queueAdmission,
    const ResourceBinding& binding,
    const DeclarationMutationScope& mutationAccess
){
    if(!mutationAccess.validFor(*this))
        return {};

    const usize queueFamilyIndexCount = queueAdmission ? queueAdmission->queueFamilyIndexCount : 0u;
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
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || desc.type >= GpuGraphResourceType::kCount
        || !ResourceQueueSharing::IsValid(desc.queueSharing)
        || (
            queueAdmission
            && (
                !queueAdmission->valid()
                || queueAdmission->admittedQueueClasses != desc.queueSharing
                || queueFamilyIndexCount > static_cast<usize>(Limit<u32>::s_Max) - m_queueFamilyIndices.size()
            )
        )
        || (
            desc.initialAvailabilityCompletion.valid()
            && (
                desc.type == GpuGraphResourceType::HazardDomain
                || !validExternalCompletion(desc.initialAvailabilityCompletion)
            )
        )
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
                || desc.queueSharing != ResourceQueueSharing::Exclusive
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
                || !desc.initialOwnerStateSource->valid()
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
                !source.range.textureSubresources.hasExtent()
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
                || !source.stateSource->valid()
            )
                return {};
            for(usize previousSourceIndex = 0u; previousSourceIndex < sourceIndex; ++previousSourceIndex){
                const GpuGraphInitialOwnerHandoffSourceDesc& previousSource =
                    desc.initialOwnerHandoffSources[previousSourceIndex]
                ;
                // One completion binding supplies one physical-queue timeline token at submission. Reusing it for
                // sources from different queues would make a later graph impossible to submit safely: one acquire
                // would necessarily wait on the wrong producer frontier.
                if(
                    source.completion == previousSource.completion
                    && source.sourceQueue != previousSource.sourceQueue
                )
                    return {};
                if(source.range.textureSubresources.overlaps(previousSource.range.textureSubresources))
                    return {};
            }
        }
    }

    // Initial-owner imports freeze the valid producer snapshot while the resource is declared, so later recording
    // never depends on producer-owned storage or a state source that could not become usable after publication.
    GlobalUniquePtr<CommandListResourceStateHandoff> initialOwnerStateSnapshot;
    if(desc.initialOwnerStateSource){
        initialOwnerStateSnapshot = MakeGlobalUnique<CommandListResourceStateHandoff>(m_arena, m_arena);
        if(!initialOwnerStateSnapshot)
            return {};
        if(!initialOwnerStateSnapshot->copyFrom(*desc.initialOwnerStateSource))
            return {};
    }

    const usize initialOwnerHandoffSourceOffset = m_initialOwnerHandoffSources.size();
    Optional<GraphicsVector<GlobalUniquePtr<CommandListResourceStateHandoff>>> initialOwnerHandoffStateSnapshots;
    if(hasMultiInitialOwnerHandoff){
        auto& snapshots = initialOwnerHandoffStateSnapshots.emplace(m_arena);
        snapshots.reserve(desc.initialOwnerHandoffSourceCount);
        for(usize sourceIndex = 0u; sourceIndex < desc.initialOwnerHandoffSourceCount; ++sourceIndex){
            const GpuGraphInitialOwnerHandoffSourceDesc& source = desc.initialOwnerHandoffSources[sourceIndex];
            GlobalUniquePtr<CommandListResourceStateHandoff> stateSnapshot =
                MakeGlobalUnique<CommandListResourceStateHandoff>(m_arena, m_arena)
            ;
            if(!stateSnapshot)
                return {};
            if(!stateSnapshot->copyFrom(*source.stateSource))
                return {};
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
    prepareResourceIndexes(pointerKey);

    ContainerDetail::ReserveGrowingCapacity(
        m_initialOwnerHandoffSources,
        m_initialOwnerHandoffSources.size() + desc.initialOwnerHandoffSourceCount
    );
    ContainerDetail::ReserveGrowingCapacity(m_queueFamilyIndices, m_queueFamilyIndices.size() + queueFamilyIndexCount);
    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_resources, m_resources.size() + 1u);

    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope initialOwnerSourceRollback(
        m_initialOwnerHandoffSources
    );
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope queueFamilyRollback(m_queueFamilyIndices);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope markerRollback(m_markerText);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope resourceRollback(m_resources);

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

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
    }

    GpuGraphResourceNode resource;
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
    resource.initialOwnerReleaseDestinationQueue = desc.initialOwnerReleaseDestinationQueue;
    resource.initialOwnerCompletion = desc.initialOwnerCompletion;
    resource.initialOwnerMinimumCompletionToken = desc.initialOwnerMinimumCompletionToken;
    resource.initialAvailabilityCompletion = desc.initialAvailabilityCompletion;
    resource.initialOwnerStateSource = initialOwnerStateSnapshot.get();
    resource.initialOwnerHandoffSourceOffset = static_cast<u32>(initialOwnerHandoffSourceOffset);
    resource.initialOwnerHandoffSourceCount = static_cast<u32>(desc.initialOwnerHandoffSourceCount);
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelOffset;
    resource.markerLabelSize = markerLabelSize;
    if(queueAdmission){
        resource.queueFamilyIndexOffset = static_cast<u32>(m_queueFamilyIndices.size());
        resource.queueFamilyIndexCount = queueAdmission->queueFamilyIndexCount;
        resource.usesConcurrentSharing = queueAdmission->usesConcurrentSharing;
        resource.hasQueueAdmission = true;
        for(usize queueFamilyIndex = 0u; queueFamilyIndex < queueFamilyIndexCount; ++queueFamilyIndex)
            m_queueFamilyIndices.push_back(queueAdmission->queueFamilyIndices[queueFamilyIndex]);
    }

    const NameHash& identityKey = desc.identity.identityHash();
    bool identityInserted = false;
    bool pointerInserted = false;
    ScopeExit indexRollback([&]()noexcept{
        if(pointerInserted)
            m_resourcePointerIndex->erase(pointerKey);
        if(identityInserted)
            m_resourceIdentityIndex->erase(identityKey);
    });
    const u32 index = static_cast<u32>(m_resources.size());
    m_resources.push_back(Move(resource));
    if(m_resourceIdentityIndex){
        identityInserted = m_resourceIdentityIndex->emplace(identityKey, index).second;
        if(!identityInserted)
            return {};
        if(pointerKey.pointer){
            pointerInserted = m_resourcePointerIndex->emplace(pointerKey, index).second;
            if(!pointerInserted)
                return {};
        }
    }
    initialOwnerStateSnapshot.release();
    if(initialOwnerHandoffStateSnapshots){
        for(GlobalUniquePtr<CommandListResourceStateHandoff>& snapshot : *initialOwnerHandoffStateSnapshots)
            snapshot.release();
    }
    initialOwnerSourceRollback.commit();
    queueFamilyRollback.commit();
    markerRollback.commit();
    resourceRollback.commit();
    indexRollback.release();
    m_declarationRevision = allocateGeneration();
    return GpuGraphResourceId{ index, m_generation };
}

GpuGraphResourceVersionId GpuTaskGraph::appendResourceVersion(const GpuGraphResourceVersionDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(m_resourceVersions.size() >= Limit<u32>::s_Max)
        return {};

    GpuGraphResourceVersionNode version;
    version.resource = desc.resource;
    version.range = desc.range;
    version.origin = desc.origin;

    const u32 index = static_cast<u32>(m_resourceVersions.size());
    m_resourceVersions.push_back(version);
    m_declarationRevision = allocateGeneration();
    return GpuGraphResourceVersionId{ index, m_generation };
}

GpuGraphResourceSetId GpuTaskGraph::appendResourceSet(const GpuGraphResourceSetDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || (desc.memberCount != 0u && !desc.members)
        || desc.memberCount > static_cast<usize>(Limit<u32>::s_Max) - m_resourceSetMembers.size()
        || m_resourceSets.size() >= Limit<u32>::s_Max
    )
        return {};

    {
        constexpr usize s_InlineMemberCount = 32u;
        u32 inlineMembers[s_InlineMemberCount];
        Optional<HashSet<u32, Alloc::ScratchArena>> indexedMembers;
        for(usize memberIndex = 0u; memberIndex < desc.memberCount; ++memberIndex){
            const GpuGraphResourceId member = desc.members[memberIndex];
            if(!validResource(member))
                return {};

            // Validity establishes this graph generation before index-only membership is used.
            if(memberIndex < s_InlineMemberCount){
                for(usize previousMemberIndex = 0u; previousMemberIndex < memberIndex; ++previousMemberIndex){
                    if(inlineMembers[previousMemberIndex] == member.index)
                        return {};
                }
                inlineMembers[memberIndex] = member.index;
            }
            else{
                if(!indexedMembers){
                    // Direct capacity construction avoids an empty-table proxy allocation and later rehash.
                    indexedMembers.emplace(AddSize(desc.memberCount, desc.memberCount), m_declarationScratch);
                    indexedMembers->insert(inlineMembers, inlineMembers + s_InlineMemberCount);
                }
                if(!indexedMembers->insert(member.index).second)
                    return {};
            }
        }
    }

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_resourceSetMembers, m_resourceSetMembers.size() + desc.memberCount);
    ContainerDetail::ReserveGrowingCapacity(m_resourceSets, m_resourceSets.size() + 1u);
    prepareResourceSetIndex();

    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope markerRollback(m_markerText);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope memberRollback(m_resourceSetMembers);
    __hidden_gpu_task_graph_storage::AppendedContainerRollbackScope setRollback(m_resourceSets);

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

    const NameHash& identityKey = desc.identity.identityHash();
    bool identityInserted = false;
    ScopeExit indexRollback([&]()noexcept{
        if(identityInserted)
            m_resourceSetIdentityIndex->erase(identityKey);
    });
    const u32 index = static_cast<u32>(m_resourceSets.size());
    m_resourceSets.push_back(Move(resourceSet));
    if(m_resourceSetIdentityIndex){
        identityInserted = m_resourceSetIdentityIndex->emplace(identityKey, index).second;
        if(!identityInserted)
            return {};
    }
    markerRollback.commit();
    memberRollback.commit();
    setRollback.commit();
    indexRollback.release();
    m_declarationRevision = allocateGeneration();
    return GpuGraphResourceSetId{ index, m_generation };
}

GpuGraphPipelineId GpuTaskGraph::appendPipeline(const GpuGraphPipelineDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || desc.type >= GpuGraphPipelineType::kCount
        || m_pipelines.size() >= Limit<u32>::s_Max
    )
        return {};

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_pipelines, m_pipelines.size() + 1u);

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
    m_declarationRevision = allocateGeneration();
    return GpuGraphPipelineId{ index, m_generation };
}

GpuExternalCompletionId GpuTaskGraph::appendExternalCompletion(const GpuExternalCompletionDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.markerLabel.size() > Limit<u32>::s_Max
        || desc.markerLabel.size() > Limit<u32>::s_Max - m_markerText.size()
        || m_externalCompletions.size() >= Limit<u32>::s_Max
    )
        return {};

    ContainerDetail::ReserveGrowingCapacity(m_markerText, m_markerText.size() + desc.markerLabel.size());
    ContainerDetail::ReserveGrowingCapacity(m_externalCompletions, m_externalCompletions.size() + 1u);

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuExternalCompletionNode completion;
    completion.identity = desc.identity;
    completion.token = desc.token;
    completion.markerLabelOffset = markerLabelOffset;
    completion.markerLabelSize = markerLabelSize;
    completion.hasToken = desc.token.valid() && desc.token.hasPhysicalQueueIdentity();

    const u32 index = static_cast<u32>(m_externalCompletions.size());
    m_externalCompletions.push_back(Move(completion));
    m_declarationRevision = allocateGeneration();
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

bool GpuTaskGraph::destroyTaskPayloads(){
    DiscardNotificationScope notification(*this);
    TaskPayloadDestroyScope payloadDestroy(*this);
    u64 notificationGeneration = 0u;
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            !m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
                || task.lifecycleState == TaskLifecycleState::Recording
            ){
                return false;
            }
        }
        bool hasUnacceptedTask = false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            )
                hasUnacceptedTask = true;
        }
        if(hasUnacceptedTask){
            notificationGeneration = allocateGeneration();
            notification.activateWithinLock();
        }
        for(GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            ){
                task.lifecycleState = TaskLifecycleState::Discarded;
                task.recordingClaimGeneration = 0u;
                task.submissionClaimGeneration = 0u;
                task.discardNotificationGeneration = notificationGeneration;
                task.recordThunkInProgress = false;
                task.recordThunkCompleted = false;
            }
        }
        payloadDestroy.activateWithinLock();
    }

    for(GpuTaskNode& task : m_tasks){
        if(
            notificationGeneration != 0u
            && task.discardNotificationGeneration == notificationGeneration
            && task.payload
            && task.discardPayload
        )
            task.discardPayload(task.payload);
    }
    return true;
}

bool GpuTaskGraph::destroyTaskPayloadsWithoutCallbacks()noexcept{
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(
            !m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
                || task.lifecycleState == TaskLifecycleState::Recording
            )
                return false;
        }
        for(GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Declared
                || task.lifecycleState == TaskLifecycleState::Recorded
            )
                task.lifecycleState = TaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = 0u;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
    }

    destroyTaskPayloadObjects();
    return true;
}

void GpuTaskGraph::destroyTaskPayloadObjects()noexcept{
    for(GpuTaskNode& task : m_tasks){
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.recordPayload = nullptr;
        task.acceptPayload = nullptr;
        task.discardPayload = nullptr;
        task.destroyPayload = nullptr;
    }
}

void GpuTaskGraph::destroyTaskStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(CommandListResourceStateHandoff* const states : m_externalStateSnapshots)
        DestroyArenaObjectNoexcept(m_arena, states);
    m_externalStateSnapshots.clear();
}

void GpuTaskGraph::destroyResourceStateSnapshots()noexcept{
    static_assert(IsNothrowDestructible_V<CommandListResourceStateHandoff>);
    for(GpuGraphResourceNode& resource : m_resources){
        if(resource.initialOwnerStateSource)
            DestroyArenaObjectNoexcept(m_arena, resource.initialOwnerStateSource);
        resource.initialOwnerStateSource = nullptr;
    }
    for(GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(source.stateSource)
            DestroyArenaObjectNoexcept(m_arena, const_cast<CommandListResourceStateHandoff*>(source.stateSource));
        source.stateSource = nullptr;
    }
    m_initialOwnerHandoffSources.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

