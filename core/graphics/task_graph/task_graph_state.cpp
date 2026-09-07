// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"
#include "compiler.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskGraph::ResetCompletionScope::ResetCompletionScope(GpuTaskGraph& graph)noexcept
    : m_graph(graph)
{}

GpuTaskGraph::ResetCompletionScope::~ResetCompletionScope(){
    complete();
}

void GpuTaskGraph::ResetCompletionScope::activateWithinLock()noexcept{
    const bool activationValid = !m_active && m_graph.m_teardownInProgress;
    NWB_FATAL_ASSERT_MSG(activationValid, "GPU task graph reset completion requires fresh teardown ownership");
    if(!activationValid)
        TerminateInvariant();
    m_active = true;
}

void GpuTaskGraph::ResetCompletionScope::complete()noexcept{
    if(!m_active)
        return;
    m_graph.completeResetWithoutCallbacks();
    m_active = false;
}


GpuTaskGraph::RecordingAttemptScope::~RecordingAttemptScope()noexcept{
    if(m_graph)
        m_graph->cancelRecordingAttempt(*this);
}


void GpuTaskGraph::RecordingAttemptScope::complete()noexcept{
    if(m_graph){
        m_graph->completeRecordingPreparation(*this);
        return;
    }
    completeWithinLock();
}


void GpuTaskGraph::RecordingAttemptScope::completeWithinLock()noexcept{
    m_graph = nullptr;
    m_compiledGraph = nullptr;
    m_recordingAttemptGeneration = 0u;
    m_preparationSerial = 0u;
    m_previousPlanWasActive = false;
}


bool GpuTaskGraph::RecordingAttemptScope::validPreparationWithinLock(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const u64 preparationSerial
)const noexcept{
    return m_graph == &graph
        && m_compiledGraph == &compiledGraph
        && m_recordingAttemptGeneration == recordingAttemptGeneration
        && m_preparationSerial != 0u
        && m_preparationSerial == preparationSerial
    ;
}


void GpuTaskGraph::RecordingAttemptScope::activateWithinLock(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const u64 preparationSerial,
    const bool previousPlanWasActive
)noexcept{
    NWB_FATAL_ASSERT_MSG(!m_graph, "GPU task graph recording attempt scope must be fresh");
    NWB_FATAL_ASSERT_MSG(preparationSerial != 0u, "GPU task graph recording preparation requires a nonzero serial");
    if(m_graph || preparationSerial == 0u)
        TerminateInvariant();
    m_graph = &graph;
    m_compiledGraph = &compiledGraph;
    m_recordingAttemptGeneration = recordingAttemptGeneration;
    m_preparationSerial = preparationSerial;
    m_previousPlanWasActive = previousPlanWasActive;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraph::tryReset(){
    ResetCompletionScope resetCompletion(*this);
    {
        ScopedLock lock(m_lifecycleMutex);
        if(
            m_teardownInProgress
            || m_activeDeclarationAccessCount != 0u
            || m_activeDiscardNotificationCount != 0u
            || m_activePacketRecordingClaimCount.load(MemoryOrder::acquire) != 0u
            || m_activeRecordingPreparationSerial != 0u
            || m_submissionBindingState == SubmissionBindingState::Active
            || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        )
            return false;
        for(const GpuTaskNode& task : m_tasks){
            if(
                task.lifecycleState == TaskLifecycleState::Recording
                || task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
            )
                return false;
        }
        m_teardownInProgress = true;
        resetCompletion.activateWithinLock();
    }

    if(!destroyTaskPayloads())
        return false;
    resetCompletion.complete();
    return true;
}


void GpuTaskGraph::reset(){
    if(tryReset())
        return;

    NWB_FATAL_ASSERT_MSG(false, "GpuTaskGraph::reset requires every bound or in-flight task to resolve first");
    TerminateInvariant();
}

void GpuTaskGraph::completeResetWithoutCallbacks()noexcept{
    const bool taskPayloadsDestroyed = destroyTaskPayloadsWithoutCallbacks();
    NWB_FATAL_ASSERT_MSG(taskPayloadsDestroyed, "GPU task graph reset cleanup requires terminal task payloads");
    if(!taskPayloadsDestroyed)
        TerminateInvariant();

    destroyTaskStateSnapshots();
    destroyResourceStateSnapshots();
    {
        NothrowScopedLock lock(m_lifecycleMutex);
        if(
            m_activeCompiledGraph
            && m_activeRecordingPlanGeneration != 0u
            && m_submissionBindingState == SubmissionBindingState::None
        ){
            const bool recordingAttemptResolved = m_activeCompiledGraph->resolveRecordingAttempt(
                *this,
                m_activeRecordingPlanGeneration,
                m_activeRecordingAttemptGeneration
            );
            NWB_FATAL_ASSERT_MSG(
                recordingAttemptResolved,
                "GPU task graph reset cleanup must release its exact recording-plan lease"
            );
            if(!recordingAttemptResolved)
                TerminateInvariant();
        }
        m_tasks.clear();
        m_dependencies.clear();
        m_externalDependencies.clear();
        m_externalStateSources.clear();
        m_resourceUses.clear();
        m_resourceVersionUses.clear();
        if(m_resourceIdentityIndex)
            m_resourceIdentityIndex->clear();
        if(m_resourcePointerIndex)
            m_resourcePointerIndex->clear();
        m_resources.clear();
        m_resourceVersions.clear();
        m_initialOwnerHandoffSources.clear();
        m_queueFamilyIndices.clear();
        if(m_resourceSetIdentityIndex)
            m_resourceSetIdentityIndex->clear();
        m_resourceSets.clear();
        m_resourceSetMembers.clear();
        m_pipelines.clear();
        m_externalCompletions.clear();
        m_uploadBlobs.clear();
        m_markerText.clear();
        m_presentEndpoint = {};
        m_generation = allocateGeneration();
        m_declarationRevision = allocateGeneration();
        m_activeRecordingAttemptGeneration = allocateGeneration();
        m_activeRecordingPlanGeneration = 0u;
        m_activeRecordingPreparationSerial = 0u;
        NWB_FATAL_ASSERT_MSG(
            m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) == 0u,
            "GPU task graph reset publication requires every packet recording claim to drain"
        );
        if(m_activePacketRecordingClaimCount.load(MemoryOrder::relaxed) != 0u)
            TerminateInvariant();
        m_activeCompiledGraph = nullptr;
        m_activeSubmissionBinding = {};
        m_submissionBindingState = SubmissionBindingState::None;
        m_activeDiscardNotificationCount = 0u;
        m_hasPresentEndpoint = false;
        m_teardownInProgress = false;
    }
}

u64 GpuTaskGraph::recordingAttemptGeneration()const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    return m_activeRecordingAttemptGeneration;
}

bool GpuTaskGraph::beginRecordingAttempt(
    const GpuCompiledGraph& compiledGraph,
    const GpuSubmissionPacketId packet,
    const DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    RecordingAttemptScope& outAttempt
)const noexcept{
    if(!declarationAccess.validFor(*this) || !planAccess.validFor(compiledGraph) || !packet.valid())
        return false;

    NothrowScopedLock lock(m_lifecycleMutex);
    const bool continuingPreparation = outAttempt.validPreparationWithinLock(
        *this,
        compiledGraph,
        m_activeRecordingAttemptGeneration,
        m_activeRecordingPreparationSerial
    );
    if(
        m_teardownInProgress
        || m_activeDeclarationAccessCount != m_activeDeclarationReadCount
        || m_activeDiscardNotificationCount != 0u
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
        || (m_activeRecordingPreparationSerial != 0u && !continuingPreparation)
    )
        return false;

    if(!planAccess.validFor(declarationAccess))
        return false;
    const GpuCompiledPacketView packetView = planAccess.packet(packet);
    if(!packetView.valid() || packetView.plan->taskCount == 0u)
        return false;
    const GpuSubmissionPacket& packetPlan = *packetView.plan;
    const GpuTaskId* const tasks = packetView.tasks;
    for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
        if(!validTask(tasks[taskIndex]))
            return false;
    }

    const u64 requestedPlanGeneration = packet.generation;
    const bool samePlan = m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingPlanGeneration == requestedPlanGeneration
    ;
    if(samePlan && m_submissionBindingState != SubmissionBindingState::Resolved){
        if(!compiledGraph.beginRecordingAttempt(
            *this,
            declarationAccess,
            packet,
            requestedPlanGeneration,
            m_activeRecordingAttemptGeneration,
            0u,
            planAccess
        ))
            return false;

        bool selectedTaskWasDiscarded = false;
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            if(task.lifecycleAttemptGeneration != m_activeRecordingAttemptGeneration)
                return false;
            if(
                task.lifecycleState == TaskLifecycleState::Recording
                || task.lifecycleState == TaskLifecycleState::Recorded
                || task.lifecycleState == TaskLifecycleState::Submitting
                || task.lifecycleState == TaskLifecycleState::Accepting
                || task.lifecycleState == TaskLifecycleState::Accepted
            )
                return false;
            if(task.lifecycleState == TaskLifecycleState::Discarded)
                selectedTaskWasDiscarded = true;
        }
        if(!selectedTaskWasDiscarded)
            return true;
    }

    if(
        m_submissionBindingState == SubmissionBindingState::Active
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return false;
    if(m_activeRecordingPreparationSerial != 0u)
        return false;

    for(const GpuTaskNode& task : m_tasks){
        if(
            task.lifecycleAttemptGeneration != m_activeRecordingAttemptGeneration
            || task.lifecycleState == TaskLifecycleState::Recording
            || task.lifecycleState == TaskLifecycleState::Recorded
            || task.lifecycleState == TaskLifecycleState::Submitting
            || task.lifecycleState == TaskLifecycleState::Accepting
            || task.lifecycleState == TaskLifecycleState::Accepted
        )
            return false;
        // Once a plan began recording, every task must receive its discarded callback before a different plan or
        // retry can re-arm the graph.
        if(
            m_activeRecordingPlanGeneration != 0u
            && task.lifecycleState != TaskLifecycleState::Discarded
        )
            return false;
    }

    const bool previousPlanWasActive = m_activeRecordingPlanGeneration != 0u;
    const u64 nextRecordingAttemptGeneration = allocateGeneration();
    const u64 preparationSerial = allocateGeneration();
    if(nextRecordingAttemptGeneration == 0u || preparationSerial == 0u)
        return false;
    const u64 previousRecordingAttemptGeneration = samePlan
        && m_submissionBindingState == SubmissionBindingState::None
        ? m_activeRecordingAttemptGeneration
        : 0u
    ;
    if(!compiledGraph.beginRecordingAttempt(
        *this,
        declarationAccess,
        packet,
        requestedPlanGeneration,
        nextRecordingAttemptGeneration,
        previousRecordingAttemptGeneration,
        planAccess
    ))
        return false;

    if(
        m_activeCompiledGraph
        && m_activeCompiledGraph != &compiledGraph
        && m_submissionBindingState == SubmissionBindingState::None
    ){
        const bool previousAttemptResolved = m_activeCompiledGraph->resolveRecordingAttempt(
            *this,
            m_activeRecordingPlanGeneration,
            m_activeRecordingAttemptGeneration
        );
        if(!previousAttemptResolved){
            const bool candidateResolved = compiledGraph.resolveRecordingAttempt(
                *this,
                requestedPlanGeneration,
                nextRecordingAttemptGeneration
            );
            NWB_FATAL_ASSERT_MSG(candidateResolved, "Failed plan switch must release its candidate exact-plan lease");
            if(!candidateResolved)
                TerminateInvariant();
            return false;
        }
    }

    m_activeCompiledGraph = &compiledGraph;
    m_activeRecordingPlanGeneration = requestedPlanGeneration;
    m_activeRecordingAttemptGeneration = nextRecordingAttemptGeneration;
    m_activeRecordingPreparationSerial = preparationSerial;
    m_activeSubmissionBinding = {};
    m_submissionBindingState = SubmissionBindingState::None;
    for(const GpuTaskNode& task : m_tasks){
        task.lifecycleState = TaskLifecycleState::Declared;
        task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration;
        task.recordingClaimGeneration = 0u;
        task.submissionClaimGeneration = 0u;
        task.discardNotificationGeneration = 0u;
        task.recordThunkInProgress = false;
        task.recordThunkCompleted = false;
    }
    outAttempt.activateWithinLock(
        *this,
        compiledGraph,
        nextRecordingAttemptGeneration,
        preparationSerial,
        previousPlanWasActive
    );
    return true;
}


void GpuTaskGraph::cancelRecordingAttempt(RecordingAttemptScope& attempt)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    const bool attemptValid = attempt.m_graph == this
        && attempt.m_compiledGraph
        && m_activeCompiledGraph == attempt.m_compiledGraph
        && m_activeRecordingAttemptGeneration == attempt.m_recordingAttemptGeneration
        && m_activeRecordingPreparationSerial == attempt.m_preparationSerial
        && m_submissionBindingState == SubmissionBindingState::None
        && !m_activeSubmissionBinding.valid()
        && m_activeDiscardNotificationCount == 0u
    ;
    bool tasksUnclaimed = attemptValid;
    for(const GpuTaskNode& task : m_tasks){
        tasksUnclaimed = tasksUnclaimed
            && task.lifecycleAttemptGeneration == attempt.m_recordingAttemptGeneration
            && task.lifecycleState == TaskLifecycleState::Declared
            && task.recordingClaimGeneration == 0u
            && task.submissionClaimGeneration == 0u
            && !task.recordThunkInProgress
            && !task.recordThunkCompleted
        ;
    }
    NWB_FATAL_ASSERT_MSG(tasksUnclaimed, "provisional recording attempt cancellation requires unclaimed graph tasks");
    if(!tasksUnclaimed)
        TerminateInvariant();

    const bool candidateResolved = attempt.m_compiledGraph->resolveRecordingAttempt(
        *this,
        m_activeRecordingPlanGeneration,
        attempt.m_recordingAttemptGeneration
    );
    NWB_FATAL_ASSERT_MSG(candidateResolved, "provisional recording attempt cancellation must release its exact plan lease");
    if(!candidateResolved)
        TerminateInvariant();

    const bool restoreDiscardedTasks = attempt.m_previousPlanWasActive;
    m_activeCompiledGraph = nullptr;
    m_activeRecordingPlanGeneration = 0u;
    m_activeRecordingAttemptGeneration = allocateGeneration();
    m_activeRecordingPreparationSerial = 0u;
    m_activeSubmissionBinding = {};
    m_submissionBindingState = SubmissionBindingState::None;
    for(const GpuTaskNode& task : m_tasks){
        task.lifecycleState = restoreDiscardedTasks ? TaskLifecycleState::Discarded : TaskLifecycleState::Declared;
        task.lifecycleAttemptGeneration = m_activeRecordingAttemptGeneration;
        task.recordingClaimGeneration = 0u;
        task.submissionClaimGeneration = 0u;
        task.discardNotificationGeneration = 0u;
        task.recordThunkInProgress = false;
        task.recordThunkCompleted = false;
    }
    attempt.completeWithinLock();
}


void GpuTaskGraph::completeRecordingPreparation(RecordingAttemptScope& attempt)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    const bool preparationValid = attempt.m_compiledGraph
        && attempt.validPreparationWithinLock(
            *this,
            *attempt.m_compiledGraph,
            m_activeRecordingAttemptGeneration,
            m_activeRecordingPreparationSerial
        )
    ;
    NWB_FATAL_ASSERT_MSG(preparationValid, "Recording preparation completion requires its exact active capability");
    if(!preparationValid)
        TerminateInvariant();

    m_activeRecordingPreparationSerial = 0u;
    attempt.completeWithinLock();
}

bool GpuTaskGraph::matchesRecordingAttempt(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    return recordingAttemptGeneration != 0u
        && !m_teardownInProgress
        && m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingPlanGeneration != 0u
        && m_activeRecordingAttemptGeneration == recordingAttemptGeneration
        && m_activeRecordingPreparationSerial == 0u
        && compiledGraph.matchesRecordingAttempt(
            *this,
            m_activeRecordingPlanGeneration,
            recordingAttemptGeneration
        )
    ;
}


bool GpuTaskGraph::resolveRecordingAttemptIfTerminal(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeCompiledGraph != &compiledGraph
        || m_activeRecordingPlanGeneration == 0u
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        || m_activeRecordingPreparationSerial != 0u
    )
        return false;
    if(m_submissionBindingState != SubmissionBindingState::None)
        return true;
    if(m_activeDiscardNotificationCount != 0u)
        return true;
    for(const GpuTaskNode& task : m_tasks){
        if(
            task.lifecycleAttemptGeneration != recordingAttemptGeneration
            || task.lifecycleState != TaskLifecycleState::Discarded
        )
            return true;
    }
    if(!compiledGraph.resolveRecordingAttempt(
        *this,
        m_activeRecordingPlanGeneration,
        recordingAttemptGeneration
    ))
        return false;
    m_submissionBindingState = SubmissionBindingState::Resolved;
    return true;
}


bool GpuTaskGraph::bindSubmissionTransaction(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding,
    const RecordingAttemptScope* const preparationAttempt
)const noexcept{
    if(!submissionBinding.valid())
        return false;

    NothrowScopedLock lock(m_lifecycleMutex);
    if(
        m_teardownInProgress
        || m_activeCompiledGraph != &compiledGraph
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
    )
        return false;
    if(m_activeRecordingPreparationSerial != 0u){
        if(
            !preparationAttempt
            || !preparationAttempt->validPreparationWithinLock(
                *this,
                compiledGraph,
                recordingAttemptGeneration,
                m_activeRecordingPreparationSerial
            )
        )
            return false;
    }else if(preparationAttempt){
        return false;
    }
    if(
        m_submissionBindingState == SubmissionBindingState::Resolved
        || m_submissionBindingState == SubmissionBindingState::ExceptionClosing
    )
        return false;
    if(m_submissionBindingState == SubmissionBindingState::Active){
        return m_activeSubmissionBinding == submissionBinding
            && compiledGraph.matchesSubmissionTransaction(
                *this,
                m_activeRecordingPlanGeneration,
                recordingAttemptGeneration,
                submissionBinding
            )
        ;
    }
    if(m_submissionBindingState != SubmissionBindingState::None || m_activeSubmissionBinding.valid())
        return false;
    if(!compiledGraph.bindSubmissionTransaction(
        *this,
        m_activeRecordingPlanGeneration,
        recordingAttemptGeneration,
        submissionBinding
    ))
        return false;
    m_activeSubmissionBinding = submissionBinding;
    m_submissionBindingState = SubmissionBindingState::Active;
    return true;
}


bool GpuTaskGraph::matchesSubmissionTransaction(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    return submissionBinding.valid()
        && !m_teardownInProgress
        && m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingAttemptGeneration == recordingAttemptGeneration
        && m_activeRecordingPreparationSerial == 0u
        && m_submissionBindingState != SubmissionBindingState::None
        && m_activeSubmissionBinding == submissionBinding
        && compiledGraph.matchesSubmissionTransaction(
            *this,
            m_activeRecordingPlanGeneration,
            recordingAttemptGeneration,
            submissionBinding
        )
    ;
}


bool GpuTaskGraph::resolveSubmissionTransaction(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);
    if(
        !submissionBinding.valid()
        || m_teardownInProgress
        || m_activeCompiledGraph != &compiledGraph
        || m_activeRecordingAttemptGeneration != recordingAttemptGeneration
        || m_activeRecordingPreparationSerial != 0u
        || m_activeSubmissionBinding != submissionBinding
    )
        return false;
    if(m_submissionBindingState == SubmissionBindingState::Resolved)
        return true;
    if(
        m_submissionBindingState != SubmissionBindingState::Active
        && m_submissionBindingState != SubmissionBindingState::ExceptionClosing
    )
        return false;
    if(!compiledGraph.resolveSubmissionTransaction(
        *this,
        m_activeRecordingPlanGeneration,
        recordingAttemptGeneration,
        submissionBinding
    ))
        return false;
    m_submissionBindingState = SubmissionBindingState::Resolved;
    return true;
}

bool GpuTaskGraph::beginSubmissionExceptionClosing(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(recordingAttemptGeneration == 0u || !submissionBinding.valid())
        return false;

    NothrowScopedLock lock(m_lifecycleMutex);
    const bool exactBinding = !m_teardownInProgress
        && m_activeCompiledGraph == &compiledGraph
        && m_activeRecordingPlanGeneration != 0u
        && m_activeRecordingAttemptGeneration == recordingAttemptGeneration
        && m_activeRecordingPreparationSerial == 0u
        && m_activeSubmissionBinding == submissionBinding
    ;
    if(!exactBinding)
        return false;
    if(m_submissionBindingState == SubmissionBindingState::ExceptionClosing)
        return true;
    if(m_submissionBindingState != SubmissionBindingState::Active)
        return false;
    if(!compiledGraph.matchesSubmissionTransaction(
        *this,
        m_activeRecordingPlanGeneration,
        recordingAttemptGeneration,
        submissionBinding
    ))
        return false;
    m_submissionBindingState = SubmissionBindingState::ExceptionClosing;
    return true;
}

bool GpuTaskGraph::waitForSubmissionExceptionRecordingClaims(
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration,
    const GpuGraphSubmissionBinding& submissionBinding
)const noexcept{
    if(recordingAttemptGeneration == 0u || !submissionBinding.valid())
        return false;

    while(true){
        u32 activeClaimCount = 0u;
        {
            NothrowScopedLock lock(m_lifecycleMutex);
            const bool exactBinding = !m_teardownInProgress
                && m_activeCompiledGraph == &compiledGraph
                && m_activeRecordingAttemptGeneration == recordingAttemptGeneration
                && m_activeSubmissionBinding == submissionBinding
            ;
            if(!exactBinding)
                return false;
            if(m_submissionBindingState == SubmissionBindingState::Resolved)
                return true;
            if(m_submissionBindingState != SubmissionBindingState::ExceptionClosing)
                return false;
            activeClaimCount = m_activePacketRecordingClaimCount.load(MemoryOrder::acquire);
            if(activeClaimCount == 0u)
                return true;
        }
        m_activePacketRecordingClaimCount.wait(activeClaimCount, MemoryOrder::acquire);
    }
}

bool GpuTaskGraph::validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
    if(deviceGeneration == 0u)
        return false;

    const auto validStateSource = [deviceGeneration](const CommandListResourceStateHandoff* const states){
        // Invalid declarations intentionally remain a record-time failure so legacy callers retain their existing
        // diagnostic. A valid snapshot, however, must never cross a native Device lifetime.
        return !states || !states->valid() || states->validForDeviceGeneration(deviceGeneration);
    };

    for(const GpuTaskExternalStateSource& source : m_externalStateSources){
        if(!validStateSource(source.states))
            return false;
    }
    for(const GpuGraphResourceNode& resource : m_resources){
        if(
            (resource.texture != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.buffer != nullptr && resource.deviceGeneration != deviceGeneration)
            || (resource.accelStruct != nullptr && resource.deviceGeneration != deviceGeneration)
            || !validStateSource(resource.initialOwnerStateSource)
        )
            return false;
    }
    for(const GpuTaskGraphInitialOwnerHandoffSourceView& source : m_initialOwnerHandoffSources){
        if(!validStateSource(source.stateSource))
            return false;
    }
    for(const GpuGraphPipelineNode& pipeline : m_pipelines){
        if(
            (pipeline.graphicsPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.computePipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.meshletPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
            || (pipeline.rayTracingPipeline != nullptr && pipeline.deviceGeneration != deviceGeneration)
        )
            return false;
    }
    for(const GpuExternalCompletionNode& completion : m_externalCompletions){
        if(
            completion.hasToken
            && (
                !completion.token.valid()
                || !completion.token.hasPhysicalQueueIdentity()
                || completion.token.deviceGeneration != deviceGeneration
            )
        )
            return false;
    }
    return true;
}

u64 GpuTaskGraphDeclarationReadView::generation()const noexcept{
    return m_graph ? m_graph->m_generation : 0u;
}

u64 GpuTaskGraphDeclarationReadView::declarationRevision()const noexcept{
    return m_graph ? m_graph->m_declarationRevision : 0u;
}

bool GpuTaskGraphDeclarationReadView::validForDeviceGeneration(const u16 deviceGeneration)const noexcept{
    return m_graph && m_graph->validForDeviceGeneration(deviceGeneration);
}

bool GpuTaskGraphDeclarationReadView::validTask(const GpuTaskId& id)const noexcept{
    return m_graph && m_graph->validTask(id);
}

bool GpuTaskGraphDeclarationReadView::validResource(const GpuGraphResourceId& id)const noexcept{
    return m_graph && m_graph->validResource(id);
}

bool GpuTaskGraphDeclarationReadView::validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept{
    return m_graph && m_graph->validResourceVersion(id);
}

bool GpuTaskGraphDeclarationReadView::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return m_graph && m_graph->validResourceSet(id);
}

bool GpuTaskGraphDeclarationReadView::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return m_graph && m_graph->validUploadBlob(id);
}

bool GpuTaskGraphDeclarationReadView::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return m_graph && m_graph->validPipeline(id);
}

bool GpuTaskGraphDeclarationReadView::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return m_graph && m_graph->validExternalCompletion(id);
}

usize GpuTaskGraphDeclarationReadView::taskCount()const noexcept{
    return m_graph ? m_graph->m_tasks.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceCount()const noexcept{
    return m_graph ? m_graph->m_resources.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceVersionCount()const noexcept{
    return m_graph ? m_graph->m_resourceVersions.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::resourceSetCount()const noexcept{
    return m_graph ? m_graph->m_resourceSets.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::uploadBlobCount()const noexcept{
    return m_graph ? m_graph->m_uploadBlobs.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::pipelineCount()const noexcept{
    return m_graph ? m_graph->m_pipelines.size() : 0u;
}

usize GpuTaskGraphDeclarationReadView::externalCompletionCount()const noexcept{
    return m_graph ? m_graph->m_externalCompletions.size() : 0u;
}

GpuTaskGraphTaskView GpuTaskGraphDeclarationReadView::taskAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_tasks.size() ? m_graph->taskAt(index) : GpuTaskGraphTaskView{};
}

GpuTaskGraphResourceView GpuTaskGraphDeclarationReadView::resourceAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_resources.size() ? m_graph->resourceAt(index) : GpuTaskGraphResourceView{};
}

GpuTaskGraphResourceVersionView GpuTaskGraphDeclarationReadView::resourceVersionAt(const usize index)const noexcept{
    return m_graph && index < m_graph->m_resourceVersions.size()
        ? m_graph->resourceVersionAt(index)
        : GpuTaskGraphResourceVersionView{}
    ;
}

GpuTaskGraphResourceSetView GpuTaskGraphDeclarationReadView::resourceSetAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_resourceSets.size()
        ? m_graph->resourceSetAt(index)
        : GpuTaskGraphResourceSetView{}
    ;
}

GpuTaskGraphPipelineView GpuTaskGraphDeclarationReadView::pipelineAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_pipelines.size() ? m_graph->pipelineAt(index) : GpuTaskGraphPipelineView{};
}

GpuTaskGraphExternalCompletionView GpuTaskGraphDeclarationReadView::externalCompletionAt(const usize index)const & noexcept{
    return m_graph && index < m_graph->m_externalCompletions.size()
        ? m_graph->externalCompletionAt(index)
        : GpuTaskGraphExternalCompletionView{}
    ;
}

const QueueSubmissionToken* GpuTaskGraphDeclarationReadView::externalCompletionToken(
    const GpuExternalCompletionId& completion
)const & noexcept{
    return m_graph ? m_graph->externalCompletionToken(completion) : nullptr;
}

Texture* GpuTaskGraphDeclarationReadView::textureForResource(const GpuGraphResourceId& resource)const & noexcept{
    return m_graph ? m_graph->textureForResource(resource) : nullptr;
}

Buffer* GpuTaskGraphDeclarationReadView::bufferForResource(const GpuGraphResourceId& resource)const & noexcept{
    return m_graph ? m_graph->bufferForResource(resource) : nullptr;
}

RayTracingAccelStruct* GpuTaskGraphDeclarationReadView::accelStructForResource(
    const GpuGraphResourceId& resource
)const & noexcept{
    return m_graph ? m_graph->accelStructForResource(resource) : nullptr;
}

const void* GpuTaskGraphDeclarationReadView::uploadBlobData(
    const GpuUploadBlobId& blob,
    usize& outByteSize
)const & noexcept{
    if(m_graph)
        return m_graph->uploadBlobData(blob, outByteSize);
    outByteSize = 0u;
    return nullptr;
}

GraphicsPipeline* GpuTaskGraphDeclarationReadView::graphicsPipelineFor(
    const GpuGraphPipelineId& pipeline
)const & noexcept{
    return m_graph ? m_graph->graphicsPipelineFor(pipeline) : nullptr;
}

ComputePipeline* GpuTaskGraphDeclarationReadView::computePipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept{
    return m_graph ? m_graph->computePipelineFor(pipeline) : nullptr;
}

MeshletPipeline* GpuTaskGraphDeclarationReadView::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept{
    return m_graph ? m_graph->meshletPipelineFor(pipeline) : nullptr;
}

RayTracingPipeline* GpuTaskGraphDeclarationReadView::rayTracingPipelineFor(
    const GpuGraphPipelineId& pipeline
)const & noexcept{
    return m_graph ? m_graph->rayTracingPipelineFor(pipeline) : nullptr;
}

bool GpuTaskGraph::validTask(const GpuTaskId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_tasks.size();
}

bool GpuTaskGraph::validResource(const GpuGraphResourceId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resources.size();
}

bool GpuTaskGraph::validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceVersions.size();
}

bool GpuTaskGraph::validResourceSet(const GpuGraphResourceSetId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_resourceSets.size();
}

bool GpuTaskGraph::validUploadBlob(const GpuUploadBlobId& id)const noexcept{
    return id.valid()
        && id.generation == m_generation
        && id.index < m_uploadBlobs.size()
        && !m_uploadBlobs[id.index].bytes.empty()
    ;
}

bool GpuTaskGraph::validPipeline(const GpuGraphPipelineId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_pipelines.size();
}

bool GpuTaskGraph::validExternalCompletion(const GpuExternalCompletionId& id)const noexcept{
    return id.valid() && id.generation == m_generation && id.index < m_externalCompletions.size();
}

GpuTaskGraphTaskView GpuTaskGraph::taskAt(const usize index)const{
    NWB_ASSERT(index < m_tasks.size());
    const GpuTaskNode& task = m_tasks[index];
    return GpuTaskGraphTaskView{
        .id = GpuTaskId{ static_cast<u32>(index), m_generation },
        .identity = task.identity,
        .markerLabel = markerLabel(task.markerLabelOffset, task.markerLabelSize),
        .queue = task.queue,
        .scheduling = task.scheduling,
        .timing = task.timing,
        .dependencies = task.dependencyCount > 0u ? m_dependencies.data() + task.dependencyOffset : nullptr,
        .dependencyCount = task.dependencyCount,
        .externalDependencies = task.externalDependencyCount > 0u
            ? m_externalDependencies.data() + task.externalDependencyOffset
            : nullptr,
        .externalDependencyCount = task.externalDependencyCount,
        .externalStateSources = task.externalStateSourceCount > 0u
            ? m_externalStateSources.data() + task.externalStateSourceOffset
            : nullptr,
        .externalStateSourceCount = task.externalStateSourceCount,
        .resourceUses = task.resourceUseCount > 0u ? m_resourceUses.data() + task.resourceUseOffset : nullptr,
        .resourceUseCount = task.resourceUseCount,
        .resourceVersionUses = task.resourceVersionUseCount > 0u
            ? m_resourceVersionUses.data() + task.resourceVersionUseOffset
            : nullptr,
        .resourceVersionUseCount = task.resourceVersionUseCount,
        .payloadObjectSize = task.payloadObjectSize,
        .directResourceUseCount = task.directResourceUseCount,
        .declaredResourceSetUseCount = task.declaredResourceSetUseCount,
        .expandedResourceSetMemberUseCount = task.expandedResourceSetMemberUseCount,
        .hasPayload = task.payload != nullptr,
        .hasRecordPayload = task.recordPayload != nullptr,
        .hasAcceptedPayload = task.acceptPayload != nullptr,
    };
}

GpuTaskGraphResourceView GpuTaskGraph::resourceAt(const usize index)const{
    NWB_ASSERT(index < m_resources.size());
    const GpuGraphResourceNode& resource = m_resources[index];
    return GpuTaskGraphResourceView{
        .id = GpuGraphResourceId{ static_cast<u32>(index), m_generation },
        .identity = resource.identity,
        .markerLabel = markerLabel(resource.markerLabelOffset, resource.markerLabelSize),
        .type = resource.type,
        .initialState = resource.initialState,
        .externalFinalState = resource.externalFinalState,
        .externalFinalReleaseDestinationQueue = resource.externalFinalReleaseDestinationQueue,
        .initialOwnerQueue = resource.initialOwnerQueue,
        .initialOwnerReleaseDestinationQueue = resource.initialOwnerReleaseDestinationQueue,
        .initialOwnerCompletion = resource.initialOwnerCompletion,
        .initialOwnerMinimumCompletionToken = resource.initialOwnerMinimumCompletionToken,
        .initialOwnerStateSource = resource.initialOwnerStateSource,
        .initialOwnerHandoffSources = resource.initialOwnerHandoffSourceCount != 0u
            ? m_initialOwnerHandoffSources.data() + resource.initialOwnerHandoffSourceOffset
            : nullptr,
        .initialOwnerHandoffSourceCount = resource.initialOwnerHandoffSourceCount,
        .queueSharing = resource.queueSharing,
        .initialAvailabilityCompletion = resource.initialAvailabilityCompletion,
        .queueAdmission = ResourceQueueAdmissionSnapshot{
            .admittedQueueClasses = resource.queueSharing,
            .queueFamilyIndices = resource.queueFamilyIndexCount != 0u
                ? m_queueFamilyIndices.data() + resource.queueFamilyIndexOffset
                : nullptr,
            .queueFamilyIndexCount = resource.queueFamilyIndexCount,
            .usesConcurrentSharing = resource.usesConcurrentSharing,
        },
        .hasQueueAdmission = resource.hasQueueAdmission,
        .hasBackendResource = resource.texture != nullptr || resource.buffer != nullptr || resource.accelStruct != nullptr,
    };
}

GpuTaskGraphResourceVersionView GpuTaskGraph::resourceVersionAt(const usize index)const{
    NWB_ASSERT(index < m_resourceVersions.size());
    const GpuGraphResourceVersionNode& version = m_resourceVersions[index];
    return GpuTaskGraphResourceVersionView{
        .id = GpuGraphResourceVersionId{ static_cast<u32>(index), m_generation },
        .resource = version.resource,
        .range = version.range,
        .origin = version.origin,
    };
}

GpuTaskGraphResourceSetView GpuTaskGraph::resourceSetAt(const usize index)const{
    NWB_ASSERT(index < m_resourceSets.size());
    const GpuGraphResourceSetNode& resourceSet = m_resourceSets[index];
    return GpuTaskGraphResourceSetView{
        .id = GpuGraphResourceSetId{ static_cast<u32>(index), m_generation },
        .identity = resourceSet.identity,
        .markerLabel = markerLabel(resourceSet.markerLabelOffset, resourceSet.markerLabelSize),
        .members = resourceSet.memberCount > 0u ? m_resourceSetMembers.data() + resourceSet.memberOffset : nullptr,
        .memberCount = resourceSet.memberCount,
    };
}

GpuTaskGraphPipelineView GpuTaskGraph::pipelineAt(const usize index)const{
    NWB_ASSERT(index < m_pipelines.size());
    const GpuGraphPipelineNode& pipeline = m_pipelines[index];
    return GpuTaskGraphPipelineView{
        .id = GpuGraphPipelineId{ static_cast<u32>(index), m_generation },
        .identity = pipeline.identity,
        .markerLabel = markerLabel(pipeline.markerLabelOffset, pipeline.markerLabelSize),
        .type = pipeline.type,
        .hasBackendPipeline = pipeline.graphicsPipeline != nullptr
            || pipeline.computePipeline != nullptr
            || pipeline.meshletPipeline != nullptr
            || pipeline.rayTracingPipeline != nullptr,
    };
}

GpuTaskGraphExternalCompletionView GpuTaskGraph::externalCompletionAt(const usize index)const{
    NWB_ASSERT(index < m_externalCompletions.size());
    const GpuExternalCompletionNode& completion = m_externalCompletions[index];
    return GpuTaskGraphExternalCompletionView{
        .id = GpuExternalCompletionId{ static_cast<u32>(index), m_generation },
        .identity = completion.identity,
        .markerLabel = markerLabel(completion.markerLabelOffset, completion.markerLabelSize),
        .token = completion.token,
        .hasToken = completion.hasToken,
    };
}

const QueueSubmissionToken* GpuTaskGraph::externalCompletionToken(
    const GpuExternalCompletionId& completion
)const noexcept{
    if(!validExternalCompletion(completion) || !m_externalCompletions[completion.index].hasToken)
        return nullptr;
    return &m_externalCompletions[completion.index].token;
}

Texture* GpuTaskGraph::textureForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Texture ? node.texture.get() : nullptr;
}

Buffer* GpuTaskGraph::bufferForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::Buffer ? node.buffer.get() : nullptr;
}

RayTracingAccelStruct* GpuTaskGraph::accelStructForResource(const GpuGraphResourceId& resource)const noexcept{
    if(!validResource(resource))
        return nullptr;
    const GpuGraphResourceNode& node = m_resources[resource.index];
    return node.type == GpuGraphResourceType::AccelStruct ? node.accelStruct.get() : nullptr;
}

const void* GpuTaskGraph::uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept{
    outByteSize = 0u;
    const GpuUploadBlobNode* const node = findUploadBlob(blob);
    if(!node || node->bytes.empty())
        return nullptr;
    outByteSize = node->bytes.size();
    return node->bytes.data();
}

GraphicsPipeline* GpuTaskGraph::graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Graphics ? node.graphicsPipeline.get() : nullptr;
}

ComputePipeline* GpuTaskGraph::computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Compute ? node.computePipeline.get() : nullptr;
}

MeshletPipeline* GpuTaskGraph::meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::Meshlet ? node.meshletPipeline.get() : nullptr;
}

RayTracingPipeline* GpuTaskGraph::rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept{
    if(!validPipeline(pipeline))
        return nullptr;
    const GpuGraphPipelineNode& node = m_pipelines[pipeline.index];
    return node.type == GpuGraphPipelineType::RayTracing ? node.rayTracingPipeline.get() : nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

