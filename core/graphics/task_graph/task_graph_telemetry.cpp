// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::appendFrameGraphTelemetry(
    Telemetry::FrameGraphBuilder& builder,
    const GpuTaskGraphAnalysis& analysis,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphTelemetryOptions& options
)const{
    if(
        !analysis.validFor(*this)
        || m_tasks.empty()
        || (options.queueAssignments && !options.queueAssignments->validFor(*this))
    )
        return false;
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
            case CommandQueue::Transfer:
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedTransferQueue;
                break;
            default:
                return false;
            }
            if(assignment->dedicated)
                flags |= GpuTaskGraphTelemetryNodeFlag::AssignedDedicatedQueue;
            if(assignment->reason == GpuTaskQueueAssignmentReason::Fallback)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentFallback;
            if(assignment->reason == GpuTaskQueueAssignmentReason::CompilerOverride)
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentCompilerOverride;
            if(
                (assignment->modifiers & GpuTaskQueueAssignmentModifier::DirectDependencyAffinity)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::NonPrimaryPreference)
                || assignment->initialQueue != assignment->queue
            )
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentSameClassRouting;
            if(
                (assignment->modifiers & GpuTaskQueueAssignmentModifier::DebugTimingOverride)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::TimingCalibration)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::TimingFeedback)
            )
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentTimingRouting;
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


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

