// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"
#include "dependency_pair_hash.h"
#include "queue_assignment_telemetry.h"

#include <core/telemetry/frame_graph_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_telemetry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 DependencyPairKey(const GpuTaskDependencyEdge& edge)noexcept{
    return (static_cast<u64>(edge.producer.index) << 32u) | edge.consumer.index;
}

[[nodiscard]] static Expected<Telemetry::FrameGraphQueueClass::Enum> TranslateQueueClass(
    const CommandQueue::Enum queueClass
)noexcept{
    switch(queueClass){
    case CommandQueue::Graphics:
        return Telemetry::FrameGraphQueueClass::Graphics;
    case CommandQueue::Compute:
        return Telemetry::FrameGraphQueueClass::Compute;
    case CommandQueue::Transfer:
        return Telemetry::FrameGraphQueueClass::Transfer;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static Expected<Telemetry::FrameGraphQueueAssignmentReason::Enum> TranslateReason(
    const GpuTaskQueueAssignmentReason::Enum reason
)noexcept{
    switch(reason){
    case GpuTaskQueueAssignmentReason::RequiredGraphics:
        return Telemetry::FrameGraphQueueAssignmentReason::RequiredGraphics;
    case GpuTaskQueueAssignmentReason::Conservative:
        return Telemetry::FrameGraphQueueAssignmentReason::Conservative;
    case GpuTaskQueueAssignmentReason::Scored:
        return Telemetry::FrameGraphQueueAssignmentReason::Scored;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static Expected<Telemetry::FrameGraphQueueAssignmentModifier::Mask> TranslateModifiers(
    const GpuTaskQueueAssignmentModifier::Mask modifiers
)noexcept{
    constexpr u8 s_KnownModifiers = GpuTaskQueueAssignmentModifier::DirectDependencyAffinity
        | GpuTaskQueueAssignmentModifier::SameClassLoadBalance
        | GpuTaskQueueAssignmentModifier::NonPrimaryRouting
        | GpuTaskQueueAssignmentModifier::DiagnosticTimingQueueOverride
        | GpuTaskQueueAssignmentModifier::TimingCalibration
        | GpuTaskQueueAssignmentModifier::TimingFeedback
        | GpuTaskQueueAssignmentModifier::DiagnosticQueueOverride
    ;
    if((static_cast<u8>(modifiers) & static_cast<u8>(~s_KnownModifiers)) != 0u)
        return MakeUnexpected(Failure{});

    u8 translated = Telemetry::FrameGraphQueueAssignmentModifier::None;
    if(modifiers & GpuTaskQueueAssignmentModifier::DirectDependencyAffinity)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::DirectDependencyAffinity;
    if(modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::SameClassLoadBalance;
    if(modifiers & GpuTaskQueueAssignmentModifier::NonPrimaryRouting)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::NonPrimaryRouting;
    if(modifiers & GpuTaskQueueAssignmentModifier::DiagnosticTimingQueueOverride)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::DiagnosticTimingQueueOverride;
    if(modifiers & GpuTaskQueueAssignmentModifier::TimingCalibration)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::TimingCalibration;
    if(modifiers & GpuTaskQueueAssignmentModifier::TimingFeedback)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::TimingFeedback;
    if(modifiers & GpuTaskQueueAssignmentModifier::DiagnosticQueueOverride)
        translated |= Telemetry::FrameGraphQueueAssignmentModifier::DiagnosticQueueOverride;
    return static_cast<Telemetry::FrameGraphQueueAssignmentModifier::Mask>(translated);
}

[[nodiscard]] static Expected<Telemetry::FrameGraphQueueAssignmentAcceptance::Enum> TranslateAcceptance(
    const GpuTaskQueueAssignmentAcceptance::Enum acceptance
)noexcept{
    switch(acceptance){
    case GpuTaskQueueAssignmentAcceptance::NotAccepted:
        return Telemetry::FrameGraphQueueAssignmentAcceptance::NotAccepted;
    case GpuTaskQueueAssignmentAcceptance::First:
        return Telemetry::FrameGraphQueueAssignmentAcceptance::First;
    case GpuTaskQueueAssignmentAcceptance::Unchanged:
        return Telemetry::FrameGraphQueueAssignmentAcceptance::Unchanged;
    case GpuTaskQueueAssignmentAcceptance::Changed:
        return Telemetry::FrameGraphQueueAssignmentAcceptance::Changed;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static Expected<Telemetry::FrameGraphTaskPacketizationDecision::Enum> TranslatePacketizationDecision(
    const GpuTaskPacketizationDecision::Enum decision
)noexcept{
    switch(decision){
    case GpuTaskPacketizationDecision::FirstTask:
        return Telemetry::FrameGraphTaskPacketizationDecision::FirstTask;
    case GpuTaskPacketizationDecision::MergeNotRequested:
        return Telemetry::FrameGraphTaskPacketizationDecision::MergeNotRequested;
    case GpuTaskPacketizationDecision::TaskForcesBoundary:
        return Telemetry::FrameGraphTaskPacketizationDecision::TaskForcesBoundary;
    case GpuTaskPacketizationDecision::QueueChanged:
        return Telemetry::FrameGraphTaskPacketizationDecision::QueueChanged;
    case GpuTaskPacketizationDecision::PrecedingTaskForcesBoundary:
        return Telemetry::FrameGraphTaskPacketizationDecision::PrecedingTaskForcesBoundary;
    case GpuTaskPacketizationDecision::ScoredMergeIneligible:
        return Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeIneligible;
    case GpuTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency:
        return Telemetry::FrameGraphTaskPacketizationDecision::MergeRequiresExplicitImmediateDependency;
    case GpuTaskPacketizationDecision::CrossQueueConsumerFrontier:
        return Telemetry::FrameGraphTaskPacketizationDecision::CrossQueueConsumerFrontier;
    case GpuTaskPacketizationDecision::MergedExplicit:
        return Telemetry::FrameGraphTaskPacketizationDecision::MergedExplicit;
    case GpuTaskPacketizationDecision::MergedFrontierScored:
        return Telemetry::FrameGraphTaskPacketizationDecision::MergedFrontierScored;
    case GpuTaskPacketizationDecision::ScoredMergeDomainMismatch:
        return Telemetry::FrameGraphTaskPacketizationDecision::ScoredMergeDomainMismatch;
    default:
        return MakeUnexpected(Failure{});
    }
}

[[nodiscard]] static Expected<Telemetry::FrameGraphQueueAssignment> BuildQueueAssignment(
    const GpuTaskQueueAssignment& assignment,
    const GpuTaskQueueAssignmentTelemetry* const accepted
)noexcept{
    Telemetry::FrameGraphQueueAssignment result;
    result.initialQueue = {
        .index = assignment.initialQueue.index,
        .deviceGeneration = assignment.initialQueue.deviceGeneration,
    };
    result.plannedQueue = {
        .index = assignment.queue.index,
        .deviceGeneration = assignment.queue.deviceGeneration,
    };
    result.score = {
        .overlap = assignment.score.overlap,
        .queueLoad = assignment.score.queueLoad,
        .incomingCrossings = assignment.score.incomingCrossings,
        .outgoingCrossings = assignment.score.outgoingCrossings,
        .ownershipTransfers = assignment.score.ownershipTransfers,
        .total = assignment.score.total(),
    };
    result.dedicated = assignment.dedicated;
    result.present = true;
    const auto queueClass = TranslateQueueClass(assignment.queueClass);
    if(!queueClass)
        return MakeUnexpected(Failure{});
    const auto reason = TranslateReason(assignment.reason);
    if(!reason)
        return MakeUnexpected(Failure{});
    const auto modifiers = TranslateModifiers(assignment.modifiers);
    if(!modifiers)
        return MakeUnexpected(Failure{});
    result.queueClass = *queueClass;
    result.reason = *reason;
    result.modifiers = *modifiers;

    if(accepted){
        result.acceptedQueue = {
            .index = accepted->acceptedQueue.index,
            .deviceGeneration = accepted->acceptedQueue.deviceGeneration,
        };
        result.previousAcceptedQueue = {
            .index = accepted->previousAcceptedQueue.index,
            .deviceGeneration = accepted->previousAcceptedQueue.deviceGeneration,
        };
        const auto acceptance = TranslateAcceptance(accepted->acceptance);
        if(!acceptance)
            return MakeUnexpected(Failure{});
        result.acceptance = *acceptance;
    }
    if(!Telemetry::IsValidFrameGraphQueueAssignment(result))
        return MakeUnexpected(Failure{});
    return result;
}

[[nodiscard]] static Expected<Telemetry::FrameGraphCompiledTask> BuildCompiledTask(
    const GpuCompiledGraph::ReadView& compiledPlan,
    const GpuTaskId task
)noexcept{
    const GpuCompiledTaskView compiledTask = compiledPlan.findTask(task);
    if(!compiledTask.valid() || !compiledPlan.validPacket(compiledTask.plan->packet))
        return MakeUnexpected(Failure{});

    Telemetry::FrameGraphCompiledTask result{
        .planGeneration = compiledTask.plan->packet.generation,
        .packetIndex = compiledTask.plan->packet.index,
        .packetizationDecision = Telemetry::FrameGraphTaskPacketizationDecision::Unknown,
        .present = true,
    };
    const auto decision = TranslatePacketizationDecision(compiledTask.plan->packetizationDecision);
    if(!decision)
        return MakeUnexpected(Failure{});
    result.packetizationDecision = *decision;
    if(!Telemetry::IsValidFrameGraphCompiledTask(result))
        return MakeUnexpected(Failure{});
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphDeclarationReadView::appendFrameGraphTelemetry(
    Telemetry::FrameGraphBuilder& builder,
    const GpuTaskGraphAnalysis& analysis,
    Alloc::ScratchArena& scratchArena,
    const GpuTaskGraphTelemetryOptions& options
)const{
    if(!m_graph)
        return false;

    if(
        !analysis.validFor(*this)
        || taskCount() == 0u
    )
        return false;
    if(options.compiledPlan && !options.compiledPlan->validFor(*this))
        return false;
    if(
        options.queueAssignments
        && (
            options.compiledPlan
            ? !options.queueAssignments->validFor(*this, *options.compiledPlan)
            : !options.queueAssignments->validFor(*this)
        )
    )
        return false;
    if(
        options.queueAssignmentTelemetry
        && (
            !options.queueAssignments
            || !options.compiledPlan
            || !options.queueAssignmentTelemetry->validFor(
                *this,
                *options.queueAssignments,
                *options.compiledPlan
            )
        )
    )
        return false;
    if(options.queueAssignments){
        for(usize taskIndex = 0u; taskIndex < taskCount(); ++taskIndex){
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
    resourceNodes.reserve(resourceCount());
    completionNodes.reserve(externalCompletionCount());
    taskNodes.reserve(taskCount());

    for(usize resourceIndex = 0u; resourceIndex < resourceCount(); ++resourceIndex){
        const GpuTaskGraphResourceView resource = resourceAt(resourceIndex);
        resourceNodes.push_back(builder.addResource(resource.identity, resource.markerLabel));
    }
    for(usize completionIndex = 0u; completionIndex < externalCompletionCount(); ++completionIndex){
        const GpuTaskGraphExternalCompletionView completion = externalCompletionAt(completionIndex);
        completionNodes.push_back(builder.addExternal(completion.identity, completion.markerLabel));
    }
    for(usize taskIndex = 0u; taskIndex < taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = taskAt(taskIndex);
        u8 flags = GpuTaskGraphTelemetryNodeFlag::None;
        Telemetry::FrameGraphPassMetadata metadata;
        if(options.compiledPlan){
            const auto compiledTask = __hidden_task_graph_telemetry::BuildCompiledTask(*options.compiledPlan, task.id);
            if(!compiledTask)
                return false;
            metadata.compiledTask = *compiledTask;
        }
        if(options.queueAssignments){
            const GpuTaskQueueAssignment* const assignment = options.queueAssignments->find(task.id);
            NWB_ASSERT(assignment);
            const GpuTaskQueueAssignmentTelemetry* const accepted = options.queueAssignmentTelemetry
                ? options.queueAssignmentTelemetry->find(task.id)
                : nullptr
            ;
            const auto queueAssignment = __hidden_task_graph_telemetry::BuildQueueAssignment(*assignment, accepted);
            if(!queueAssignment)
                return false;
            metadata.queueAssignment = *queueAssignment;
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
            if(
                (assignment->modifiers & GpuTaskQueueAssignmentModifier::DirectDependencyAffinity)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::SameClassLoadBalance)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::NonPrimaryRouting)
                || assignment->initialQueue != assignment->queue
            )
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentRerouted;
            if(
                (assignment->modifiers & GpuTaskQueueAssignmentModifier::DiagnosticTimingQueueOverride)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::TimingCalibration)
                || (assignment->modifiers & GpuTaskQueueAssignmentModifier::TimingFeedback)
            )
                flags |= GpuTaskGraphTelemetryNodeFlag::QueueAssignmentTimingRouting;
        }
        taskNodes.push_back(builder.addPass(task.identity, task.markerLabel, metadata, flags));
    }

    for(usize taskIndex = 0u; taskIndex < taskCount(); ++taskIndex){
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
    HashMap<u64, u8, Alloc::ScratchArena, GpuTaskDependencyPairHasher, EqualTo<u64>> inferredFlags(
        0u,
        GpuTaskDependencyPairHasher{},
        EqualTo<u64>{},
        scratchArena
    );
    inferredFlags.reserve(Min(analysis.edges().size(), analysis.inferredEdges().size()));
    for(const GpuTaskDependencyEdge& edge : analysis.inferredEdges()){
        u8 flags = GpuTaskGraphTelemetryEdgeFlag::InferredDependency;
        if(edge.hazard == GpuTaskHazardType::VersionDependency)
            flags |= GpuTaskGraphTelemetryEdgeFlag::VersionDependency;
        else if(edge.hazard == GpuTaskHazardType::VersionLifetime)
            flags |= GpuTaskGraphTelemetryEdgeFlag::VersionLifetime;
        auto [annotation, inserted] = inferredFlags.try_emplace(__hidden_task_graph_telemetry::DependencyPairKey(edge), flags);
        if(!inserted)
            annotation.value() |= flags;
    }
    for(const GpuTaskDependencyEdge& edge : analysis.edges()){
        // Analysis keeps one raw edge per pair and gives explicit dependencies precedence over inferred hazards.
        u8 flags = edge.hazard == GpuTaskHazardType::Explicit
            ? GpuTaskGraphTelemetryEdgeFlag::ExplicitDependency
            : GpuTaskGraphTelemetryEdgeFlag::None
        ;
        const auto annotation = inferredFlags.find(__hidden_task_graph_telemetry::DependencyPairKey(edge));
        if(annotation != inferredFlags.end())
            flags |= annotation->second;
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

