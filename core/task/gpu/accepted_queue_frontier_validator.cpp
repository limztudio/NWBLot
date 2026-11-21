// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accepted_queue_frontier_validator.h"
#include "compiler_internal.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


b8 GpuAcceptedQueueFrontierValidator::Validate(const GpuTaskGraph::DeclarationReadView& graph, const Span<const usize> incomingOffsets, const Span<const GpuTaskDependencyEdge> edges, GpuTaskGraphAnalysisDiagnostic& diagnostic)noexcept{
    using namespace GpuTaskGraphCompilerDetail;
    if(!CompilerGraphAccess::HasAcceptedQueueFrontierOrRecoveryTask(graph))
        return true;

    const auto fail = [&](const GpuTaskGraphAnalysisStatus::Enum status, const GpuTaskId task = {}, const GpuTaskId relatedTask = {}, const GpuGraphResourceId resource = {})noexcept{
        diagnostic.status = status;
        diagnostic.task = task;
        diagnostic.relatedTask = relatedTask;
        diagnostic.resource = resource;
        diagnostic.resourceVersion = {};
        return false;
    };
    const usize taskCount = CompilerGraphAccess::TaskCount(graph);
    const u64 generation = CompilerGraphAccess::Generation(graph);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        const GpuTaskGraphDetail::GpuTaskNode& task = CompilerGraphAccess::TaskNode(graph, taskIndex);
        const GpuTaskId taskId{ .generation = generation, .index = static_cast<u32>(taskIndex) };
        if(task.scheduling.isRecoverySubmission && !task.scheduling.joinsAcceptedQueueFrontier)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, taskId);
        if(!task.scheduling.joinsAcceptedQueueFrontier)
            continue;

        if(incomingOffsets[taskIndex] != incomingOffsets[taskIndex + 1u]){
            for(const GpuTaskDependencyEdge& edge : edges){
                if(edge.consumer == taskId){
                    return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, taskId, edge.producer, edge.resource);
                }
            }
        }
        if(task.externalDependencyCount != 0u || task.externalStateSourceCount != 0u)
            return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, taskId);

        const Span<const GpuTaskResourceUse> resourceUses = CompilerGraphAccess::TaskResourceUses(graph, task);
        for(const GpuTaskResourceUse& use : resourceUses){
            if(CompilerGraphAccess::ResourceNode(graph, use.resource.index).type != GpuGraphResourceType::HazardDomain){
                return fail(GpuTaskGraphAnalysisStatus::InvalidAcceptedQueueFrontierTask, taskId, {}, use.resource);
            }
        }
    }
    return true;
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

