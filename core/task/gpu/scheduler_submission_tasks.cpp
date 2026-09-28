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
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    SubmissionAttemptExceptionFinalizer exceptionFinalizer(graph, compiledGraph, recordedGraph, transaction);
    GpuRecordedGraph::ArtifactOperation artifactOperation(
        recordedGraph,
        GpuRecordedGraph::ArtifactOperationMode::Read
    );
    if(!artifactOperation.valid()){
        return false;
    }
    if(!GpuGraphSubmissionTransaction::SubmissionOperation::activeExclusiveFor(transaction))
        return false;
    GpuCompiledGraph::ReadView planAccess(compiledGraph);
    if(!planAccess.valid())
        return false;
    GpuTaskGraph::DeclarationReadView declarationAccess = GpuTaskGraph::DeclarationReadView::tryAcquire(graph);
    if(!declarationAccess.valid())
        return false;

    SubmissionAttemptExceptionScope preflightExceptionScope(
        graph,
        compiledGraph,
        recordedGraph,
        transaction,
        outFailedPacket
    );
    if(planAccess.validPacketRange(range))
        preflightExceptionScope.setFailedPacket(range.first);

    if(
        !planAccess.validFor(declarationAccess)
        || !planAccess.validPacketRange(range)
        || !recordedGraph.validForWithinArtifactOperation(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            artifactOperation
        )
        || !transaction.validFor(planAccess)
        || !GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(declarationAccess, planAccess, range)
        || !ValidateTaskCallbackRange(
            declarationAccess,
            planAccess,
            range,
            taskAcceptedCallbacks,
            taskAcceptedCallbackCount
        )
    )
        return false;

    GpuTaskSubmissionDetail::TaskSubmissionBindings bindings(scratchArena);
    if(!bindings.resolve(
        declarationAccess,
        planAccess,
        range,
        taskTimingTickets,
        taskTimingTicketCount,
        taskSubmissionHooks,
        taskSubmissionHookCount
    ))
        return false;

    if(!bindings.timingTickets.empty()){
        for(usize ownerIndex = 0u; ownerIndex < planAccess.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = planAccess.packetIdAt(ownerIndex);
            if(!bindings.validateOwnedTimingTicket(
                ownerPacket,
                recordedGraph.packetTimingTicket(ownerPacket, artifactOperation)
            ))
                return false;
        }
    }

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    const u64 recordingAttemptGeneration =
        recordedGraph.recordingAttemptGenerationWithinArtifactOperation(artifactOperation)
    ;
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(
            packet,
            artifactOperation
        );
        if(
            packetPlan.recordsTiming != static_cast<bool>(ownedTimingTicket)
            || !graph.packetReadyForSubmission(compiledGraph, planAccess, packet, recordingAttemptGeneration)
        )
            return false;
    }
    Vector<GpuTimingSubmissionTicket*, Alloc::ScratchArena> resolvedTimingTickets{ scratchArena };
    resolvedTimingTickets.reserve(bindings.timingTickets.size());
    // The owning composite admission remains active. Release the nested reader only after every mutable artifact query and scratch allocation completes
    preflightExceptionScope.complete();

    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr;
        bindings.collectPacket(packet, resolvedTimingTickets, preSubmitHook);
        SubmissionAttemptExceptionScope packetExceptionScope(
            graph,
            compiledGraph,
            recordedGraph,
            transaction,
            outFailedPacket
        );
        packetExceptionScope.setFailedPacket(packet);
        if(!submitPacketWithinSubmissionOperation(
            graph,
            compiledGraph,
            planAccess,
            recordedGraph,
            artifactOperation,
            packet,
            transaction,
            scratchArena,
            resolvedTimingTickets.data(),
            resolvedTimingTickets.size(),
            preSubmitHook,
            taskAcceptedCallbacks,
            taskAcceptedCallbackCount
        )){
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

