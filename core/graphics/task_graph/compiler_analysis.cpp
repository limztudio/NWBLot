// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "compiler_internal.h"


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

[[nodiscard]] static bool IncludesEdge(
    const GpuTaskDependencyEdge& edge,
    const bool explicitOnly
)noexcept{
    return !explicitOnly || edge.hazard == GpuTaskHazardType::Explicit;
}

static bool BuildTopologicalOrder(
    const GpuTaskGraph& graph,
    const GraphicsVector<GpuTaskDependencyEdge>& edges,
    const bool explicitOnly,
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
        if(IncludesEdge(edge, explicitOnly))
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
            if(IncludesEdge(edge, explicitOnly) && edge.producer.index == nextTask){
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
                IncludesEdge(edge, explicitOnly)
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
            if(!IncludesEdge(edge, explicitOnly) || edge.producer.index != taskIndex)
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphAnalysis::reset(){
    m_edges.clear();
    m_inferredEdges.clear();
    m_externalDependencies.clear();
    m_topologicalOrder.clear();
    m_cyclePath.clear();
    m_cycleEdges.clear();
    m_diagnostic = GpuTaskGraphAnalysisDiagnostic{};
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_taskCount = 0u;
    m_resourceCount = 0u;
    m_externalCompletionCount = 0u;
    m_explicitEdgeCount = 0u;
    m_inferredEdgeCount = 0u;
    m_valid = false;
}

bool GpuTaskGraphAnalysis::validFor(const GpuTaskGraph& graph)const noexcept{
    return m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_taskCount == graph.taskCount()
        && m_resourceCount == graph.resourceCount()
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
    outAnalysis.m_externalCompletionCount = graph.externalCompletionCount();

    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {}){
        outAnalysis.m_diagnostic.status = status;
        outAnalysis.m_diagnostic.task = task;
        outAnalysis.m_diagnostic.relatedTask = relatedTask;
        outAnalysis.m_diagnostic.resource = resource;
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
        if(!task.identity || !IsValidQueueRequest(task.queue) || !IsValidSchedulingHint(task.scheduling))
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
            if(!task.externalStateSources[sourceIndex].states)
                return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        }
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(
                !graph.validResource(use.resource)
                || use.requiredState == ResourceStates::Unknown
                || use.access >= GpuTaskResourceAccess::kCount
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);

            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            if(
                (
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

    const auto appendSchedulingEdge = [&](const GpuTaskDependencyEdge& edge){
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
            if(existing.resource == edge.resource && existing.hazard == edge.hazard)
                return;
        }
        outAnalysis.m_inferredEdges.push_back(edge);
        if(!hasTaskPair)
            ++outAnalysis.m_inferredEdgeCount;
        appendSchedulingEdge(edge);
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
            appendSchedulingEdge(GpuTaskDependencyEdge{
                .producer = task.dependencies[dependencyIndex],
                .consumer = task.id,
                .resource = {},
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

    if(!BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        true,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = outAnalysis.m_cycleEdges.empty()
            ? nullptr
            : &outAnalysis.m_cycleEdges.front()
        ;
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{}
        );
    }

    // Process resource use in the explicit stable order. Explicit relationships therefore outrank declaration
    // order, while per-resource writer/readers state emits only the nearest required RAW/WAR/WAW dependencies.
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

    if(!BuildTopologicalOrder(
        graph,
        outAnalysis.m_edges,
        false,
        outAnalysis.m_topologicalOrder,
        outAnalysis.m_cyclePath,
        outAnalysis.m_cycleEdges,
        scratchArena
    )){
        const GpuTaskDependencyEdge* const cycleEdge = outAnalysis.m_cycleEdges.empty()
            ? nullptr
            : &outAnalysis.m_cycleEdges.front()
        ;
        return fail(
            GpuTaskGraphAnalysisStatus::Cycle,
            cycleEdge ? cycleEdge->consumer : GpuTaskId{},
            cycleEdge ? cycleEdge->producer : GpuTaskId{},
            cycleEdge ? cycleEdge->resource : GpuGraphResourceId{}
        );
    }

    if(const GpuPresentEndpoint* const endpoint = graph.presentEndpoint()){
        if(!graph.validTask(endpoint->producer) || !graph.validResource(endpoint->backBuffer))
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);

        const GpuTaskGraphTaskView producer = graph.taskAt(endpoint->producer.index);
        const GpuTaskGraphResourceView backBuffer = graph.resourceAt(endpoint->backBuffer.index);
        if(
            (backBuffer.type != GpuGraphResourceType::Texture && backBuffer.type != GpuGraphResourceType::HazardDomain)
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
                if(use.resource != endpoint->backBuffer || !IsWriteAccess(use.access))
                    continue;

                hasBackBufferWriter = true;
                if(!reachesProducer[task.id.index]){
                    return fail(
                        GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint,
                        endpoint->producer,
                        task.id,
                        endpoint->backBuffer
                    );
                }
            }
        }
        if(!hasBackBufferWriter)
            return fail(GpuTaskGraphAnalysisStatus::InvalidPresentationEndpoint, endpoint->producer, {}, endpoint->backBuffer);
    }

    outAnalysis.m_diagnostic.status = GpuTaskGraphAnalysisStatus::Success;
    outAnalysis.m_valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

