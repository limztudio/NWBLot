// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accepted_queue_frontier_validator.h"
#include "compiler_analysis_internal.h"
#include "compiler_internal.h"
#include "dependency_pair_hash.h"
#include "inferred_dependency_collector.h"
#include "resource_hazard_tracker.h"
#include "presentation_endpoint_validator.h"

#include <global/hash_utils.h>
#include <global/simd/vector.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_compiler_analysis{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace GpuTaskGraphCompilerDetail;


[[nodiscard]] static b8 IsResourceVersionHazard(const GpuTaskHazardType::Enum hazard)noexcept{
    return hazard == GpuTaskHazardType::VersionDependency || hazard == GpuTaskHazardType::VersionLifetime;
}

[[nodiscard]] static const GpuTaskDependencyEdge* FindCycleDiagnosticEdge(const Span<const GpuTaskDependencyEdge> cycleEdges)noexcept{
    for(const GpuTaskDependencyEdge& edge : cycleEdges){
        if(IsResourceVersionHazard(edge.hazard))
            return &edge;
    }
    return cycleEdges.empty() ? nullptr : &cycleEdges.front();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphAnalysis::reset()noexcept{
    m_edges.clear();
    m_schedulingEdges.clear();
    m_schedulingOutgoingOffsets.clear();
    m_schedulingOutgoingConsumers.clear();
    m_schedulingIncomingOffsets.clear();
    m_schedulingIncomingProducers.clear();
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

b8 GpuTaskGraphAnalysis::validFor(const GpuTaskGraph::DeclarationReadView& graph)const noexcept{
    // A successful analyze() publishes complete topology and scheduling adjacency before setting m_valid.
    FOUNDATION_ASSERT(
        !m_valid
        || (
            m_topologicalOrder.size() == m_taskCount
            && m_schedulingOutgoingOffsets.size() == m_taskCount + 1u
            && m_schedulingIncomingOffsets.size() == m_taskCount + 1u
            && m_schedulingOutgoingOffsets.front() == 0u
            && m_schedulingIncomingOffsets.front() == 0u
            && m_schedulingOutgoingConsumers.size() == m_schedulingEdges.size()
            && m_schedulingIncomingProducers.size() == m_schedulingEdges.size()
            && m_schedulingOutgoingOffsets.back() == m_schedulingEdges.size()
            && m_schedulingIncomingOffsets.back() == m_schedulingEdges.size()
        )
    );
    return
        m_valid
        && m_generation == graph.generation()
        && m_declarationRevision == graph.declarationRevision()
        && m_taskCount == graph.taskCount()
        && m_resourceCount == graph.resourceCount()
        && m_resourceVersionCount == graph.resourceVersionCount()
        && m_externalCompletionCount == graph.externalCompletionCount()
    ;
}

GpuTaskGraphSchedulingTaskIndexView GpuTaskGraphAnalysis::schedulingConsumers(const GpuTaskId& producer)const noexcept{
    if(!m_valid || !producer.valid() || producer.generation != m_generation || producer.index >= m_taskCount)
        return {};
    FOUNDATION_ASSERT(m_schedulingOutgoingOffsets.size() == m_taskCount + 1u);
    const usize firstConsumer = m_schedulingOutgoingOffsets[producer.index];
    const usize consumerEnd = m_schedulingOutgoingOffsets[producer.index + 1u];
    FOUNDATION_ASSERT(firstConsumer <= consumerEnd && consumerEnd <= m_schedulingOutgoingConsumers.size());
    return GpuTaskGraphSchedulingTaskIndexView{ .taskIndices = consumerEnd != firstConsumer ? m_schedulingOutgoingConsumers.data() + firstConsumer : nullptr, .taskCount = consumerEnd - firstConsumer, };
}

GpuTaskGraphSchedulingTaskIndexView GpuTaskGraphAnalysis::schedulingProducers(const GpuTaskId& consumer)const noexcept{
    if(!m_valid || !consumer.valid() || consumer.generation != m_generation || consumer.index >= m_taskCount)
        return {};
    FOUNDATION_ASSERT(m_schedulingIncomingOffsets.size() == m_taskCount + 1u);
    const usize firstProducer = m_schedulingIncomingOffsets[consumer.index];
    const usize producerEnd = m_schedulingIncomingOffsets[consumer.index + 1u];
    FOUNDATION_ASSERT(firstProducer <= producerEnd && producerEnd <= m_schedulingIncomingProducers.size());
    return GpuTaskGraphSchedulingTaskIndexView{ .taskIndices = producerEnd != firstProducer ? m_schedulingIncomingProducers.data() + firstProducer : nullptr, .taskCount = producerEnd - firstProducer, };
}

b8 GpuTaskGraphAnalysis::hasExplicitEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_edges){
        if(edge.producer == producer && edge.consumer == consumer && edge.hazard == GpuTaskHazardType::Explicit)
            return true;
    }
    return false;
}

b8 GpuTaskGraphAnalysis::hasInferredEdge(const GpuTaskId& producer, const GpuTaskId& consumer)const noexcept{
    for(const GpuTaskDependencyEdge& edge : m_inferredEdges){
        if(edge.producer == producer && edge.consumer == consumer)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskGraphCompiler::analyze(const GpuTaskGraph::DeclarationReadView& graph, Alloc::ScratchArena& scratchArena, GpuTaskGraphAnalysis& outAnalysis)const{
    using namespace GpuTaskGraphCompilerDetail;
    using namespace __hidden_gpu_task_graph_compiler_analysis;

    if(!graph.valid())
        return false;
    outAnalysis.reset();

    outAnalysis.m_generation = graph.generation();
    outAnalysis.m_declarationRevision = graph.declarationRevision();
    outAnalysis.m_taskCount = graph.taskCount();
    outAnalysis.m_resourceCount = graph.resourceCount();
    outAnalysis.m_resourceVersionCount = graph.resourceVersionCount();
    outAnalysis.m_externalCompletionCount = graph.externalCompletionCount();

    const Timer validationBegin = TimerNow();
    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {}, const GpuGraphResourceVersionId resourceVersion = {})noexcept{
        outAnalysis.m_diagnostic.status = status;
        outAnalysis.m_diagnostic.task = task;
        outAnalysis.m_diagnostic.relatedTask = relatedTask;
        outAnalysis.m_diagnostic.resource = resource;
        outAnalysis.m_diagnostic.resourceVersion = resourceVersion;
        return false;
    };
    // Graph storage admits resource and completion identities and resource types before publication.
#if defined(NWB_DEBUG)
    for(usize resourceIndex = 0u; resourceIndex < graph.resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = graph.resourceAt(resourceIndex);
        FOUNDATION_ASSERT(resource.identity && resource.type < GpuGraphResourceType::Count);
    }
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex)
        FOUNDATION_ASSERT(graph.externalCompletionAt(completionIndex).identity);
#endif
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        FOUNDATION_ASSERT(task.identity);
        if(!IsValidCommandRequirements(task.commands) || !IsValidSchedulingHint(task.scheduling) || task.timing.policy >= GpuTaskTimingPolicy::Count)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, task.id);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            if(!graph.validTask(task.dependencies[dependencyIndex]))
                return fail(GpuTaskGraphAnalysisStatus::InvalidTaskDependency, task.id, task.dependencies[dependencyIndex]);
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            if(!graph.validExternalCompletion(task.externalDependencies[dependencyIndex])){
                return fail(GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency, task.id);
            }
        }
        // Task storage owns non-null external state snapshots with admitted consumer queue classes.
        FOUNDATION_ASSERT(task.externalStateSourceCount == 0u || task.externalStateSources);
#if defined(NWB_DEBUG)
        for(usize sourceIndex = 0u; sourceIndex < task.externalStateSourceCount; ++sourceIndex)
            FOUNDATION_ASSERT(task.externalStateSources[sourceIndex].states && task.externalStateSources[sourceIndex].applicableConsumerQueueClass <= CommandQueue::kCount);
#endif
        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(!graph.validResource(use.resource) || use.access >= GpuTaskResourceAccess::Count)
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);

            const GpuTaskGraphResourceView resource = graph.resourceAt(use.resource.index);
            // Only typed imports bind backend objects, and they never use HazardDomain.
            FOUNDATION_ASSERT(resource.type != GpuGraphResourceType::HazardDomain || !resource.hasBackendResource);
            if(
                (resource.type != GpuGraphResourceType::HazardDomain && use.requiredState == ResourceStates::Unknown)
                || (resource.type == GpuGraphResourceType::Texture && !use.range.textureSubresources.hasExtent())
                || (resource.type == GpuGraphResourceType::Buffer && !use.range.bufferRange.hasExtent())
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, task.id, {}, use.resource);
        }
    }
    f64 validationSeconds = DurationInSeconds<f64>(TimerNow(), validationBegin);

    const Timer dependencyAnalysisBegin = TimerNow();
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena> resourceVersionDependencyEdges(scratchArena);
    const auto resourceVersions = BuildResourceVersionDependencyEdges(graph, scratchArena, resourceVersionDependencyEdges);
    if(!resourceVersions){
        outAnalysis.m_diagnostic = resourceVersions.error();
        return false;
    }
    GpuInferredDependencyCollector edgeCollector(scratchArena, outAnalysis.m_edges, outAnalysis.m_inferredEdges, outAnalysis.m_explicitEdgeCount, outAnalysis.m_inferredEdgeCount, outAnalysis.m_resourceVersionEdgeCount);
    usize expectedEdgeCount = resourceVersionDependencyEdges.size();
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        expectedEdgeCount += task.dependencyCount + task.resourceUseCount;
    }
    edgeCollector.reserve(expectedEdgeCount);
    Vector<u32, Alloc::ScratchArena> externalDependencyConsumerMarkers(graph.externalCompletionCount(), scratchArena);
    for(usize completionIndex = 0u; completionIndex < graph.externalCompletionCount(); ++completionIndex)
        externalDependencyConsumerMarkers[completionIndex] = Limit<u32>::s_Max;

    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        for(usize dependencyIndex = 0u; dependencyIndex < task.dependencyCount; ++dependencyIndex){
            edgeCollector.appendRaw(GpuTaskDependencyEdge{ .producer = task.dependencies[dependencyIndex], .consumer = task.id, .resource = {}, .resourceVersion = {}, .hazard = GpuTaskHazardType::Explicit, });
        }
        for(usize dependencyIndex = 0u; dependencyIndex < task.externalDependencyCount; ++dependencyIndex){
            const GpuExternalCompletionId completion = task.externalDependencies[dependencyIndex];
            if(externalDependencyConsumerMarkers[completion.index] == task.id.index)
                continue;
            outAnalysis.m_externalDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = completion, .consumer = task.id, });
            externalDependencyConsumerMarkers[completion.index] = task.id.index;
        }
    }
    for(const GpuTaskDependencyEdge& edge : resourceVersionDependencyEdges)
        edgeCollector.appendInferred(edge);
    const f64 dependencyAnalysisSeconds = DurationInSeconds<f64>(TimerNow(), dependencyAnalysisBegin);

    const Timer semanticTopologyBegin = TimerNow();
    TaskDependencyAdjacency dependencyAdjacency(scratchArena);
    {
        BuildTaskDependencyAdjacency(outAnalysis.m_edges, graph.taskCount(), scratchArena, dependencyAdjacency);
        if(!BuildTopologicalOrder(graph, outAnalysis.m_edges, dependencyAdjacency, scratchArena, outAnalysis.m_topologicalOrder, outAnalysis.m_cyclePath, outAnalysis.m_cycleEdges)){
            const GpuTaskDependencyEdge* const cycleEdge = FindCycleDiagnosticEdge(outAnalysis.m_cycleEdges);
            return fail(
                GpuTaskGraphAnalysisStatus::Cycle,
                cycleEdge ? cycleEdge->consumer : GpuTaskId{},
                cycleEdge ? cycleEdge->producer : GpuTaskId{},
                cycleEdge ? cycleEdge->resource : GpuGraphResourceId{},
                cycleEdge ? cycleEdge->resourceVersion : GpuGraphResourceVersionId{}
            );
        }
    }
    const f64 semanticTopologySeconds = DurationInSeconds<f64>(TimerNow(), semanticTopologyBegin);

    const Timer hazardAnalysisBegin = TimerNow();
    GpuTaskId invalidHazardTask;
    GpuGraphResourceId invalidHazardResource;
    if(!GpuResourceHazardTracker::Append(graph, outAnalysis.m_topologicalOrder, scratchArena, edgeCollector, invalidHazardTask, invalidHazardResource))
        return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, invalidHazardTask, {}, invalidHazardResource);
    const f64 hazardAnalysisSeconds = DurationInSeconds<f64>(TimerNow(), hazardAnalysisBegin);

    const Timer finalTopologyBegin = TimerNow();
    {
        if(dependencyAdjacency.edgeIndices.size() != outAnalysis.m_edges.size()){
            BuildTaskDependencyAdjacency(outAnalysis.m_edges, graph.taskCount(), scratchArena, dependencyAdjacency);
        }
        // Physical hazards only connect earlier semantic-order accesses to the current task.
        // Those forward edges preserve the already smallest stable topological order and cannot introduce a cycle.
        BuildSchedulingEdges(outAnalysis.m_edges, dependencyAdjacency, outAnalysis.m_topologicalOrder, scratchArena, outAnalysis.m_schedulingEdges);
        BuildSchedulingTaskAdjacency(
            outAnalysis.m_schedulingEdges,
            graph.taskCount(),
            graph.generation(),
            scratchArena,
            outAnalysis.m_schedulingOutgoingOffsets,
            outAnalysis.m_schedulingOutgoingConsumers,
            outAnalysis.m_schedulingIncomingOffsets,
            outAnalysis.m_schedulingIncomingProducers
        );
    }
    const f64 topologicalOrderSeconds = semanticTopologySeconds + DurationInSeconds<f64>(TimerNow(), finalTopologyBegin);

    const Timer acceptedQueueFrontierValidationBegin = TimerNow();
    if(!GpuAcceptedQueueFrontierValidator::Validate(graph, outAnalysis.m_schedulingIncomingOffsets, outAnalysis.m_edges, outAnalysis.m_diagnostic))
        return false;
    validationSeconds += DurationInSeconds<f64>(TimerNow(), acceptedQueueFrontierValidationBegin);

    const Timer presentationValidationBegin = TimerNow();
    if(!GpuPresentationEndpointValidator::Validate(graph, outAnalysis.m_topologicalOrder, outAnalysis.m_schedulingIncomingOffsets, outAnalysis.m_schedulingIncomingProducers, scratchArena, outAnalysis.m_diagnostic))
        return false;
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

