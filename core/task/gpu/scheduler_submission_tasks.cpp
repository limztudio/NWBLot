// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "packet_runtime_internal.h"
#include "task_graph.h"
#include "scheduler_submission_bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::submitPacketRangeWithinSubmissionOperation(
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount,
    Alloc::ScratchArena& scratchArena,
    GpuTaskGraph& graph,
    GpuGraphSubmissionTransaction& transaction,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, transaction, compiledGraph, recordedGraph);
    GpuRecordedGraph::ArtifactOperation artifactOperation(recordedGraph, GpuRecordedGraph::ArtifactOperationMode::Read);
    if(!artifactOperation.valid()){
        return false;
    }
    if(!GpuGraphSubmissionTransaction::SubmissionOperation::ActiveExclusiveFor(transaction))
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::TryAcquire(graph);
    if(!declarationAccess.valid())
        return false;

    SubmissionAttemptExceptionScope preflightExceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);
    if(planAccess.validPacketRange(range))
        preflightExceptionScope.setFailedPacket(range.first);

    GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback> acceptedCallbacks(scratchArena);
    if(
        !planAccess.validFor(declarationAccess)
        || !planAccess.validPacketRange(range)
        || !recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactOperation)
        || !transaction.validFor(planAccess)
        || !GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(declarationAccess, planAccess, range)
        || !acceptedCallbacks.resolve(declarationAccess, planAccess, range, taskAcceptedCallbacks, taskAcceptedCallbackCount)
    )
        return false;

    GpuTaskSubmissionDetail::TaskSubmissionBindings bindings(scratchArena);
    if(!bindings.resolve(declarationAccess, planAccess, range, taskTimingTickets, taskTimingTicketCount, taskSubmissionHooks, taskSubmissionHookCount))
        return false;

    if(!bindings.timingTickets.empty()){
        for(usize ownerIndex = 0u; ownerIndex < planAccess.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = planAccess.packetIdAt(ownerIndex);
            if(!bindings.validateOwnedTimingTicket(ownerPacket, recordedGraph.packetTimingTicket(ownerPacket, artifactOperation)))
                return false;
        }
    }

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    const u64 recordingAttemptGeneration = recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation);
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packet, artifactOperation);
        if(packetPlan.recordsTiming != static_cast<bool>(ownedTimingTicket) || !graph.packetReadyForSubmission(compiledGraph, planAccess, packet, recordingAttemptGeneration))
            return false;
    }
    Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena> resolvedTimingTickets{ scratchArena };
    resolvedTimingTickets.reserve(bindings.timingTickets.size());
    // The owning composite admission remains active. Release the nested reader only after every mutable artifact query and scratch allocation completes.
    preflightExceptionScope.complete();

    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        const QueueSubmissionPreSubmitHook* const preSubmitHook = bindings.collectPacket(packet, resolvedTimingTickets);
        SubmissionAttemptExceptionScope packetExceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);
        packetExceptionScope.setFailedPacket(packet);
        if(!submitPacketWithinSubmissionOperation(compiledGraph, planAccess, recordedGraph, artifactOperation, packet, resolvedTimingTickets.size(), preSubmitHook, acceptedCallbacks, scratchArena, resolvedTimingTickets.data(), graph, transaction)){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

