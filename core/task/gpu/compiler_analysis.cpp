// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accepted_queue_frontier_validator.h"

#include <global/simd/vector_arithmetic.h>
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
    Alloc::ScratchArena::Scope scratchScope(scratchArena);

    const u64 generation = CompilerGraphAccess::Generation(graph);
    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const usize resourceCount = CompilerGraphAccess::ResourceCount(graph);
    const usize resourceVersionCount = CompilerGraphAccess::ResourceVersionCount(graph);
    const usize externalCompletionCount = CompilerGraphAccess::ExternalCompletionCount(graph);
    outAnalysis.m_generation = generation;
    outAnalysis.m_declarationRevision = CompilerGraphAccess::DeclarationRevision(graph);
    outAnalysis.m_taskCount = taskCount;
    outAnalysis.m_resourceCount = resourceCount;
    outAnalysis.m_resourceVersionCount = resourceVersionCount;
    outAnalysis.m_externalCompletionCount = externalCompletionCount;

    const Timer validationBegin = TimerNow();
    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {}, const GpuGraphResourceVersionId resourceVersion = {})noexcept{
        outAnalysis.m_diagnostic.status = status;
        outAnalysis.m_diagnostic.task = task;
        outAnalysis.m_diagnostic.relatedTask = relatedTask;
        outAnalysis.m_diagnostic.resource = resource;
        outAnalysis.m_diagnostic.resourceVersion = resourceVersion;
        return false;
    };
#if defined(NWB_DEBUG)
    for(usize resourceIndex = 0u; resourceIndex < resourceCount; ++resourceIndex){
        const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, resourceIndex);
        FOUNDATION_ASSERT(resource.identity && resource.type < GpuGraphResourceType::Count);
    }
    for(usize completionIndex = 0u; completionIndex < externalCompletionCount; ++completionIndex)
        FOUNDATION_ASSERT(graph.externalCompletionAt(completionIndex).identity);
#endif
    b8 hasTaskExternalDependencies = false;
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskIndex);
        const GpuTaskId taskId{ .generation = generation, .index = static_cast<u32>(taskIndex) };
        FOUNDATION_ASSERT(task.identity);
        if(!IsValidCommandRequirements(task.commands) || !IsValidSchedulingHint(task.scheduling) || task.timing.policy >= GpuTaskTimingPolicy::Count)
            return fail(GpuTaskGraphAnalysisStatus::InvalidTask, taskId);
        const Span<const GpuTaskId> dependencies = CompilerGraphAccess::TaskDependencies(graph, task);
        for(const GpuTaskId dependency : dependencies){
            if(!CompilerGraphAccess::ValidTask(graph, dependency))
                return fail(GpuTaskGraphAnalysisStatus::InvalidTaskDependency, taskId, dependency);
        }
        const Span<const GpuExternalCompletionId> externalDependencies = CompilerGraphAccess::TaskExternalDependencies(graph, task);
        if(!externalDependencies.empty()){
            hasTaskExternalDependencies = true;
            for(const GpuExternalCompletionId dependency : externalDependencies){
                if(!CompilerGraphAccess::ValidExternalCompletion(graph, dependency))
                    return fail(GpuTaskGraphAnalysisStatus::InvalidExternalCompletionDependency, taskId);
            }
        }
        FOUNDATION_ASSERT(task.externalStateSourceCount == 0u || CompilerGraphAccess::TaskExternalStateSources(graph, task).data());
#if defined(NWB_DEBUG)
        for(const GpuTaskExternalStateSource& source : CompilerGraphAccess::TaskExternalStateSources(graph, task))
            FOUNDATION_ASSERT(source.states && source.applicableConsumerQueueClass <= CommandQueue::kCount);
#endif
        const Span<const GpuTaskResourceUse> resourceUses = CompilerGraphAccess::TaskResourceUses(graph, task);
        for(const GpuTaskResourceUse& use : resourceUses){
            if(!CompilerGraphAccess::ValidResource(graph, use.resource) || use.access >= GpuTaskResourceAccess::Count)
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, taskId, {}, use.resource);

            const GpuTaskGraphDetail::GpuGraphResourceNode& resource = CompilerGraphAccess::ResourceNode(graph, use.resource.index);
            FOUNDATION_ASSERT(resource.type != GpuGraphResourceType::HazardDomain || !CompilerGraphAccess::HasBackendResource(resource));
            if(
                (resource.type != GpuGraphResourceType::HazardDomain && use.requiredState == ResourceStates::Unknown)
                || (resource.type == GpuGraphResourceType::Texture && !use.range.textureSubresources.hasExtent())
                || (resource.type == GpuGraphResourceType::Buffer && !use.range.bufferRange.hasExtent())
            )
                return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, taskId, {}, use.resource);
        }
    }
    const Timer validationEnd = TimerNow();
    f64 validationSeconds = DurationInSeconds<f64>(validationEnd, validationBegin);

    const Timer dependencyAnalysisBegin = validationEnd;
    Vector<GpuTaskDependencyEdge, Alloc::ScratchArena> resourceVersionDependencyEdges(scratchArena);
    if(resourceVersionCount != 0u || CompilerGraphAccess::TotalResourceVersionUseCount(graph) != 0u){
        const auto resourceVersions = BuildResourceVersionDependencyEdges(graph, scratchArena, resourceVersionDependencyEdges);
        if(!resourceVersions){
            outAnalysis.m_diagnostic = resourceVersions.error();
            return false;
        }
    }
    GpuInferredDependencyCollector edgeCollector(scratchArena, outAnalysis.m_edges, outAnalysis.m_inferredEdges, outAnalysis.m_explicitEdgeCount, outAnalysis.m_inferredEdgeCount, outAnalysis.m_resourceVersionEdgeCount);
    const usize expectedEdgeCount = resourceVersionDependencyEdges.size() + CompilerGraphAccess::TotalDependencyCount(graph) + CompilerGraphAccess::TotalResourceUseCount(graph);
    edgeCollector.reserve(expectedEdgeCount, taskCount);
    Vector<u32, Alloc::ScratchArena> externalDependencyConsumerMarkers(hasTaskExternalDependencies ? externalCompletionCount : 0u, Limit<u32>::s_Max, scratchArena);

    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskIndex);
        const GpuTaskId taskId{ .generation = generation, .index = static_cast<u32>(taskIndex) };
        for(const GpuTaskId producer : CompilerGraphAccess::TaskDependencies(graph, task))
            edgeCollector.appendRaw(GpuTaskDependencyEdge{ .producer = producer, .consumer = taskId, .resource = {}, .resourceVersion = {}, .hazard = GpuTaskHazardType::Explicit, });
        if(hasTaskExternalDependencies){
            for(const GpuExternalCompletionId completion : CompilerGraphAccess::TaskExternalDependencies(graph, task)){
                if(externalDependencyConsumerMarkers[completion.index] == taskId.index)
                    continue;
                outAnalysis.m_externalDependencies.push_back(GpuTaskExternalDependencyEdge{ .completion = completion, .consumer = taskId, });
                externalDependencyConsumerMarkers[completion.index] = taskId.index;
            }
        }
    }
    for(const GpuTaskDependencyEdge& edge : resourceVersionDependencyEdges)
        edgeCollector.appendInferred(edge);
    const Timer semanticTopologyBegin = TimerNow();
    const f64 dependencyAnalysisSeconds = DurationInSeconds<f64>(semanticTopologyBegin, dependencyAnalysisBegin);

    TaskDependencyAdjacency dependencyAdjacency(scratchArena);
    {
        BuildTaskDependencyAdjacency(outAnalysis.m_edges, taskCount, scratchArena, dependencyAdjacency);
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
    const Timer hazardAnalysisBegin = TimerNow();
    const f64 semanticTopologySeconds = DurationInSeconds<f64>(hazardAnalysisBegin, semanticTopologyBegin);

    GpuTaskId invalidHazardTask;
    GpuGraphResourceId invalidHazardResource;
    if(!GpuResourceHazardTracker::Append(graph, outAnalysis.m_topologicalOrder, scratchArena, edgeCollector, invalidHazardTask, invalidHazardResource))
        return fail(GpuTaskGraphAnalysisStatus::InvalidResourceUse, invalidHazardTask, {}, invalidHazardResource);
    const Timer finalTopologyBegin = TimerNow();
    const f64 hazardAnalysisSeconds = DurationInSeconds<f64>(finalTopologyBegin, hazardAnalysisBegin);

    {
        if(dependencyAdjacency.edgeIndices.size() != outAnalysis.m_edges.size()){
            BuildTaskDependencyAdjacency(outAnalysis.m_edges, taskCount, scratchArena, dependencyAdjacency);
        }
        BuildSchedulingEdges(outAnalysis.m_edges, dependencyAdjacency, outAnalysis.m_topologicalOrder, scratchArena, outAnalysis.m_schedulingEdges);
        BuildSchedulingTaskAdjacency(
            outAnalysis.m_schedulingEdges,
            taskCount,
            generation,
            scratchArena,
            outAnalysis.m_schedulingOutgoingOffsets,
            outAnalysis.m_schedulingOutgoingConsumers,
            outAnalysis.m_schedulingIncomingOffsets,
            outAnalysis.m_schedulingIncomingProducers
        );
    }
    const Timer acceptedQueueFrontierValidationBegin = TimerNow();
    const f64 topologicalOrderSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(semanticTopologySeconds), VectorDoubleReplicate(DurationInSeconds<f64>(acceptedQueueFrontierValidationBegin, finalTopologyBegin))));

    if(!GpuAcceptedQueueFrontierValidator::Validate(graph, outAnalysis.m_schedulingIncomingOffsets, outAnalysis.m_edges, outAnalysis.m_diagnostic))
        return false;
    const Timer presentationValidationBegin = TimerNow();
    validationSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(validationSeconds), VectorDoubleReplicate(DurationInSeconds<f64>(presentationValidationBegin, acceptedQueueFrontierValidationBegin))));

    if(!GpuPresentationEndpointValidator::Validate(graph, outAnalysis.m_topologicalOrder, outAnalysis.m_schedulingIncomingOffsets, outAnalysis.m_schedulingIncomingProducers, scratchArena, outAnalysis.m_diagnostic))
        return false;
    validationSeconds = VectorDoubleGetX(VectorDoubleAdd(VectorDoubleReplicate(validationSeconds), VectorDoubleReplicate(DurationInSeconds<f64>(TimerNow(), presentationValidationBegin))));

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

