// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraph::reset(){
    {
        ScopedLock lock(m_lifecycleMutex);
        if(m_teardownInProgress){
            NWB_ASSERT_MSG(false, "GpuTaskGraph::reset requires prior teardown completion");
            return;
        }
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == GpuTaskLifecycleState::Recording
                || task.lifecycleState == GpuTaskLifecycleState::Submitting
                || task.lifecycleState == GpuTaskLifecycleState::Accepting
                || task.lifecycleState == GpuTaskLifecycleState::Discarding
            ){
                NWB_ASSERT_MSG(false, "GpuTaskGraph::reset requires in-flight task work to resolve first");
                return;
            }
        }
        m_teardownInProgress = true;
    }

    if(!destroyTaskPayloads()){
        ScopedLock lock(m_lifecycleMutex);
        m_teardownInProgress = false;
        NWB_ASSERT_MSG(false, "GpuTaskGraph::reset requires in-flight task work to resolve first");
        return;
    }
    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
    {
        ScopedLock lock(m_lifecycleMutex);
        m_tasks.clear();
        m_dependencies.clear();
        m_externalDependencies.clear();
        m_externalStateSources.clear();
        m_resourceUses.clear();
        m_resources.clear();
        m_initialOwnerHandoffSources.clear();
        m_resourceSets.clear();
        m_resourceSetMembers.clear();
        m_pipelines.clear();
        m_externalCompletions.clear();
        m_uploadBlobs.clear();
        m_markerText.clear();
        m_presentEndpoint = {};
        m_generation = allocateGeneration();
        m_declarationRevision = allocateGeneration();
        m_activeRecordingAttemptGeneration = allocateGeneration();
        m_activeRecordingPlanGeneration = 0u;
        m_hasPresentEndpoint = false;
        m_teardownInProgress = false;
    }
}

u64 GpuTaskGraph::recordingAttemptGeneration()const noexcept{
    ScopedLock lock(m_lifecycleMutex);
    return m_activeRecordingAttemptGeneration;
}

bool GpuTaskGraph::beginRecordingAttempt(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet
)const noexcept{
    if(!compiledGraph.validFor(*this) || !compiledGraph.validPacket(packet))
        return false;
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    const GpuTaskId* const tasks = compiledGraph.packetTasks(packet);
    if(!tasks || packetPlan.taskCount == 0u)
        return false;

    ScopedLock lock(m_lifecycleMutex);
    if(m_teardownInProgress)
        return false;

    bool selectedTaskWasDiscarded = false;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
        const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
        if(task.lifecycleAttemptGeneration != m_activeRecordingAttemptGeneration)
            return false;
        if(
            task.lifecycleState == GpuTaskLifecycleState::Recording
            || task.lifecycleState == GpuTaskLifecycleState::Recorded
            || task.lifecycleState == GpuTaskLifecycleState::Discarding
            || task.lifecycleState == GpuTaskLifecycleState::Submitting
            || task.lifecycleState == GpuTaskLifecycleState::Accepting
            || task.lifecycleState == GpuTaskLifecycleState::Accepted
        )
            return false;
        if(task.lifecycleState == GpuTaskLifecycleState::Discarded)
            selectedTaskWasDiscarded = true;
    }

    const bool planChanged = m_activeRecordingPlanGeneration != compiledGraph.planGeneration();
    if(!planChanged && !selectedTaskWasDiscarded)
        return true;

    for(const GpuTaskNode& task : m_tasks){
        if(
            task.lifecycleAttemptGeneration != m_activeRecordingAttemptGeneration
            || task.lifecycleState == GpuTaskLifecycleState::Recording
            || task.lifecycleState == GpuTaskLifecycleState::Recorded
            || task.lifecycleState == GpuTaskLifecycleState::Discarding
            || task.lifecycleState == GpuTaskLifecycleState::Submitting
            || task.lifecycleState == GpuTaskLifecycleState::Accepting
            || task.lifecycleState == GpuTaskLifecycleState::Accepted
        )
            return false;
        // A declaration-only discard is terminal. Once a plan began recording, every task must also receive its
        // discarded callback before a different plan or retry can re-arm the graph.
        if(
            (m_activeRecordingPlanGeneration == 0u && task.lifecycleState == GpuTaskLifecycleState::Discarded)
            || (
                (!planChanged || m_activeRecordingPlanGeneration != 0u)
                && task.lifecycleState != GpuTaskLifecycleState::Discarded
            )
        )
            return false;
    }

    m_activeRecordingPlanGeneration = compiledGraph.planGeneration();
    m_activeRecordingAttemptGeneration = allocateGeneration();
    for(const GpuTaskNode& task : m_tasks){
        task.lifecycleState = GpuTaskLifecycleState::Declared;
        task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration;
        task.recordingClaimGeneration = 0u;
        task.submissionClaimGeneration = 0u;
        task.recordThunkInProgress = false;
        task.recordThunkCompleted = false;
    }
    return true;
}

bool GpuTaskGraph::matchesRecordingAttempt(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)const noexcept{
    ScopedLock lock(m_lifecycleMutex);
    return recordingAttemptGeneration != 0u
        && !m_teardownInProgress
        && compiledGraph.validFor(*this)
        && m_activeRecordingPlanGeneration == compiledGraph.planGeneration()
        && m_activeRecordingAttemptGeneration == recordingAttemptGeneration
    ;
}

bool GpuTaskGraph::validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
    if(deviceGeneration == 0u)
        return false;

    const auto validStateSource = [deviceGeneration](const CommandListResourceStateHandoff* const states){
        // Invalid declarations intentionally remain a record-time failure so legacy callers retain their existing
        // diagnostic. A valid snapshot, however, must never cross a native Device lifetime.
        return !states || !states->valid() || states->validForDeviceGeneration(deviceGeneration);
    };

    for(const GpuTaskExternalStateSource& source : m_externalStateSources){
        if(!validStateSource(source.states))
            return false;
    }
    for(const GpuGraphResourceNode& resource : m_resources){
        if(
            (resource.texture != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.buffer != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.accelStruct != nullptr && resource.deviceGeneration != deviceGeneration)
            || !validStateSource(resource.initialOwnerStateSource)
        )
            return false;
    }
    for(const GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(!validStateSource(source.stateSource))
            return false;
    }
    for(const GpuGraphPipelineNode& pipeline : m_pipelines){
        if(
            (pipeline.graphicsPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.computePipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.meshletPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.rayTracingPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
        )
            return false;
    }
    for(const GpuExternalCompletionNode& completion : m_externalCompletions){
        if(
            completion.hasToken
            && (
                !completion.token.valid()
                || !completion.token.hasPhysicalQueueIdentity()
                || completion.token.deviceGeneration != deviceGeneration
            )
        )
            return false;
    }
    return true;
}

bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
}

bool GpuTaskGraph::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceSets.size();
}

bool GpuTaskGraph::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return id.valid()
        && id.generation == m_generation
        && id.index < m_uploadBlobs.size()
        && !m_uploadBlobs[id.index].bytes.empty()
    ;
}

bool GpuTaskGraph::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_pipelines.size();
}

bool GpuTaskGraph::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_externalCompletions.size();
}

GpuTaskGraphTaskView GpuTaskGraph::taskAt(const usize index)const{
    NWB_ASSERT(index < m_tasks.size());
    const GpuTaskNode& task = m_tasks[index];
    return GpuTaskGraphTaskView{
        .id = GpuTaskId{ static_cast<u32>(index), m_generation },
        .identity = task.identity,
        .markerLabel = markerLabel(task.markerLabelOffset, task.markerLabelSize),
        .queue = task.queue,
        .scheduling = task.scheduling,
        .timing = task.timing,
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .externalStateSources = task.externalStateSourceCount > 0u
            ? m_externalStateSources.data() + task.externalStateSourceOffset
            : nullptr,
        .externalStateSourceCount = task.externalStateSourceCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .hasPayload = task.payload != nullptr,
        .hasRecordPayload = task.recordPayload != nullptr,
    };
}

GpuTaskGraphResourceView GpuTaskGraph::resourceAt(const usize index)const{
    NWB_ASSERT(index < m_resources.size());
    const GpuGraphResourceNode& resource = m_resources[index];
    return GpuTaskGraphResourceView{
        .id = GpuGraphResourceId{ static_cast<u32>(index), m_generation },
        .identity = resource.identity,
        .markerLabel = markerLabel(resource.markerLabelOffset, resource.markerLabelSize),
        .type = resource.type,
        .initialState = resource.initialState,
        .externalFinalState = resource.externalFinalState,
        .externalFinalReleaseDestinationQueue = resource.externalFinalReleaseDestinationQueue,
        .initialOwnerQueue = resource.initialOwnerQueue,
        .initialOwnerReleaseDestinationQueue = resource.initialOwnerReleaseDestinationQueue,
        .initialOwnerCompletion = resource.initialOwnerCompletion,
        .initialOwnerMinimumCompletionToken = resource.initialOwnerMinimumCompletionToken,
        .initialOwnerStateSource = resource.initialOwnerStateSource,
        .initialOwnerHandoffSources = resource.initialOwnerHandoffSourceCount != 0u
            ? m_initialOwnerHandoffSources.data() + resource.initialOwnerHandoffSourceOffset
            : nullptr,
        .initialOwnerHandoffSourceCount = resource.initialOwnerHandoffSourceCount,
        .queueSharing = resource.queueSharing,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
    };
}

GpuTaskGraphResourceSetView GpuTaskGraph::resourceSetAt(const usize index)const{
    NWB_ASSERT(index < m_resourceSets.size());
    const GpuGraphResourceSetNode& resourceSet = m_resourceSets[index];
    return GpuTaskGraphResourceSetView{
        .id = GpuGraphResourceSetId{ static_cast<u32>(index), m_generation },
        .identity = resourceSet.identity,
        .markerLabel = markerLabel(resourceSet.markerLabelOffset, resourceSet.markerLabelSize),
        .members = resourceSet.memberCount > 0u ? m_resourceSetMembers.data() + resourceSet.memberOffset : nullptr,
        .memberCount = resourceSet.memberCount,
    };
}

GpuTaskGraphPipelineView GpuTaskGraph::pipelineAt(const usize index)const{
    NWB_ASSERT(index < m_pipelines.size());
    const GpuGraphPipelineNode& pipeline = m_pipelines[index];
    return GpuTaskGraphPipelineView{
        .id = GpuGraphPipelineId{ static_cast<u32>(index), m_generation },
        .identity = pipeline.identity,
        .markerLabel = markerLabel(pipeline.markerLabelOffset, pipeline.markerLabelSize),
        .type = pipeline.type,
        .hasBackendPipeline = pipeline.graphicsPipeline != nullptr
            || pipeline.computePipeline != nullptr
            || pipeline.meshletPipeline != nullptr
            || pipeline.rayTracingPipeline != nullptr,
    };
}

GpuTaskGraphExternalCompletionView GpuTaskGraph::externalCompletionAt(const usize index)const{
    NWB_ASSERT(index < m_externalCompletions.size());
    const GpuExternalCompletionNode& completion = m_externalCompletions[index];
    return GpuTaskGraphExternalCompletionView{
        .id = GpuExternalCompletionId{ static_cast<u32>(index), m_generation },
        .identity = completion.identity,
        .markerLabel = markerLabel(completion.markerLabelOffset, completion.markerLabelSize),
        .token = completion.token,
        .hasToken = completion.hasToken,
    };
}

const QueueSubmissionToken* GpuTaskGraph::externalCompletionToken(
    const GpuExternalCompletionId& completion
)const noexcept{
    if(!validExternalCompletion(completion) || !m_externalCompletions[completion.index].hasToken)
        return nullptr;
    return &m_externalCompletions[completion.index].token;
}

Texture* GpuTaskGraph::textureForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Texture ? node.texture.get() : nullptr;
}

Buffer* GpuTaskGraph::bufferForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Buffer ? node.buffer.get() : nullptr;
}

RayTracingAccelStruct* GpuTaskGraph::accelStructForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::AccelStruct ? node.accelStruct.get() : nullptr;
}

const void* GpuTaskGraph::uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept{
    outByteSize = 0u;
    const GpuUploadBlobNode* const node = findUploadBlob(blob);
    if(!node || node->bytes.empty())
        return nullptr;
    outByteSize = node->bytes.size();
    return node->bytes.data();
}

GraphicsPipeline* GpuTaskGraph::graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Graphics ? node.graphicsPipeline.get() : nullptr;
}

ComputePipeline* GpuTaskGraph::computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Compute ? node.computePipeline.get() : nullptr;
}

MeshletPipeline* GpuTaskGraph::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Meshlet ? node.meshletPipeline.get() : nullptr;
}

RayTracingPipeline* GpuTaskGraph::rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::RayTracing ? node.rayTracingPipeline.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

