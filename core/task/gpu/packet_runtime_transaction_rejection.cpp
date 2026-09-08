// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"

#include <global/exception.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuGraphSubmissionTransaction::RejectingPacketUnwindScope final : NoCopy{
public:
    RejectingPacketUnwindScope(
        GpuGraphSubmissionTransaction& transaction,
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId packet,
        const u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )noexcept
        : m_transaction(transaction)
        , m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_submissionBinding(submissionBinding)
        , m_packet(packet)
        , m_recordingAttemptGeneration(recordingAttemptGeneration)
        , m_uncaughtExceptionCount(UncaughtExceptionCount())
    {}
    ~RejectingPacketUnwindScope()noexcept{
        if(UncaughtExceptionCount() <= m_uncaughtExceptionCount)
            return;
        const bool abandoned = m_graph.abandonUnacceptedPacketWithoutCallbacks(
            m_compiledGraph,
            m_planAccess,
            m_packet,
            m_recordingAttemptGeneration,
            m_submissionBinding
        );
        NWB_FATAL_ASSERT_MSG(abandoned, "throwing discard observer must leave an abandonable graph packet");
        if(!abandoned)
            TerminateInvariant();
        m_transaction.completeRejectedPacketWithinSubmissionOperation(
            m_graph,
            m_compiledGraph,
            m_planAccess,
            m_packet,
            false
        );
    }


private:
    GpuGraphSubmissionTransaction& m_transaction;
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    const GpuGraphSubmissionBinding m_submissionBinding;
    const GpuSubmissionPacketId m_packet;
    const u64 m_recordingAttemptGeneration = 0u;
    const i32 m_uncaughtExceptionCount = 0;
};


class GpuGraphSubmissionTransaction::RejectingSubmissionUnwindScope final : NoCopy{
public:
    RejectingSubmissionUnwindScope(
        GpuGraphSubmissionTransaction& transaction,
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId packet,
        const GpuTaskGraph::PacketSubmissionLease& lease
    )noexcept
        : m_transaction(transaction)
        , m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_planAccess(planAccess)
        , m_lease(lease)
        , m_packet(packet)
        , m_uncaughtExceptionCount(UncaughtExceptionCount())
    {}
    ~RejectingSubmissionUnwindScope()noexcept{
        if(UncaughtExceptionCount() <= m_uncaughtExceptionCount)
            return;
        NWB_FATAL_ASSERT_MSG(!m_lease.valid(), "throwing submission discard observer must consume its graph lease");
        if(m_lease.valid())
            TerminateInvariant();
        m_transaction.completeRejectedPacketWithinSubmissionOperation(
            m_graph,
            m_compiledGraph,
            m_planAccess,
            m_packet,
            true
        );
    }


private:
    GpuGraphSubmissionTransaction& m_transaction;
    const GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    const GpuCompiledGraph::ReadView& m_planAccess;
    const GpuTaskGraph::PacketSubmissionLease& m_lease;
    const GpuSubmissionPacketId m_packet;
    const i32 m_uncaughtExceptionCount = 0;
};


class GpuGraphSubmissionTransaction::UnacceptedPacketsFinalizationScope final : NoCopy{
public:
    UnacceptedPacketsFinalizationScope(
        GpuGraphSubmissionTransaction& transaction,
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph
    )noexcept
        : m_transaction(transaction)
        , m_graph(graph)
        , m_compiledGraph(compiledGraph)
        , m_uncaughtExceptionCount(UncaughtExceptionCount())
    {}
    ~UnacceptedPacketsFinalizationScope()noexcept{
        if(!m_armed || UncaughtExceptionCount() <= m_uncaughtExceptionCount)
            return;

        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        if(!m_graph.waitForSubmissionExceptionRecordingClaims(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        )){
            if(m_transaction.submissionExceptionClosingResolved(
                m_compiledGraph,
                m_recordingAttemptGeneration,
                m_submissionBinding
            ))
                return;
            TerminateInvariant();
        }
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;

        SubmissionOperation submissionOperation(
            m_transaction,
            SubmissionOperationMode::ExceptionFinalizer
        );
        if(!submissionOperation.valid()){
            if(m_transaction.submissionExceptionClosingResolved(
                m_compiledGraph,
                m_recordingAttemptGeneration,
                m_submissionBinding
            ))
                return;
            TerminateInvariant();
        }
        if(m_transaction.submissionExceptionClosingResolved(
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        ))
            return;
        GpuCompiledGraph::ReadView planAccess(m_compiledGraph);
        if(!planAccess.valid()){
            if(m_transaction.submissionExceptionClosingResolved(
                m_compiledGraph,
                m_recordingAttemptGeneration,
                m_submissionBinding
            ))
                return;
            TerminateInvariant();
        }
        m_transaction.completeSubmissionExceptionClosingWithinSubmissionOperation(
            m_graph,
            m_compiledGraph,
            planAccess,
            m_recordingAttemptGeneration,
            m_submissionBinding
        );
    }


public:
    void beginClosingWithinSubmissionOperation()noexcept{
        if(m_armed)
            return;
        if(!m_transaction.beginSubmissionExceptionClosingWithinSubmissionOperation(
            m_graph,
            m_compiledGraph,
            m_recordingAttemptGeneration,
            m_submissionBinding
        )){
            if(m_transaction.hasUnresolvedSubmissionBinding(m_compiledGraph))
                TerminateInvariant();
            return;
        }
        if(m_recordingAttemptGeneration == 0u || !m_submissionBinding.valid())
            TerminateInvariant();
        m_armed = true;
    }


private:
    GpuGraphSubmissionTransaction& m_transaction;
    GpuTaskGraph& m_graph;
    const GpuCompiledGraph& m_compiledGraph;
    u64 m_recordingAttemptGeneration = 0u;
    GpuGraphSubmissionBinding m_submissionBinding;
    const i32 m_uncaughtExceptionCount = 0;
    bool m_armed = false;
};


class GpuGraphSubmissionTransaction::UnacceptedPacketsUnwindScope final : NoCopy{
public:
    explicit UnacceptedPacketsUnwindScope(UnacceptedPacketsFinalizationScope& finalization)noexcept
        : m_finalization(finalization)
        , m_uncaughtExceptionCount(UncaughtExceptionCount())
    {}
    ~UnacceptedPacketsUnwindScope()noexcept{
        if(UncaughtExceptionCount() <= m_uncaughtExceptionCount)
            return;
        m_finalization.beginClosingWithinSubmissionOperation();
    }


private:
    UnacceptedPacketsFinalizationScope& m_finalization;
    const i32 m_uncaughtExceptionCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void GpuGraphSubmissionTransaction::rejectTask(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskId task,
    const u64 recordingAttemptGeneration
){
    UnacceptedPacketsFinalizationScope finalizationScope(*this, graph, compiledGraph);
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::WaitExclusiveBarrier);
    if(!submissionOperation.valid())
        return;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(!planAccess.validFor(declarationAccess) || !validFor(planAccess))
        return;
    UnacceptedPacketsUnwindScope unwindScope(finalizationScope);

    rejectTaskWithinSubmissionOperation(
        graph,
        declarationAccess,
        compiledGraph,
        planAccess,
        task,
        recordingAttemptGeneration
    );
}

bool GpuGraphSubmissionTransaction::discardUnaccepted(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const u64 recordingAttemptGeneration
){
    UnacceptedPacketsFinalizationScope finalizationScope(*this, graph, compiledGraph);
    SubmissionOperation submissionOperation(*this, SubmissionOperationMode::TryExclusiveBarrier);
    if(!submissionOperation.valid())
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(
        !planAccess.valid()
        || !validFor(planAccess)
        || recordingAttemptGeneration == 0u
    )
        return false;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(!planAccess.validFor(declarationAccess))
        return false;

    GpuGraphSubmissionBinding terminalBinding;
    {
        ScopedLock lock(m_mutex);
        if(
            validForLocked(planAccess)
            && m_recordingAttemptGeneration == recordingAttemptGeneration
            && m_submissionBindingResolved
            && allPacketsTerminalLocked()
        )
            terminalBinding = m_activeSubmissionBinding;
    }
    if(terminalBinding.valid()){
        return graph.matchesSubmissionTransaction(
            compiledGraph,
            recordingAttemptGeneration,
            terminalBinding
        );
    }
    if(!bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordingAttemptGeneration))
        return false;
    UnacceptedPacketsUnwindScope unwindScope(finalizationScope);

    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        {
            ScopedLock lock(m_mutex);
            if(
                !validForLocked(planAccess)
                || m_recordingAttemptGeneration != recordingAttemptGeneration
                || packetIndex >= m_packets.size()
            )
                return false;
            if(
                m_packets[packetIndex].state == PacketRuntimeState::Accepted
                || m_packets[packetIndex].state == PacketRuntimeState::Rejected
            )
                continue;
            if(
                m_packets[packetIndex].state == PacketRuntimeState::Submitting
                || m_packets[packetIndex].state == PacketRuntimeState::Rejecting
            )
                return false;
        }

        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        if(!packet.valid())
            return false;
        rejectPacket(graph, compiledGraph, planAccess, packet, recordingAttemptGeneration);

        ScopedLock lock(m_mutex);
        if(
            !validForLocked(planAccess)
            || packetIndex >= m_packets.size()
            || m_packets[packetIndex].state != PacketRuntimeState::Rejected
        )
            return false;
    }
    return true;
}


void GpuGraphSubmissionTransaction::rejectTaskWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuTaskId task,
    const u64 recordingAttemptGeneration
){
    if(
        !SubmissionOperation::activeExclusiveFor(*this)
        || !declarationAccess.validFor(graph)
        || !planAccess.validFor(declarationAccess)
        || !planAccess.findTask(task).valid()
        || !validFor(planAccess)
    )
        return;
    rejectPacket(graph, compiledGraph, planAccess, planAccess.packetForTask(task), recordingAttemptGeneration);
}


void GpuGraphSubmissionTransaction::rejectPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packetID,
    const u64 recordingAttemptGeneration
){
    if(
        !SubmissionOperation::activeFor(*this)
        || !planAccess.validFor(compiledGraph)
        || !validFor(planAccess)
        || !planAccess.validPacket(packetID)
        || recordingAttemptGeneration == 0u
        || !bindRecordingAttemptWithinSubmissionOperation(graph, compiledGraph, recordingAttemptGeneration)
    )
        return;
    GpuGraphSubmissionBinding submissionBinding;
    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(planAccess)
            || m_recordingAttemptGeneration != recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return;
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != PacketRuntimeState::Declared)
            return;
        runtime.state = PacketRuntimeState::Rejecting;
        submissionBinding = m_activeSubmissionBinding;
    }
    RejectingPacketUnwindScope unwindScope(
        *this,
        graph,
        compiledGraph,
        planAccess,
        packetID,
        recordingAttemptGeneration,
        submissionBinding
    );

    const bool packetDiscarded = graph.discardUnacceptedPacket(
        compiledGraph,
        planAccess,
        packetID,
        recordingAttemptGeneration,
        submissionBinding
    );
    if(!packetDiscarded){
        ScopedLock lock(m_mutex);
        if(validForLocked(planAccess) && packetID.index < m_packets.size()){
            PacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == PacketRuntimeState::Rejecting)
                runtime.state = PacketRuntimeState::Declared;
        }
        return;
    }
    completeRejectedPacketWithinSubmissionOperation(graph, compiledGraph, planAccess, packetID, false);
}

void GpuGraphSubmissionTransaction::rejectSubmittingPacket(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packetID,
    GpuTaskGraph::PacketSubmissionLease& lease
){
    if(
        !SubmissionOperation::activeFor(*this)
        || !planAccess.validFor(compiledGraph)
        || !validFor(planAccess)
        || !planAccess.validPacket(packetID)
        || !lease.valid()
        || lease.m_packet != packetID
        || lease.m_planGeneration != planAccess.planGeneration()
        || !matchesRecordingAttemptBinding(
            graph,
            compiledGraph,
            lease.m_recordingAttemptGeneration,
            lease.m_submissionBinding
        )
    )
        return;

    {
        ScopedLock lock(m_mutex);
        if(
            !validForLocked(planAccess)
            || m_recordingAttemptGeneration != lease.m_recordingAttemptGeneration
            || packetID.index >= m_packets.size()
        )
            return;
        PacketRuntime& runtime = m_packets[packetID.index];
        if(runtime.state != PacketRuntimeState::Submitting)
            return;
        runtime.state = PacketRuntimeState::Rejecting;
    }
    RejectingSubmissionUnwindScope unwindScope(*this, graph, compiledGraph, planAccess, packetID, lease);

    graph.abortPacketSubmission(compiledGraph, planAccess, packetID, lease);
    if(lease.valid()){
        ScopedLock lock(m_mutex);
        if(validForLocked(planAccess) && packetID.index < m_packets.size()){
            PacketRuntime& runtime = m_packets[packetID.index];
            if(runtime.state == PacketRuntimeState::Rejecting)
                runtime.state = PacketRuntimeState::Submitting;
        }
        return;
    }
    completeRejectedPacketWithinSubmissionOperation(graph, compiledGraph, planAccess, packetID, true);
}

void GpuGraphSubmissionTransaction::abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packetID,
    GpuTaskGraph::PacketSubmissionLease& lease
)noexcept{
    const bool inputValid = SubmissionOperation::activeFor(*this)
        && planAccess.validFor(compiledGraph)
        && validFor(planAccess)
        && planAccess.validPacket(packetID)
        && lease.valid()
        && lease.m_packet == packetID
        && lease.m_planGeneration == planAccess.planGeneration()
        && matchesRecordingAttemptBinding(
            graph,
            compiledGraph,
            lease.m_recordingAttemptGeneration,
            lease.m_submissionBinding
        )
    ;
    NWB_FATAL_ASSERT_MSG(inputValid, "pre-submit exception cleanup must retain its exact transaction submission lease");
    if(!inputValid)
        TerminateInvariant();

    {
        NothrowScopedLock lock(m_mutex);
        const bool runtimeValid = validForLocked(planAccess)
            && m_recordingAttemptGeneration == lease.m_recordingAttemptGeneration
            && packetID.index < m_packets.size()
            && m_packets[packetID.index].state == PacketRuntimeState::Submitting
        ;
        NWB_FATAL_ASSERT_MSG(runtimeValid, "pre-submit exception cleanup must retain its submitting packet runtime");
        if(!runtimeValid)
            TerminateInvariant();
        m_packets[packetID.index].state = PacketRuntimeState::Rejecting;
    }

    const bool abandoned = graph.abandonPacketSubmissionWithoutCallbacks(compiledGraph, planAccess, packetID, lease);
    NWB_FATAL_ASSERT_MSG(
        abandoned && !lease.valid(),
        "pre-submit exception cleanup must consume its exact graph submission lease without observers"
    );
    if(!abandoned || lease.valid())
        TerminateInvariant();
    completeRejectedPacketWithinSubmissionOperation(graph, compiledGraph, planAccess, packetID, true);
}


void GpuGraphSubmissionTransaction::completeRejectedPacketWithinSubmissionOperation(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId packetID,
    const bool nativeSubmissionRejected
)noexcept{
    const GpuCompiledPacketView packetView = planAccess.packet(packetID);
    const bool inputValid = SubmissionOperation::activeFor(*this)
        && planAccess.validFor(compiledGraph)
        && packetView.valid()
    ;
    NWB_FATAL_ASSERT_MSG(inputValid, "rejected packet completion requires its exact transaction operation and packet");
    if(!inputValid)
        TerminateInvariant();

    const GpuSubmissionPacket& packet = *packetView.plan;
    NothrowScopedLock lock(m_mutex);
    const bool runtimeValid = validForLocked(planAccess)
        && packetID.index < m_packets.size()
        && m_packets[packetID.index].state == PacketRuntimeState::Rejecting
    ;
    NWB_FATAL_ASSERT_MSG(runtimeValid, "rejected packet completion must retain its rejecting transaction state");
    if(!runtimeValid)
        TerminateInvariant();
    PacketRuntime& runtime = m_packets[packetID.index];
    runtime.state = PacketRuntimeState::Rejected;
    runtime.nativeSubmissionRejected = nativeSubmissionRejected;
    ++m_submissionStatistics.rejectedPacketCount;
    m_submissionStatistics.rejectedTaskCount += packet.taskCount;
    if(nativeSubmissionRejected)
        ++m_submissionStatistics.rejectedSubmissionCount;
    resolveSubmissionBindingIfTerminalLocked(graph, compiledGraph);
}

void GpuGraphSubmissionTransaction::abandonUnacceptedPacketsAfterExceptionWithinSubmissionOperation(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess
)noexcept{
    const bool operationValid = SubmissionOperation::activeExclusiveFor(*this)
        && planAccess.validFor(compiledGraph)
    ;
    NWB_FATAL_ASSERT_MSG(operationValid, "exception cleanup requires exclusive graph submission ownership");
    if(!operationValid)
        TerminateInvariant();

    for(usize packetIndex = 0u; packetIndex < planAccess.packetCount(); ++packetIndex){
        GpuGraphSubmissionBinding submissionBinding;
        u64 recordingAttemptGeneration = 0u;
        {
            NothrowScopedLock lock(m_mutex);
            const bool transactionValid = validForLocked(planAccess)
                && m_activeSubmissionBinding.valid()
                && m_recordingAttemptGeneration != 0u
                && packetIndex < m_packets.size()
            ;
            NWB_FATAL_ASSERT_MSG(transactionValid, "exception cleanup must retain its graph submission binding");
            if(!transactionValid)
                TerminateInvariant();

            PacketRuntime& runtime = m_packets[packetIndex];
            if(runtime.state == PacketRuntimeState::Accepted || runtime.state == PacketRuntimeState::Rejected)
                continue;
            const bool packetReady = runtime.state == PacketRuntimeState::Declared;
            NWB_FATAL_ASSERT_MSG(packetReady, "exception cleanup cannot cross an active packet operation");
            if(!packetReady)
                TerminateInvariant();
            runtime.state = PacketRuntimeState::Rejecting;
            submissionBinding = m_activeSubmissionBinding;
            recordingAttemptGeneration = m_recordingAttemptGeneration;
        }

        const GpuSubmissionPacketId packetID = planAccess.packetIdAt(packetIndex);
        const bool abandoned = packetID.valid() && graph.abandonUnacceptedPacketWithoutCallbacks(
            compiledGraph,
            planAccess,
            packetID,
            recordingAttemptGeneration,
            submissionBinding
        );
        NWB_FATAL_ASSERT_MSG(abandoned, "exception cleanup must terminalize every unaccepted graph packet");
        if(!abandoned)
            TerminateInvariant();

        const GpuCompiledPacketView packetView = planAccess.packet(packetID);
        if(!packetView.valid())
            TerminateInvariant();
        const GpuSubmissionPacket& packet = *packetView.plan;
        NothrowScopedLock lock(m_mutex);
        const bool runtimeValid = validForLocked(planAccess)
            && packetIndex < m_packets.size()
            && m_packets[packetIndex].state == PacketRuntimeState::Rejecting
        ;
        NWB_FATAL_ASSERT_MSG(runtimeValid, "exception cleanup must retain its packet transaction state");
        if(!runtimeValid)
            TerminateInvariant();
        PacketRuntime& runtime = m_packets[packetIndex];
        runtime.state = PacketRuntimeState::Rejected;
        ++m_submissionStatistics.rejectedPacketCount;
        m_submissionStatistics.rejectedTaskCount += packet.taskCount;
    }

    NothrowScopedLock lock(m_mutex);
    resolveSubmissionBindingIfTerminalLocked(graph, compiledGraph);
    const bool resolutionValid = allPacketsTerminalLocked() && m_submissionBindingResolved;
    NWB_FATAL_ASSERT_MSG(resolutionValid, "exception cleanup must resolve its complete graph submission attempt");
    if(!resolutionValid)
        TerminateInvariant();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

