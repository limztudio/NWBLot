// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accepted_queue_frontier_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuAcceptedQueueFrontierValidator::validate(const GpuTaskGraph::DeclarationReadView& graph, const Span<const usize> incomingOffsets, const Span<const GpuTaskDependencyEdge> edges, GpuTaskGraphAnalysisDiagnostic& diagnostic)noexcept{
    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {})noexcept{
        diagnostic.status = status;
        diagnostic.task = task;
        diagnostic.relatedTask = relatedTask;
        diagnostic.resource = resource;
        diagnostic.resourceVersion = {};
        return false;
    };
    for(usize taskIndex = 0u; taskIndex < graph.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = graph.taskAt(taskIndex);
        if(task.scheduling.isRecoverySubmission && !task.scheduling.joinsAcceptedQueueFrontier)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id);
        if(!task.scheduling.joinsAcceptedQueueFrontier)
            continue;

        // Late recovery/finalization joins the latest accepted queue tokens at submission time.
        // It must not inherit a graph prerequisite whose rejection could make the join unavailable.
        // Scheduling edges are a reduced subset of these raw edges, so rejecting every raw incoming edge also covers inferred HazardDomain prerequisites.
        // An empty reduced incoming row proves that the raw graph has no prerequisite either.
        if(incomingOffsets[task.id.index] != incomingOffsets[task.id.index + 1u]){
            for(const GpuTaskDependencyEdge& edge : edges){
                if(edge.consumer == task.id){
                    return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id, edge.producer, edge.resource);
                }
            }
        }
        if(task.externalDependencyCount != 0u || task.externalStateSourceCount != 0u)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id);

        for(usize useIndex = 0u; useIndex < task.resourceUseCount; ++useIndex){
            const GpuTaskResourceUse& use = task.resourceUses[useIndex];
            if(graph.resourceAt(use.resource.index).type != GpuGraphResourceType::HazardDomain){
                return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, task.id, {}, use.resource);
            }
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
