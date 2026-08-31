// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TrackedResourceAccess{
    GpuTaskId task;
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    bool active = true;
};

[[nodiscard]] static bool IsResourceVersionHazard(const GpuTaskHazardType::Enum hazard)noexcept{
    return hazard == GpuTaskHazardType::VersionDependency || hazard == GpuTaskHazardType::VersionLifetime;
}

[[nodiscard]] static bool IncludesEdge(
    const GpuTaskDependencyEdge& edge,
    const bool semanticOnly
)noexcept{
    return !semanticOnly
        || edge.hazard == GpuTaskHazardType::Explicit
        || IsResourceVersionHazard(edge.hazard)
    ;
}

static bool BuildTopologicalOrder(
    const GpuTaskGraph& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const bool semanticOnly,
    GraphicsVector<GpuTaskId>& outOrder,
    GraphicsVector<GpuTaskId>& outCyclePath,
    GraphicsVector<GpuTaskDependencyEdge>& outCycleEdges,
    Alloc::ScratchArena& scratchArena
){
    const usize taskCount = graph.taskCount();
    Vector<u32, Alloc::ScratchArena> indegrees(taskCount, scratchArena);
    Vector<u8, Alloc::ScratchArena> scheduled(taskCount, scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        indegrees[taskIndex] = 0u;
        scheduled[taskIndex] = 0u;
    }
    for(const GpuTaskDependencyEdge& edge : edges){
        if(IncludesEdge(edge, semanticOnly))
            ++indegrees[edge.consumer.index];
    }

    outOrder.clear();
    outCyclePath.clear();
    outCycleEdges.clear();
    outOrder.reserve(taskCount);
    for(usize emittedCount = 0u; emittedCount < taskCount; ++emittedCount){
        usize nextTask = taskCount;
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
            if(!scheduled[taskIndex] && indegrees[taskIndex] == 0u){
                nextTask = taskIndex;
                break;
            }
        }
        if(nextTask == taskCount)
            break;

        scheduled[nextTask] = 1u;
        outOrder.push_back(graph.taskAt(nextTask).id);
        for(const GpuTaskDependencyEdge& edge : edges){
            if(IncludesEdge(edge, semanticOnly) && edge.producer.index == nextTask){
                NWB_ASSERT(indegrees[edge.consumer.index] > 0u);
                --indegrees[edge.consumer.index];
            }
        }
    }
    if(outOrder.size() == taskCount)
        return true;

    Vector<u8, Alloc::ScratchArena> visitState(taskCount, scratchArena);
    Vector<u32, Alloc::ScratchArena> visitStack(scratchArena);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
        visitState[taskIndex] = 0u;

    const auto appendCycleEdge = [&](const u32 producerIndex, const u32 consumerIndex){
        for(const GpuTaskDependencyEdge& edge : edges){
            if(
                IncludesEdge(edge, semanticOnly)
                && edge.producer.index == producerIndex
                && edge.consumer.index == consumerIndex
            ){
                outCycleEdges.push_back(edge);
                return;
            }
        }
        NWB_ASSERT(false);
    };
    const auto visit = [&](auto&& self, const u32 taskIndex) -> bool{
        visitState[taskIndex] = 1u;
        visitStack.push_back(taskIndex);
        for(const GpuTaskDependencyEdge& edge : edges){
            if(!IncludesEdge(edge, semanticOnly) || edge.producer.index != taskIndex)
                continue;

            const u32 consumerIndex = edge.consumer.index;
            if(visitState[consumerIndex] == 1u){
                usize cycleStart = 0u;
                while(visitStack[cycleStart] != consumerIndex)
                    ++cycleStart;
                for(usize cycleIndex = cycleStart; cycleIndex < visitStack.size(); ++cycleIndex){
                    outCyclePath.push_back(graph.taskAt(visitStack[cycleIndex]).id);
                    if(cycleIndex + 1u < visitStack.size())
                        appendCycleEdge(visitStack[cycleIndex], visitStack[cycleIndex + 1u]);
                }
                outCyclePath.push_back(graph.taskAt(consumerIndex).id);
                appendCycleEdge(taskIndex, consumerIndex);
                return true;
            }
            if(visitState[consumerIndex] == 0u && self(self, consumerIndex))
                return true;
        }
        visitStack.pop_back();
        visitState[taskIndex] = 2u;
        return false;
    };

    for(u32 taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        if(visitState[taskIndex] == 0u && visit(visit, taskIndex))
            break;
    }
    outOrder.clear();
    return false;
}


static void BuildSchedulingEdges(
    const GraphicsVector<GpuTaskDependencyEdge>& rawEdges,
    const usize taskCount,
    GraphicsVector<GpuTaskDependencyEdge>& outSchedulingEdges,
    Alloc::ScratchArena& scratchArena
){
    Vector<u8, Alloc::ScratchArena> reached(taskCount, scratchArena);
    Vector<u32, Alloc::ScratchArena> pending(scratchArena);
    pending.reserve(taskCount);
    outSchedulingEdges.clear();
    outSchedulingEdges.reserve(rawEdges.size());

    // Raw edges contain one stable record per task pair. Keep a pair only when removing that candidate eliminates
    // all producer-to-consumer paths; iterating raw order makes the reduced DAG deterministic for diagnostics.
    for(const GpuTaskDependencyEdge& candidate : rawEdges){
        for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex)
            reached[taskIndex] = 0u;
        pending.clear();
        reached[candidate.producer.index] = 1u;
        pending.push_back(candidate.producer.index);

        bool hasAlternatePath = false;
        for(usize pendingIndex = 0u; pendingIndex < pending.size() && !hasAlternatePath; ++pendingIndex){
            const u32 producerIndex = pending[pendingIndex];
            for(const GpuTaskDependencyEdge& edge : rawEdges){
                if(
                    edge.producer.index != producerIndex
                    || (
                        edge.producer == candidate.producer
                        && edge.consumer == candidate.consumer
                    )
                    || reached[edge.consumer.index]
                )
                    continue;

                reached[edge.consumer.index] = 1u;
                if(edge.consumer == candidate.consumer){
                    hasAlternatePath = true;
                    break;
                }
                pending.push_back(edge.consumer.index);
            }
        }
        if(!hasAlternatePath)
            outSchedulingEdges.push_back(candidate);
    }
}

[[nodiscard]] static const GpuTaskDependencyEdge* FindCycleDiagnosticEdge(
    const GraphicsVector<GpuTaskDependencyEdge>& cycleEdges
)noexcept{
    for(const GpuTaskDependencyEdge& edge : cycleEdges){
        if(IsResourceVersionHazard(edge.hazard))
            return &edge;
    }
    return cycleEdges.empty() ? nullptr : &cycleEdges.front();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphAnalysis::reset(){
    m_edges.clear();
    m_schedulingEdges.clear();
    m_inferredEdges.clear();
    m_externalDependencies.clear();
    m_topologicalOrder.clear();
    m_cyclePath.clear();
    m_cycleEdges.clear();
    m_diagnostic = GpuTaskGraphAnalysisDiagnostic{};
    m_validationSeconds = 0.0;
    m_dependencyAnalysisSeconds = 0.0;
    m_hazardAnalysisSeconds = 0.0;
    m_topologicalOrderSeconds = 0.0;
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_taskCount = 0u;
    m_resourceCount = 0u;
    m_resourceVersionCount = 0u;
    m_externalCompletionCount = 0u;
    m_explicitEdgeCount = 0u;
    m_inferredEdgeCount = 0u;
    m_resourceVersionEdgeCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphAnalysis::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_taskCount == graph.taskCount()
        && m_resourceCount == graph.resourceCount()
        && m_resourceVersionCount == graph.resourceVersionCount()
        && m_externalCompletionCount == graph.externalCompletionCount()
    ;
}

bool GpuTaskGraphAnalysis::hasExplicitEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_edges){
        if(
            edge.producer == producer
            && edge.consumer == consumer
            && edge.hazard == GpuTaskHazardType::Explicit
        )
            return true;
    }
    return false;
}

bool GpuTaskGraphAnalysis::hasInferredEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_inferredEdges){
        if(edge.producer == producer && edge.consumer == consumer)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphCompiler::analyze(
    const GpuTaskGraph& graph,
    GpuTaskGraphAnalysis& outAnalysis,
    Alloc::ScratchArena& scratchArena
)const{
    using namespace GpuTaskGraphCompilerDetail;

    outAnalysis.reset();
    outAnalysis.m_generation = graph.generation();
    outAnalysis.m_declarationRevision = graph.declarationRevision();
    outAnalysis.m_taskCount = graph.taskCount();
    outAnalysis.m_resourceCount = graph.resourceCount();
    outAnalysis.m_resourceVersionCount = graph.resourceVersionCount();
    outAnalysis.m_externalCompletionCount = graph.externalCompletionCount();

    const Timer validationBegin = TimerNow();
    const auto fail = [&](
        const GpuTaskGraphAnalysisStatus::Enum status,
        const GpuTaskId task = {},
        const GpuTaskId relatedTask = {},
        const GpuGraphResourceId resource = {},
        const GpuGraphResourceVersionId resourceVersion = {}
    ){
        outAnalysis.m_diagnostic.status = status;
        outAnalysis.m_diagnostic.task = task;
        outAnalysis.m_diagnostic.relatedTask = relatedTask;
        outAnalysis.m_diagnostic.resource = resource;
        outAnalysis.m_diagnostic.resourceVersion = resourceVersion;
        return false;
    };
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        if(!resource.identity || resource.type >= GpuGraphResourceType::kCount)
            return fail(GpuTaskGraphAnalysisStatus::InvalidResource, {}, {}, resource.id);
    }
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex){
        if(!graph.externalCompletionAt(completionIndex).identity)
            return fail(GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency);
    }
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(
            !task.identity
            || !IsValidQueueRequest(task.queue)
            || !IsValidSchedulingHint(task.scheduling)
            || task.timing.policy >= GpuTaskTimingPolicy::kCount
        )
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            if(!graph.validTask(task.dependencies[dependencyIndex]))
                return fail(GpuTaskGraphAnalysisStatus::InvalidTaskDependency, task.id, task.dependencies[dependencyIndex]);
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            if(!graph.validExternalCompletion(task.externalDependencies[dependencyIndex])){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency,
                    task.id
                );
            }
        }
        if(task.externalStateSourceCount != 0u && !task.externalStateSources)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        for(usize sourceIndex = 0u; sourceIndex < task.externalStateSourceCount; ++sourceIndex){
            if(
                !task.externalStateSources[sourceIndex].states
                || task.externalStateSources[sourceIndex].applicableConsumerQueueClass > CommandQueue::kCount
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        }
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(
                !graph.validResource(use.resource)
                || use.access >= GpuTaskResourceAccess::kCount
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);

            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(
                (
                    resource.type == GpuGraphResourceType::HazardDomain
                    && resource.hasBackendResource
                )
                || (
                    resource.type != GpuGraphResourceType::HazardDomain
                    && use.requiredState == ResourceStates::Unknown
                ) || (
                    resource.type == GpuGraphResourceType::Texture
                    && !IsValidTextureRange(use.range.textureSubresources)
                )
                || (
                    resource.type == GpuGraphResourceType::Buffer
                    && !IsValidBufferRange(use.range.bufferRange)
                )
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);
        }
    }
    f64 validationSeconds = DurationInSeconds<f64>(TimerNow(), validationBegin);

    const Timer dependencyAnalysisBegin = TimerNow();
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena> resourceVersionDependencyEdges(scratchArena);
    GpuTaskGraphAnalysisDiagnostic resourceVersionDiagnostic;
    if(!BuildResourceVersionDependencyEdges(
        graph,
        resourceVersionDependencyEdges,
        resourceVersionDiagnostic,
        scratchArena
    )){
        outAnalysis.m_diagnostic = resourceVersionDiagnostic;
        return false;
    }
    const auto appendRawEdge = [&](const GpuTaskDependencyEdge& edge){
        for(GpuTaskDependencyEdge& existing : outAnalysis.m_edges){
            if(existing.producer != edge.producer || existing.consumer != edge.consumer)
                continue;
            if(
                edge.hazard == GpuTaskHazardType::Explicit
                && existing.hazard != GpuTaskHazardType::Explicit
            ){
                existing = edge;
                ++outAnalysis.m_explicitEdgeCount;
            }
            return;
        }
        outAnalysis.m_edges.push_back(edge);
        if(edge.hazard == GpuTaskHazardType::Explicit)
            ++outAnalysis.m_explicitEdgeCount;
    };
    const auto appendInferredEdge = [&](const GpuTaskDependencyEdge& edge){
        NWB_ASSERT(edge.hazard != GpuTaskHazardType::Explicit);

        bool hasTaskPair = false;
        for(const GpuTaskDependencyEdge& existing : outAnalysis.m_inferredEdges){
            if(existing.producer != edge.producer || existing.consumer != edge.consumer)
                continue;
            hasTaskPair = true;
            if(
                existing.resource == edge.resource
                && existing.resourceVersion == edge.resourceVersion
                && existing.hazard == edge.hazard
            )
                return;
        }
        outAnalysis.m_inferredEdges.push_back(edge);
        if(!hasTaskPair)
            ++outAnalysis.m_inferredEdgeCount;
        if(IsResourceVersionHazard(edge.hazard))
            ++outAnalysis.m_resourceVersionEdgeCount;
        appendRawEdge(edge);
    };
    const auto appendExternalDependency = [&](const GpuTaskExternalDependencyEdge& edge){
        for(const GpuTaskExternalDependencyEdge& existing : outAnalysis.m_externalDependencies){
            if(existing.completion == edge.completion && existing.consumer == edge.consumer)
                return;
        }
        outAnalysis.m_externalDependencies.push_back(edge);
    };

    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            appendRawEdge(GpuTaskDependencyEdge{
                .producer = task.dependencies[dependencyIndex],
                .consumer = task.id,
                .resource = {},
                .resourceVersion = {},
                .hazard = GpuTaskHazardType::Explicit,
            });
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            appendExternalDependency(GpuTaskExternalDependencyEdge{
                .completion = task.externalDependencies[dependencyIndex],
                .consumer = task.id,
            });
        }
    }
    for(const GpuTaskDependencyEdge& edge : resourceVersionDependencyEdges)
        appendInferredEdge(edge);
    const f64 dependencyAnalysisSeconds = DurationInSeconds<f64>(TimerNow(), dependencyAnalysisBegin);

    const Timer semanticTopologyBegin = TimerNow();
    if(!BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        true,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = FindCycleDiagnosticEdge(outAnalysis.m_cycleEdges);
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{},
            cycleEdge ? cycleEdge->resourceVersion : GpuGraphResourceVersionId{}
        );
    }
    const f64 semanticTopologySeconds = DurationInSeconds<f64>(TimerNow(), semanticTopologyBegin);

    const Timer hazardAnalysisBegin = TimerNow();
    // Process physical resource use in the semantic stable order. Explicit and version relationships therefore
    // outrank declaration order, while per-resource state emits only the nearest required RAW/WAR/WAW dependencies.
    usize potentialAccessCount = 0u;
    for(const GpuTaskId task : outAnalysis.m_topologicalOrder)
        potentialAccessCount += graph.taskAt(task.index).resourceUseCount;
    Vector<TrackedResourceAccess, Alloc::ScratchArena> writers(scratchArena);
    Vector<TrackedResourceAccess, Alloc::ScratchArena> readers(scratchArena);
    writers.reserve(potentialAccessCount);
    readers.reserve(potentialAccessCount);

    for(const GpuTaskId taskID : outAnalysis.m_topologicalOrder){
        const GpuTaskGraphTaskView task = graph.taskAt(taskID.index);
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            const auto overlaps = [&](const TrackedResourceAccess& access){
                return access.active
                    && access.task != task.id
                    && access.resource == use.resource
                    && RangesOverlap(resource, access.range, use.range)
                ;
            };

            if(IsReadAccess(use.access)){
                for(const TrackedResourceAccess& writer : writers){
                    if(!overlaps(writer))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = writer.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .resourceVersion = {},
                        .hazard = GpuTaskHazardType::ReadAfterWrite,
                    });
                }
            }
            if(IsWriteAccess(use.access)){
                for(const TrackedResourceAccess& writer : writers){
                    if(!overlaps(writer))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = writer.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .resourceVersion = {},
                        .hazard = GpuTaskHazardType::WriteAfterWrite,
                    });
                }
                for(const TrackedResourceAccess& reader : readers){
                    if(!overlaps(reader))
                        continue;
                    appendInferredEdge(GpuTaskDependencyEdge{
                        .producer = reader.task,
                        .consumer = task.id,
                        .resource = use.resource,
                        .resourceVersion = {},
                        .hazard = GpuTaskHazardType::WriteAfterRead,
                    });
                }
                for(TrackedResourceAccess& writer : writers){
                    if(
                        writer.active
                        && writer.resource == use.resource
                        && RangeContains(resource, use.range, writer.range)
                    )
                        writer.active = false;
                }
                for(TrackedResourceAccess& reader : readers){
                    if(
                        reader.active
                        && reader.resource == use.resource
                        && RangeContains(resource, use.range, reader.range)
                    )
                        reader.active = false;
                }
                writers.push_back(TrackedResourceAccess{
                    .task = task.id,
                    .resource = use.resource,
                    .range = use.range,
                });
            }
            else if(IsReadAccess(use.access)){
                bool alreadyWrittenByTask = false;
                for(const TrackedResourceAccess& writer : writers){
                    if(
                        writer.active
                        && writer.task == task.id
                        && writer.resource == use.resource
                        && RangeContains(resource, writer.range, use.range)
                    ){
                        alreadyWrittenByTask = true;
                        break;
                    }
                }
                if(!alreadyWrittenByTask){
                    readers.push_back(TrackedResourceAccess{
                        .task = task.id,
                        .resource = use.resource,
                        .range = use.range,
                    });
                }
            }
        }
    }
    const f64 hazardAnalysisSeconds = DurationInSeconds<f64>(TimerNow(), hazardAnalysisBegin);

    const Timer finalTopologyBegin = TimerNow();
    if(!BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        false,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = FindCycleDiagnosticEdge(outAnalysis.m_cycleEdges);
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{},
            cycleEdge ? cycleEdge->resourceVersion : GpuGraphResourceVersionId{}
        );
    }
    BuildSchedulingEdges(
        outAnalysis.m_edges,
        graph.taskCount(),
        outAnalysis.m_schedulingEdges,
        scratchArena
    );
    const f64 topologicalOrderSeconds = semanticTopologySeconds
        + DurationInSeconds<f64>(TimerNow(), finalTopologyBegin)
    ;

    const Timer acceptedQueueFrontierValidationBegin = TimerNow();
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(task.scheduling.isRecoverySubmission && !task.scheduling.joinsAcceptedQueueFrontier)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id);
        if(!task.scheduling.joinsAcceptedQueueFrontier)
            continue;

        // Late recovery/finalization joins the latest accepted queue tokens at submission time. It must not inherit
        // a graph prerequisite whose rejection could make the join unavailable. Scheduling edges are a reduced
        // subset of these raw edges, so rejecting every raw incoming edge also covers inferred HazardDomain
        // prerequisites.
        for(const GpuTaskDependencyEdge& edge : outAnalysis.m_edges){
            if(edge.consumer == task.id){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask,
                    task.id,
                    edge.producer,
                    edge.resource
                );
            }
        }
        if(task.externalDependencyCount != 0u || task.externalStateSourceCount != 0u)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id);

        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(graph.resourceAt(use.resource.index).type != GpuGraphResourceType::HazardDomain){
                return fail(
                    GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask,
                    task.id,
                    {},
                    use.resource
                );
            }
        }
    }
    validationSeconds += DurationInSeconds<f64>(TimerNow(), acceptedQueueFrontierValidationBegin);

    const Timer presentationValidationBegin = TimerNow();
    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        if(!graph.validTask(endpoint->producer) || !graph.validResource(endpoint->backBuffer))
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);

        const GpuTaskGraphTaskView producer = graph.taskAt(endpoint->producer.index);
        const GpuTaskGraphResourceView backBuffer = graph.resourceAt(endpoint->backBuffer.index);
        const Texture* const backBufferTexture = graph.textureForResource(endpoint->backBuffer);
        if(
            backBuffer.type != GpuGraphResourceType::Texture
            || !backBuffer.hasBackendResource
            || !backBufferTexture
            || backBuffer.externalFinalReleaseDestinationQueue.valid()
            || (
                backBuffer.initialState != ResourceStates::Unknown
                && backBuffer.initialState != ResourceStates::Present
            )
            || backBuffer.externalFinalState != ResourceStates::Present
            || !HasCapabilities(
                producer.queue.requiredCapabilities,
                GpuQueueCapability::Graphics
            )
        )
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, producer.id, {}, backBuffer.id);

        Vector<u8, Alloc::ScratchArena> reachesProducer(graph.taskCount(), scratchArena);
        for(usize taskIndex = 0u; taskIndex < reachesProducer.size(); ++taskIndex)
            reachesProducer[taskIndex] = 0u;
        reachesProducer[endpoint->producer.index] = 1u;
        for(usize orderIndex = outAnalysis.m_topologicalOrder.size(); orderIndex > 0u; --orderIndex){
            const GpuTaskId consumer = outAnalysis.m_topologicalOrder[orderIndex - 1u];
            if(!reachesProducer[consumer.index])
                continue;
            for(const GpuTaskDependencyEdge& edge : outAnalysis.m_edges){
                if(edge.consumer == consumer)
                    reachesProducer[edge.producer.index] = 1u;
            }
        }

        bool hasBackBufferWriter = false;
        for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
            const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
            for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
                const GpuTaskResourceUse& use = task.resourceUses[useIndex];
                if(use.resource != endpoint->backBuffer)
                    continue;

                if(!reachesProducer[task.id.index]){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint,
                        endpoint->producer,
                        task.id,
                        endpoint->backBuffer
                    );
                }
                if(!IsWriteAccess(use.access))
                    continue;
                if(
                    use.requiredState == ResourceStates::Unknown
                    || (use.requiredState & ResourceStates::Present) != ResourceStates::Unknown
                ){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint,
                        endpoint->producer,
                        task.id,
                        endpoint->backBuffer
                    );
                }
                hasBackBufferWriter = true;
            }
        }
        if(!hasBackBufferWriter)
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);
    }
    validationSeconds += DurationInSeconds<f64>(TimerNow(), presentationValidationBegin);

    outAnalysis.m_validationSeconds = validationSeconds;
    outAnalysis.m_dependencyAnalysisSeconds = dependencyAnalysisSeconds;
    outAnalysis.m_hazardAnalysisSeconds = hazardAnalysisSeconds;
    outAnalysis.m_topologicalOrderSeconds = topologicalOrderSeconds;
    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::Success;
    outAnalysis.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

