// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"
#include "queue_assignment_telemetry.h"

#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_telemetry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool TranslateQueueClass(
    const CommandQueue::Enum queueClass,
    Telemetry::FrameGraphQueueClass::Enum& outQueueClass
)noexcept{
    switch(queueClass){
    case CommandQueue::Graphics:
        outQueueClass = Telemetry::FrameGraphQueueClass::Graphics;
        return true;
    case CommandQueue::Compute:
        outQueueClass = Telemetry::FrameGraphQueueClass::Compute;
        return true;
    case CommandQueue::Transfer:
        outQueueClass = Telemetry::FrameGraphQueueClass::Transfer;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool TranslateReason(
    const GpuTaskQueueAssignmentReason::Enum reason,
    Telemetry::FrameGraphQueueAssignmentReason::Enum& outReason
)noexcept{
    switch(reason){
    case GpuTaskQueueAssignmentReason::RequiredGraphics:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics;
        return true;
    case GpuTaskQueueAssignmentReason::PreferredQueue:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::PreferredQueue;
        return true;
    case GpuTaskQueueAssignmentReason::DedicatedCompute:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::DedicatedCompute;
        return true;
    case GpuTaskQueueAssignmentReason::DedicatedTransfer:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::DedicatedTransfer;
        return true;
    case GpuTaskQueueAssignmentReason::Fallback:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::Fallback;
        return true;
    case GpuTaskQueueAssignmentReason::ConservativeAny:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::ConservativeAny;
        return true;
    case GpuTaskQueueAssignmentReason::SameClassRouting:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::SameClassRouting;
        return true;
    case GpuTaskQueueAssignmentReason::CompilerOverride:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::CompilerOverride;
        return true;
    case GpuTaskQueueAssignmentReason::ScoredAny:
        outReason = Telemetry::FrameGraphQueueAssignmentReason::ScoredAny;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool TranslateModifiers(
    const GpuTaskQueueAssignmentModifier::Mask modifiers,
    Telemetry::FrameGraphQueueAssignmentModifier::Mask& outModifiers
)noexcept{
    constexpr u8 s_KnownModifiers = GpuTaskQueueAssignmentModifier::DirectDependencyAffinity
        | GpuTaskQueueAssignmentModifier::SameClassLoadBalance
        | GpuTaskQueueAssignmentModifier::NonPrimaryPreference
        | GpuTaskQueueAssignmentModifier::DebugTimingOverride
        | GpuTaskQueueAssignmentModifier::TimingCalibration
        | GpuTaskQueueAssignmentModifier::TimingFeedback
    ;
    if((static_cast<u8>(modifiers) & static_cast<u8>(~s_KnownModifiers)) != 0u)
        return false;

    u8 translated = Telemetry::FrameGraphQueueAssignmentModifier::None;
    if(modifiers & GpuTaskQueueAssignmentModifier::DirectDependencyAffinity)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::DirectDependencyAffinity;
    if(modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::SameClassLoadBalance;
    if(modifiers & GpuTaskQueueAssignmentModifier::NonPrimaryPreference)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::NonPrimaryPreference;
    if(modifiers & GpuTaskQueueAssignmentModifier::DebugTimingOverride)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::DebugTimingOverride;
    if(modifiers & GpuTaskQueueAssignmentModifier::TimingCalibration)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::TimingCalibration;
    if(modifiers & GpuTaskQueueAssignmentModifier::TimingFeedback)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    outModifiers = static_cast<Telemetry::FrameGraphQueueAssignmentModifier::Mask>(translated);
    return true;
}

[[nodiscard]] static bool TranslateAcceptance(
    const GpuTaskQueueAssignmentAcceptance::Enum acceptance,
    Telemetry::FrameGraphQueueAssignmentAcceptance::Enum& outAcceptance
)noexcept{
    switch(acceptance){
    case GpuTaskQueueAssignmentAcceptance::NotAccepted:
        outAcceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted;
        return true;
    case GpuTaskQueueAssignmentAcceptance::First:
        outAcceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::First;
        return true;
    case GpuTaskQueueAssignmentAcceptance::Unchanged:
        outAcceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::Unchanged;
        return true;
    case GpuTaskQueueAssignmentAcceptance::Changed:
        outAcceptance = Telemetry::FrameGraphQueueAssignmentAcceptance::Changed;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool BuildQueueAssignment(
    const GpuTaskQueueAssignment& assignment,
    const GpuTaskQueueAssignmentTelemetry* const accepted,
    Telemetry::FrameGraphQueueAssignment& outAssignment
)noexcept{
    outAssignment = {};
    outAssignment.initialQueue = {
        .index = assignment.initialQueue.index,
        .deviceGeneration = assignment.initialQueue.deviceGeneration,
    };
    outAssignment.plannedQueue = {
        .index = assignment.queue.index,
        .deviceGeneration = assignment.queue.deviceGeneration,
    };
    outAssignment.score = {
        .preference = assignment.score.preference,
        .overlap = assignment.score.overlap,
        .queueLoad = assignment.score.queueLoad,
        .incomingCrossings = assignment.score.incomingCrossings,
        .outgoingCrossings = assignment.score.outgoingCrossings,
        .ownershipTransfers = assignment.score.ownershipTransfers,
        .total = assignment.score.total(),
    };
    outAssignment.dedicated = assignment.dedicated;
    outAssignment.present = true;
    if(
        !TranslateQueueClass(assignment.queueClass, outAssignment.queueClass)
        || !TranslateReason(assignment.reason, outAssignment.reason)
        || !TranslateModifiers(assignment.modifiers, outAssignment.modifiers)
    )
        return false;

    if(accepted){
        outAssignment.acceptedQueue = {
            .index = accepted->acceptedQueue.index,
            .deviceGeneration = accepted->acceptedQueue.deviceGeneration,
        };
        outAssignment.previousAcceptedQueue = {
            .index = accepted->previousAcceptedQueue.index,
            .deviceGeneration = accepted->previousAcceptedQueue.deviceGeneration,
        };
        if(!TranslateAcceptance(accepted->acceptance, outAssignment.acceptance))
            return false;
    }
    return Telemetry::IsValidFrameGraphQueueAssignment(outAssignment);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
        || ((options.compiledGraph == nullptr) != (options.queueAssignmentTelemetry == nullptr))
        || (
            options.queueAssignmentTelemetry
            && (
                !options.queueAssignments
                || !options.compiledGraph
                || !options.queueAssignmentTelemetry->validFor(
                    *this,
                    *options.queueAssignments,
                    *options.compiledGraph
                )
            )
        )
    )
        return false;
    if(options.queueAssignments){
        for(usize taskIndex = 0u; taskIndex < m_tasks.size(); ++taskIndex){
            if(!options.queueAssignments->find(taskAt(taskIndex).id))
                return false;
            if(
                options.queueAssignmentTelemetry
                && !options.queueAssignmentTelemetry->find(taskAt(taskIndex).id)
            )
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
        Telemetry::FrameGraphQueueAssignment telemetryAssignment;
        if(options.queueAssignments){
            const GpuTaskQueueAssignment* const assignment = options.queueAssignments->find(task.id);
            NWB_ASSERT(assignment);
            const GpuTaskQueueAssignmentTelemetry* const accepted = options.queueAssignmentTelemetry
                ? options.queueAssignmentTelemetry->find(task.id)
                : nullptr
            ;
            if(!__hidden_task_graph_telemetry::BuildQueueAssignment(
                *assignment,
                accepted,
                telemetryAssignment
            ))
                return false;
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
        taskNodes.push_back(options.queueAssignments
            ? builder.addPass(task.identity, task.markerLabel, telemetryAssignment, flags)
            : builder.addPass(task.identity, task.markerLabel, flags)
        );
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

