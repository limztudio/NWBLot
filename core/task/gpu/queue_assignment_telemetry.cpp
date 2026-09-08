// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "queue_assignment_telemetry.h"

#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_queue_assignment_telemetry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool MatchesAssignment(
    const GpuTaskQueueAssignment& lhs,
    const GpuTaskQueueAssignment& rhs
)noexcept{
    return lhs.task == rhs.task
        && lhs.initialQueue == rhs.initialQueue
        && lhs.queue == rhs.queue
        && lhs.score.preference == rhs.score.preference
        && lhs.score.overlap == rhs.score.overlap
        && lhs.score.queueLoad == rhs.score.queueLoad
        && lhs.score.incomingCrossings == rhs.score.incomingCrossings
        && lhs.score.outgoingCrossings == rhs.score.outgoingCrossings
        && lhs.score.ownershipTransfers == rhs.score.ownershipTransfers
        && lhs.queueClass == rhs.queueClass
        && lhs.reason == rhs.reason
        && lhs.modifiers == rhs.modifiers
        && lhs.dedicated == rhs.dedicated
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuTaskGraphQueueAssignmentTelemetryTracker::reset(){
    m_current.clear();
    m_history.clear();
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_planGeneration = 0u;
    m_recordingAttemptGeneration = 0u;
    m_acceptanceRevision = 0u;
    m_deviceGeneration = 0u;
    m_valid = false;
}

bool GpuTaskGraphQueueAssignmentTelemetryTracker::update(
    const GpuTaskGraph::DeclarationReadView& declarations,
    const GpuTaskGraphQueueAssignments& assignments,
    const GpuCompiledGraph::ReadView& compiledPlan,
    const GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena
){
    if(!declarations.valid() || !compiledPlan.validFor(declarations))
        return false;

    const bool currentMatchesPlan = validFor(declarations, assignments, compiledPlan);
    Vector<QueueSubmissionToken, Alloc::ScratchArena> acceptedPacketTokens(
        compiledPlan.packetCount(),
        scratchArena
    );
    GpuGraphSubmissionAcceptanceSnapshot acceptanceSnapshot;
    const bool sourcesValid = assignments.validFor(declarations, compiledPlan)
        && compiledPlan.deviceGeneration() != 0u
        && transaction.copyAcceptedPacketTokens(
            compiledPlan,
            acceptedPacketTokens.data(),
            acceptedPacketTokens.size(),
            acceptanceSnapshot
        )
    ;
    if(
        sourcesValid
        && currentMatchesPlan
        && m_recordingAttemptGeneration == acceptanceSnapshot.recordingAttemptGeneration
        && m_acceptanceRevision == acceptanceSnapshot.acceptanceRevision
    )
        return true;

    const bool sameAcceptanceAttempt = currentMatchesPlan
        && m_recordingAttemptGeneration == acceptanceSnapshot.recordingAttemptGeneration
        && acceptanceSnapshot.recordingAttemptGeneration != 0u
    ;
    m_generation = 0u;
    m_declarationRevision = 0u;
    m_planGeneration = 0u;
    m_recordingAttemptGeneration = 0u;
    m_acceptanceRevision = 0u;
    m_valid = false;
    if(!sourcesValid)
        return false;

    HashMap<Name, u8, Hasher<Name>, EqualTo<Name>, Alloc::ScratchArena> taskNames(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    taskNames.reserve(declarations.taskCount());
    for(usize taskIndex = 0u; taskIndex < declarations.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
        if(!task.identity || !taskNames.emplace(task.identity, 0u).second)
            return false;
    }

    const bool hasCurrentGenerationHistory = m_deviceGeneration == compiledPlan.deviceGeneration();
    Vector<GpuTaskQueueAssignmentTelemetry, Alloc::ScratchArena> staged(scratchArena);
    staged.reserve(declarations.taskCount());
    for(usize taskIndex = 0u; taskIndex < declarations.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
        const GpuTaskQueueAssignment* const assignment = assignments.find(task.id);
        const GpuSubmissionPacketId packet = compiledPlan.packetForTask(task.id);
        const GpuCompiledPacketView packetView = compiledPlan.packet(packet);
        if(
            !assignment
            || !packetView.valid()
            || assignment->task != task.id
            || assignment->queue != packetView.plan->queue
        )
            return false;

        const GpuPhysicalQueueInfo* const initialQueueInfo = compiledPlan.queueInfo(assignment->initialQueue);
        const GpuPhysicalQueueInfo* const queueInfo = compiledPlan.queueInfo(assignment->queue);
        if(
            !assignment->initialQueue.valid()
            || !assignment->queue.valid()
            || !initialQueueInfo
            || !queueInfo
            || assignment->initialQueue.deviceGeneration != compiledPlan.deviceGeneration()
            || assignment->queue.deviceGeneration != compiledPlan.deviceGeneration()
            || assignment->queueClass != queueInfo->queueClass
            || assignment->dedicated != queueInfo->dedicated
        )
            return false;

        GpuTaskQueueAssignmentTelemetry telemetry{
            .assignment = *assignment,
            .acceptedQueue = {},
            .previousAcceptedQueue = {},
            .acceptance = GpuTaskQueueAssignmentAcceptance::NotAccepted,
        };
        const auto previous = hasCurrentGenerationHistory ? m_history.find(task.identity) : m_history.end();
        const bool hasPrevious = hasCurrentGenerationHistory && previous != m_history.end();
        const QueueSubmissionToken& token = acceptedPacketTokens[packet.index];
        if(!token.valid()){
            if(hasPrevious)
                telemetry.previousAcceptedQueue = previous.value();
            staged.push_back(telemetry);
            continue;
        }
        if(
            token.queue != assignment->queueClass
            || !token.matchesPhysicalQueue(assignment->queue.index, assignment->queue.deviceGeneration)
        )
            return false;

        telemetry.acceptedQueue = assignment->queue;
        const GpuTaskQueueAssignmentTelemetry* const previousCurrent = sameAcceptanceAttempt
            ? &m_current[taskIndex]
            : nullptr
        ;
        if(
            previousCurrent
            && previousCurrent->acceptedQueue == assignment->queue
            && previousCurrent->acceptance != GpuTaskQueueAssignmentAcceptance::NotAccepted
        ){
            telemetry.previousAcceptedQueue = previousCurrent->previousAcceptedQueue;
            telemetry.acceptance = previousCurrent->acceptance;
        }
        else if(!hasPrevious){
            telemetry.acceptance = GpuTaskQueueAssignmentAcceptance::First;
        }
        else if(previous.value() == assignment->queue){
            telemetry.previousAcceptedQueue = previous.value();
            telemetry.acceptance = GpuTaskQueueAssignmentAcceptance::Unchanged;
        }
        else{
            telemetry.previousAcceptedQueue = previous.value();
            telemetry.acceptance = GpuTaskQueueAssignmentAcceptance::Changed;
        }
        staged.push_back(telemetry);
    }

    if(!hasCurrentGenerationHistory)
        m_history.clear();
    m_deviceGeneration = compiledPlan.deviceGeneration();
    m_current.clear();
    m_current.reserve(staged.size());
    for(usize taskIndex = 0u; taskIndex < staged.size(); ++taskIndex){
        const GpuTaskQueueAssignmentTelemetry& telemetry = staged[taskIndex];
        m_current.push_back(telemetry);
        if(!telemetry.acceptedQueue.valid())
            continue;

        const Name taskName = declarations.taskAt(taskIndex).identity;
        auto previous = m_history.find(taskName);
        if(previous == m_history.end())
            m_history.emplace(taskName, telemetry.acceptedQueue);
        else
            previous.value() = telemetry.acceptedQueue;
    }
    m_generation = declarations.generation();
    m_declarationRevision = declarations.declarationRevision();
    m_planGeneration = compiledPlan.planGeneration();
    m_recordingAttemptGeneration = acceptanceSnapshot.recordingAttemptGeneration;
    m_acceptanceRevision = acceptanceSnapshot.acceptanceRevision;
    m_valid = true;
    return true;
}

bool GpuTaskGraphQueueAssignmentTelemetryTracker::validFor(
    const GpuTaskGraph::DeclarationReadView& declarations,
    const GpuTaskGraphQueueAssignments& assignments,
    const GpuCompiledGraph::ReadView& compiledPlan
)const{
    if(!declarations.valid() || !compiledPlan.validFor(declarations))
        return false;

    if(
        !m_valid
        || !assignments.validFor(declarations, compiledPlan)
        || m_generation != declarations.generation()
        || m_declarationRevision != declarations.declarationRevision()
        || m_planGeneration != compiledPlan.planGeneration()
        || m_deviceGeneration != compiledPlan.deviceGeneration()
        || m_current.size() != declarations.taskCount()
    )
        return false;

    for(usize taskIndex = 0u; taskIndex < declarations.taskCount(); ++taskIndex){
        const GpuTaskGraphTaskView task = declarations.taskAt(taskIndex);
        const GpuTaskQueueAssignment* const assignment = assignments.find(task.id);
        if(
            !assignment
            || m_current[taskIndex].assignment.task != task.id
            || !__hidden_queue_assignment_telemetry::MatchesAssignment(
                m_current[taskIndex].assignment,
                *assignment
            )
        )
            return false;
    }
    return true;
}

const GpuTaskQueueAssignmentTelemetry* GpuTaskGraphQueueAssignmentTelemetryTracker::find(
    const GpuTaskId task
)const noexcept{
    if(
        !m_valid
        || !task.valid()
        || task.generation != m_generation
        || task.index >= m_current.size()
        || m_current[task.index].assignment.task != task
    )
        return nullptr;
    return &m_current[task.index];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

