// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"
#include "packet_runtime_internal.h"
#include "task_graph.h"
#include "scheduler_submission_bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskScheduler::submitPacketRangeWithinSubmissionOperation(
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const Span<const GpuTaskGraphTaskTimingTicket> taskTimingTickets,
    const Span<const GpuTaskGraphTaskAcceptedCallback> taskAcceptedCallbacks,
    const Span<const GpuTaskGraphTaskSubmissionHook> taskSubmissionHooks,
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
        !planAccess.validPacketRange(range)
        || !recordedGraph.validForWithinArtifactOperation(graph, declarationAccess, compiledGraph, planAccess, artifactOperation)
        || !transaction.validFor(planAccess)
        || !GpuPacketRuntimeDetail::ValidateExternalDependencyTokens(declarationAccess, planAccess, range)
        || !acceptedCallbacks.resolve(declarationAccess, planAccess, range, taskAcceptedCallbacks.data(), taskAcceptedCallbacks.size())
    )
        return false;

    GpuTaskSubmissionDetail::TaskSubmissionBindings bindings(scratchArena);
    if(!bindings.resolve(declarationAccess, planAccess, range, taskTimingTickets, taskSubmissionHooks))
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
        if(packetPlan.recordsTiming != static_cast<b8>(ownedTimingTicket) || !graph.packetReadyForSubmission(compiledGraph, planAccess, packet, recordingAttemptGeneration))
            return false;
    }
    Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena> resolvedTimingTickets{ scratchArena };
    resolvedTimingTickets.reserve(bindings.timingTickets.size());
    preflightExceptionScope.complete();

    const auto packetHasAcceptedCallback = [&acceptedCallbacks, &planAccess](const GpuSubmissionPacketId packet)noexcept -> b8{
        if(acceptedCallbacks.empty())
            return false;
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        for(u32 taskIndex = 0u; taskIndex < packetView.plan->taskCount; ++taskIndex){
            if(acceptedCallbacks.find(packetView.tasks[taskIndex]) != nullptr)
                return true;
        }
        return false;
    };

    for(usize packetIndex = range.first.index; packetIndex < rangeEnd;){
        const GpuSubmissionPacketId firstPacket = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView firstView = planAccess.packet(firstPacket);
        if(!firstView.valid())
            return false;
        const GpuPhysicalQueueId batchQueue = firstView.plan->queue;
        const QueueSubmissionPreSubmitHook* const firstHook = bindings.collectPacket(firstPacket, resolvedTimingTickets);
        if(firstHook || packetHasAcceptedCallback(firstPacket)){
            SubmissionAttemptExceptionScope packetExceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);
            packetExceptionScope.setFailedPacket(firstPacket);
            if(!submitPacketWithinSubmissionOperation(
                declarationAccess,
                compiledGraph,
                planAccess,
                recordedGraph,
                artifactOperation,
                firstPacket,
                Span<const NotNull<GpuTimingSubmissionTicket*>>(resolvedTimingTickets.data(), resolvedTimingTickets.size()),
                firstHook,
                acceptedCallbacks,
                scratchArena,
                graph,
                transaction
            )){
                if(outFailedPacket)
                    *outFailedPacket = firstPacket;
                return false;
            }
            ++packetIndex;
            continue;
        }

        usize batchEnd = packetIndex + 1u;
        while(batchEnd < rangeEnd){
            const GpuSubmissionPacketId nextPacket = planAccess.packetIdAt(batchEnd);
            const GpuCompiledPacketView nextView = planAccess.packet(nextPacket);
            if(!nextView.valid() || nextView.plan->queue != batchQueue)
                break;
            if(bindings.collectPacket(nextPacket, resolvedTimingTickets) != nullptr)
                break;
            ++batchEnd;
            if(packetHasAcceptedCallback(nextPacket))
                break;
        }

        if(batchEnd - packetIndex == 1u){
            const QueueSubmissionPreSubmitHook* const singleHook = bindings.collectPacket(firstPacket, resolvedTimingTickets);
            SubmissionAttemptExceptionScope packetExceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);
            packetExceptionScope.setFailedPacket(firstPacket);
            if(!submitPacketWithinSubmissionOperation(
                declarationAccess,
                compiledGraph,
                planAccess,
                recordedGraph,
                artifactOperation,
                firstPacket,
                Span<const NotNull<GpuTimingSubmissionTicket*>>(resolvedTimingTickets.data(), resolvedTimingTickets.size()),
                singleHook,
                acceptedCallbacks,
                scratchArena,
                graph,
                transaction
            )){
                if(outFailedPacket)
                    *outFailedPacket = firstPacket;
                return false;
            }
        }
        else{
            GpuSubmissionPacketId batchFailedPacket = firstPacket;
            SubmissionAttemptExceptionScope batchExceptionScope(compiledGraph, recordedGraph, graph, transaction, outFailedPacket);
            batchExceptionScope.setFailedPacket(firstPacket);
            if(!submitPacketBatchWithinSubmissionOperation(
                declarationAccess,
                compiledGraph,
                planAccess,
                recordedGraph,
                artifactOperation,
                packetIndex,
                batchEnd,
                batchQueue,
                bindings,
                acceptedCallbacks,
                scratchArena,
                graph,
                transaction,
                &batchFailedPacket
            )){
                if(outFailedPacket)
                    *outFailedPacket = batchFailedPacket;
                return false;
            }
        }
        packetIndex = batchEnd;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

