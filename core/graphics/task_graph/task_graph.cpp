// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include "compiler.h"

#include <core/telemetry/frame_graph_contributor.h>

#include <global/atomic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Atomic<u64> s_NextGeneration{ 1u };

[[nodiscard]] static u64 AllocateGeneration()noexcept{
    u64 generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    if(generation == 0u)
        generation = s_NextGeneration.fetch_add(1u, MemoryOrder::relaxed);
    return generation;
}

[[nodiscard]] static bool CompatibleResourceMetadata(
    const GpuTaskGraphResourceView& resource,
    const GpuGraphResourceDesc& desc
)noexcept{
    return resource.identity == desc.identity
        && resource.type == desc.type
        && resource.initialState == desc.initialState
        && resource.queueSharing == desc.queueSharing;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::GpuTaskGraph(GraphicsArena& arena)
    : m_arena(arena)
    , m_tasks(arena)
    , m_dependencies(arena)
    , m_externalDependencies(arena)
    , m_resourceUses(arena)
    , m_resources(arena)
    , m_externalCompletions(arena)
    , m_markerText(arena)
    , m_generation(__hidden_gpu_task_graph::AllocateGeneration())
{}

GpuTaskGraph::~GpuTaskGraph(){
    destroyTaskPayloads();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::addTask(const GpuTaskDesc& desc){
    return appendTask(desc, nullptr, nullptr, nullptr, nullptr, nullptr);
}

GpuGraphResourceId GpuTaskGraph::importResource(const GpuGraphResourceDesc& desc){
    if(!desc.identity || desc.markerLabel.empty() || desc.type >= GpuGraphResourceType::kCount)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView existing = resourceAt(resourceIndex);
        if(existing.identity != desc.identity)
            continue;
        if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(existing, desc))
            return {};
        return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
    }

    return appendResource(desc);
}

GpuGraphResourceId GpuTaskGraph::importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc){
    if(!texture || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Texture)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Texture && existing.texture.get() == texture.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), desc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(desc);
    if(resource.valid())
        m_resources[resource.index].texture = texture;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc){
    if(!buffer || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::Buffer)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::Buffer && existing.buffer.get() == buffer.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), desc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(desc);
    if(resource.valid())
        m_resources[resource.index].buffer = buffer;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importAccelStruct(
    const RayTracingAccelStructHandle& accelStruct,
    const GpuGraphResourceDesc& desc
){
    if(!accelStruct || !desc.identity || desc.markerLabel.empty() || desc.type != GpuGraphResourceType::AccelStruct)
        return {};

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuGraphResourceNode& existing = m_resources[resourceIndex];
        if(existing.type == GpuGraphResourceType::AccelStruct && existing.accelStruct.get() == accelStruct.get()){
            if(!__hidden_gpu_task_graph::CompatibleResourceMetadata(resourceAt(resourceIndex), desc))
                return {};
            return GpuGraphResourceId{ static_cast<u32>(resourceIndex), m_generation };
        }
        if(existing.identity == desc.identity)
            return {};
    }

    const GpuGraphResourceId resource = appendResource(desc);
    if(resource.valid())
        m_resources[resource.index].accelStruct = accelStruct;
    return resource;
}

GpuGraphResourceId GpuTaskGraph::importHazardDomain(const GpuGraphResourceDesc& desc){
    if(desc.type != GpuGraphResourceType::HazardDomain)
        return {};
    return importResource(desc);
}

GpuExternalCompletionId GpuTaskGraph::importExternalCompletion(const GpuExternalCompletionDesc& desc){
    if(!desc.identity || desc.markerLabel.empty())
        return {};

    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        if(m_externalCompletions[completionIndex].identity == desc.identity)
            return GpuExternalCompletionId{ static_cast<u32>(completionIndex), m_generation };
    }

    return appendExternalCompletion(desc);
}

void GpuTaskGraph::reset(){
    destroyTaskPayloads();
    m_tasks.clear();
    m_dependencies.clear();
    m_externalDependencies.clear();
    m_resourceUses.clear();
    m_resources.clear();
    m_externalCompletions.clear();
    m_markerText.clear();
    m_generation = __hidden_gpu_task_graph::AllocateGeneration();
}

bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
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
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .hasPayload = task.payload != nullptr,
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
        .queueSharing = resource.queueSharing,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
    };
}

GpuTaskGraphExternalCompletionView GpuTaskGraph::externalCompletionAt(const usize index)const{
    NWB_ASSERT(index < m_externalCompletions.size());
    const GpuExternalCompletionNode& completion = m_externalCompletions[index];
    return GpuTaskGraphExternalCompletionView{
        .id = GpuExternalCompletionId{ static_cast<u32>(index), m_generation },
        .identity = completion.identity,
        .markerLabel = markerLabel(completion.markerLabelOffset, completion.markerLabelSize),
    };
}

bool GpuTaskGraph::recordTask(
    const GpuTaskId& taskID,
    CommandList& commandList,
    const GpuTaskRecordContext& context
)const{
    if(!validTask(taskID))
        return false;

    const GpuTaskNode& task = m_tasks[taskID.index];
    return task.payload && task.recordPayload && task.recordPayload(task.payload, commandList, context);
}

void GpuTaskGraph::acceptTask(const GpuTaskId& taskID, const QueueSubmissionToken& token)noexcept{
    if(!validTask(taskID) || !token.valid())
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.payload && task.acceptPayload)
        task.acceptPayload(task.payload, token);
}

void GpuTaskGraph::discardTask(const GpuTaskId& taskID)noexcept{
    if(!validTask(taskID))
        return;

    GpuTaskNode& task = m_tasks[taskID.index];
    if(task.payload && task.discardPayload)
        task.discardPayload(task.payload);
}

bool GpuTaskGraph::appendFrameGraphTelemetry(
    Telemetry::FrameGraphBuilder& builder,
    const GpuTaskGraphAnalysis& analysis,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphTelemetryOptions& options
)const{
    if(
        !analysis.validFor(*this)
        || m_tasks.empty()
        || (options.legacyScheduleMismatchCount > 0u && !options.legacyScheduleMismatches)
        || (options.legacyQueueMismatchCount > 0u && !options.legacyQueueMismatches)
        || (options.queueAssignments && !options.queueAssignments->validFor(*this))
    )
        return false;
    for(usize mismatchIndex = 0u; mismatchIndex < options.legacyQueueMismatchCount; ++mismatchIndex){
        if(!validTask(options.legacyQueueMismatches[mismatchIndex]))
            return false;
    }
    if(options.queueAssignments){
        for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
            if(!options.queueAssignments->find(taskAt(taskIndex).id))
                return false;
        }
    }

    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> resourceNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> completionNodes(scratchArena);
    Vector<Telemetry::FrameGraphNodeHandle, Alloc::ScratchArena> taskNodes(scratchArena);
    resourceNodes.reserve(m_resources.size());
    completionNodes.reserve(m_externalCompletions.size());
    taskNodes.reserve(m_tasks.size());

    for(usize resourceIndex = 0u; resourceIndex < m_resources.size(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = resourceAt(resourceIndex);
        resourceNodes.push_back(builder.addResource(resource.identity, resource.markerLabel));
    }
    for(usize completionIndex = 0u; completionIndex < m_externalCompletions.size(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = externalCompletionAt(completionIndex);
        completionNodes.push_back(builder.addExternal(completion.identity, completion.markerLabel));
    }
    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        u8 flags = GpuTaskGraphTelemetryNodeFlag::None;
        if(options.queueAssignments){
            const GpuTaskQueueAssignment* const assignment = options.queueAssignments->find(task.id);
            NWB_ASSERT(assignment);
            switch(assignment->queueClass){
            case CommandQueue::Graphics:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedGraphicsQueue;
                break;
            case CommandQueue::Compute:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedComputeQueue;
                break;
            default:
                return false;
            }
            if(assignment->dedicated)
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue;
            if(assignment->reason == GpuTaskQueueAssignmentReason::Fallback)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentFallback;
        }
        for(usize mismatchIndex = 0u; mismatchIndex < options.legacyQueueMismatchCount; ++mismatchIndex){
            if(options.legacyQueueMismatches[mismatchIndex] == task.id){
                flags |= GpuTaskGraphTelemetryNodeFlag::LegacyQueueAssignmentMismatch;
                break;
            }
        }
        taskNodes.push_back(builder.addPass(task.identity, task.markerLabel, flags));
    }

    for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const Telemetry::FrameGraphNodeHandle resourceNode = resourceNodes[use.resource.index];
            const Telemetry::FrameGraphNodeHandle taskNode = taskNodes[taskIndex];
            if(use.access == GpuTaskResourceAccess::Read || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(resourceNode, taskNode, Telemetry::FrameGraphEdgeKind::Reads);
            if(use.access == GpuTaskResourceAccess::Write || use.access == GpuTaskResourceAccess::ReadWrite)
                builder.addEdge(taskNode, resourceNode, Telemetry::FrameGraphEdgeKind::Writes);
        }
    }
    for(const GpuTaskExternalDependencyEdge& edge : analysis.externalDependencies())
        builder.addEdge(completionNodes[edge.completion.index], taskNodes[edge.consumer.index], Telemetry::FrameGraphEdgeKind::DependsOn);
    for(const GpuTaskDependencyEdge& edge : analysis.edges()){
        u8 flags = GpuTaskGraphTelemetryEdgeFlag::None;
        if(analysis.hasExplicitEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency;
        if(analysis.hasInferredEdge(edge.producer, edge.consumer))
            flags |= GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
        for(usize mismatchIndex = 0u; mismatchIndex < options.legacyScheduleMismatchCount; ++mismatchIndex){
            const GpuTaskDependencyEdge& mismatch = options.legacyScheduleMismatches[mismatchIndex];
            if(mismatch.producer == edge.producer && mismatch.consumer == edge.consumer){
                flags |= GpuTaskGraphTelemetryEdgeFlag::MissingLegacyScheduleDependency;
                break;
            }
        }
        builder.addEdge(
            taskNodes[edge.producer.index],
            taskNodes[edge.consumer.index],
            Telemetry::FrameGraphEdgeKind::DependsOn,
            flags
        );
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskId GpuTaskGraph::appendTask(
    const GpuTaskDesc& desc,
    void* const payload,
    const GpuTaskRecordThunk recordPayload,
    const GpuTaskAcceptedThunk acceptPayload,
    const GpuTaskDiscardedThunk discardPayload,
    const GpuTaskPayloadDestroyThunk destroyPayload
){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || m_tasks.size() >= Limit<u32>::s_Max
        || desc.dependencyCount > Limit<u32>::s_Max - m_dependencies.size()
        || desc.externalDependencyCount > Limit<u32>::s_Max - m_externalDependencies.size()
        || desc.resourceUseCount > Limit<u32>::s_Max - m_resourceUses.size()
        || (desc.dependencyCount > 0u && !desc.dependencies)
        || (desc.externalDependencyCount > 0u && !desc.externalDependencies)
        || (desc.resourceUseCount > 0u && !desc.resourceUses)
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuTaskNode task;
    task.identity = desc.identity;
    task.queue = desc.queue;
    task.scheduling = desc.scheduling;
    task.markerLabelOffset = markerLabelOffset;
    task.markerLabelSize = markerLabelSize;
    task.dependencyOffset = static_cast<u32>(m_dependencies.size());
    task.dependencyCount = static_cast<u32>(desc.dependencyCount);
    task.externalDependencyOffset = static_cast<u32>(m_externalDependencies.size());
    task.externalDependencyCount = static_cast<u32>(desc.externalDependencyCount);
    task.resourceUseOffset = static_cast<u32>(m_resourceUses.size());
    task.resourceUseCount = static_cast<u32>(desc.resourceUseCount);
    task.payload = payload;
    task.recordPayload = recordPayload;
    task.acceptPayload = acceptPayload;
    task.discardPayload = discardPayload;
    task.destroyPayload = destroyPayload;

    for(usize dependencyIndex = 0u; dependencyIndex < desc.dependencyCount; ++dependencyIndex)
        m_dependencies.push_back(desc.dependencies[dependencyIndex]);
    for(usize dependencyIndex = 0u; dependencyIndex < desc.externalDependencyCount; ++dependencyIndex)
        m_externalDependencies.push_back(desc.externalDependencies[dependencyIndex]);
    for(usize useIndex = 0u; useIndex < desc.resourceUseCount; ++useIndex)
        m_resourceUses.push_back(desc.resourceUses[useIndex]);

    const u32 index = static_cast<u32>(m_tasks.size());
    m_tasks.push_back(Move(task));
    return GpuTaskId{ index, m_generation };
}

GpuGraphResourceId GpuTaskGraph::appendResource(const GpuGraphResourceDesc& desc){
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || desc.type >= GpuGraphResourceType::kCount
        || m_resources.size() >= Limit<u32>::s_Max
    )
        return {};

    u32 markerLabelOffset = 0u;
    u32 markerLabelSize = 0u;
    if(!appendMarkerLabel(desc.markerLabel, markerLabelOffset, markerLabelSize))
        return {};

    GpuGraphResourceNode resource;
    resource.identity = desc.identity;
    resource.type = desc.type;
    resource.initialState = desc.initialState;
    resource.queueSharing = desc.queueSharing;
    resource.markerLabelOffset = markerLabelOffset;
    resource.markerLabelSize = markerLabelSize;

    const u32 index = static_cast<u32>(m_resources.size());
    m_resources.push_back(Move(resource));
    return GpuGraphResourceId{ index, m_generation };
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

void GpuTaskGraph::destroyTaskPayloads()noexcept{
    for(GpuTaskNode& task : m_tasks){
        if(task.payload && task.destroyPayload)
            task.destroyPayload(m_arena, task.payload);
        task.payload = nullptr;
        task.destroyPayload = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

