// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"
#include "packet_runtime_internal.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_submission_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateTaskAcceptedCallbacks(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskAcceptedCallback* const callbacks,
    const usize callbackCount
){
    if(callbackCount != 0u && !callbacks)
        return false;

    for(usize callbackIndex = 0u; callbackIndex < callbackCount; ++callbackIndex){
        const GpuTaskGraphTaskAcceptedCallback& callback = callbacks[callbackIndex];
        if(
            !callback.invoke
            || !declarationAccess.validTask(callback.task)
            || !planAccess.findTask(callback.task).valid()
        )
            return false;

        const GpuSubmissionPacketId packet = planAccess.packetForTask(callback.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;
        for(usize previousIndex = 0u; previousIndex < callbackIndex; ++previousIndex){
            if(callbacks[previousIndex].task == callback.task)
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::submitPacketRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
)const{
    return submitPacketRangeInCompileOrderWithOperationPolicy(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        PacketRangeSubmissionOperationPolicy::PerPacket,
        externalCompletionTokens,
        externalCompletionTokenCount,
        taskTimingTickets,
        taskTimingTicketCount,
        transaction,
        scratchArena,
        outFailedPacket,
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount,
        taskSubmissionHooks,
        taskSubmissionHookCount
    );
}


bool GpuTaskScheduler::submitTaskRangeInCompileOrder(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuTaskId firstTask,
    const GpuTaskId lastTask,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
)const{
    if(outFailedPacket)
        *outFailedPacket = {};
    GpuSubmissionPacketRange range;
    {
        GpuCompiledGraph::ReadView planAccess(compiledGraph);
        if(!planAccess.valid())
            return false;
        range = planAccess.packetRangeForTasks(firstTask, lastTask);
    }
    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        externalCompletionTokens,
        externalCompletionTokenCount,
        taskTimingTickets,
        taskTimingTicketCount,
        transaction,
        scratchArena,
        outFailedPacket,
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount,
        taskSubmissionHooks,
        taskSubmissionHookCount
    );
}


bool GpuTaskScheduler::submitPacketRangeInCompileOrderWithOperationPolicy(
    GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuSubmissionPacketRange& range,
    const PacketRangeSubmissionOperationPolicy operationPolicy,
    const GpuTaskGraphExternalCompletionToken* const externalCompletionTokens,
    const usize externalCompletionTokenCount,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    GpuGraphSubmissionTransaction& transaction,
    Alloc::ScratchArena& scratchArena,
    GpuSubmissionPacketId* const outFailedPacket,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
)const{
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
    Optional<GpuGraphSubmissionTransaction::SubmissionOperation> preflightOperation;
    if(operationPolicy == PacketRangeSubmissionOperationPolicy::PerPacket){
        preflightOperation.emplace(
            transaction,
            GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
            &artifactOperation
        );
    }
    if(
        (preflightOperation && !preflightOperation->valid())
        || (
            operationPolicy == PacketRangeSubmissionOperationPolicy::ActiveExclusiveBarrier
            && !GpuGraphSubmissionTransaction::SubmissionOperation::activeExclusiveFor(transaction)
        )
    ){
        return false;
    }
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
        || (taskTimingTicketCount != 0u && !taskTimingTickets)
        || (taskSubmissionHookCount != 0u && !taskSubmissionHooks)
        || !GpuPacketRuntimeDetail::ValidateExternalCompletionBindings(
            graph,
            declarationAccess,
            compiledGraph,
            planAccess,
            externalCompletionTokens,
            externalCompletionTokenCount
        )
        || !__hidden_gpu_packet_runtime_submission_tasks::ValidateTaskAcceptedCallbacks(
            declarationAccess,
            planAccess,
            range,
            taskAcceptedCallbacks,
            taskAcceptedCallbackCount
        )
    )
        return false;

    struct ResolvedTaskTimingTicket{
        GpuSubmissionPacketId packet;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
    };
    Vector<ResolvedTaskTimingTicket, Alloc::ScratchArena> packetTimingTickets{ scratchArena };
    packetTimingTickets.reserve(taskTimingTicketCount);
    for(usize bindingIndex = 0u; bindingIndex < taskTimingTicketCount; ++bindingIndex){
        const GpuTaskGraphTaskTimingTicket& binding = taskTimingTickets[bindingIndex];
        if(
            !binding.timingTicket
            || !declarationAccess.validTask(binding.task)
            || !planAccess.findTask(binding.task).valid()
        )
            return false;

        for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
            if(taskTimingTickets[previousBindingIndex].task == binding.task)
                return false;
        }

        const GpuSubmissionPacketId packet = planAccess.packetForTask(binding.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;

        bool ticketAlreadyBound = false;
        for(const ResolvedTaskTimingTicket& existing : packetTimingTickets){
            if(existing.timingTicket != binding.timingTicket)
                continue;
            // One ticket is a one-shot native-submission transaction. Semantic aliases may share it only when the
            // compiler resolves every anchor to the same merged packet.
            if(existing.packet != packet)
                return false;
            ticketAlreadyBound = true;
            break;
        }
        if(!ticketAlreadyBound){
            packetTimingTickets.push_back(ResolvedTaskTimingTicket{
                .packet = packet,
                .timingTicket = binding.timingTicket,
            });
        }
    }

    for(const ResolvedTaskTimingTicket& ticket : packetTimingTickets){
        for(usize ownerIndex = 0u; ownerIndex < planAccess.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = planAccess.packetIdAt(ownerIndex);
            if(
                ownerPacket != ticket.packet
                && recordedGraph.packetTimingTicket(ownerPacket, artifactOperation) == ticket.timingTicket
            )
                return false;
        }
    }

    struct ResolvedTaskSubmissionHook{
        GpuSubmissionPacketId packet;
        QueueSubmissionPreSubmitHook hook;
    };
    Vector<ResolvedTaskSubmissionHook, Alloc::ScratchArena> packetSubmissionHooks{ scratchArena };
    packetSubmissionHooks.reserve(taskSubmissionHookCount);
    for(usize bindingIndex = 0u; bindingIndex < taskSubmissionHookCount; ++bindingIndex){
        const GpuTaskGraphTaskSubmissionHook& binding = taskSubmissionHooks[bindingIndex];
        if(
            !binding.hook.valid()
            || !declarationAccess.validTask(binding.task)
            || !planAccess.findTask(binding.task).valid()
        )
            return false;

        for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
            if(taskSubmissionHooks[previousBindingIndex].task == binding.task)
                return false;
        }

        const GpuSubmissionPacketId packet = planAccess.packetForTask(binding.task);
        if(
            !packet.valid()
            || packet.index < range.first.index
            || static_cast<usize>(packet.index) >= static_cast<usize>(range.first.index) + range.packetCount
        )
            return false;

        for(const ResolvedTaskSubmissionHook& existing : packetSubmissionHooks){
            // A native submission can emit one unambiguous one-shot signal. Do not silently choose between two
            // semantic targets that the compiler merged into one packet.
            if(existing.packet == packet)
                return false;
        }
        packetSubmissionHooks.push_back(ResolvedTaskSubmissionHook{
            .packet = packet,
            .hook = binding.hook,
        });
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
    resolvedTimingTickets.reserve(packetTimingTickets.size());
    // Per-packet admission keeps independent native queues concurrent. Release the range preflight reader only after
    // every mutable recorded-artifact query and every scratch allocation is complete; packet work cannot allocate
    // outside the operation that owns its exception cleanup.
    preflightExceptionScope.complete();
    preflightOperation.reset();

    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = planAccess.packetIdAt(packetIndex);
        const GpuCompiledPacketView packetView = planAccess.packet(packet);
        if(!packetView.valid())
            return false;
        const GpuSubmissionPacket& packetPlan = *packetView.plan;
        resolvedTimingTickets.clear();
        for(const ResolvedTaskTimingTicket& ticket : packetTimingTickets){
            if(ticket.packet == packet)
                resolvedTimingTickets.push_back(ticket.timingTicket);
        }
        const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr;
        for(const ResolvedTaskSubmissionHook& hook : packetSubmissionHooks){
            if(hook.packet == packet){
                preSubmitHook = &hook.hook;
                break;
            }
        }
        Optional<GpuGraphSubmissionTransaction::SubmissionOperation> submissionOperation;
        if(operationPolicy == PacketRangeSubmissionOperationPolicy::PerPacket){
            submissionOperation.emplace(
                transaction,
                packetPlan.joinsAcceptedQueueFrontier || preSubmitHook
                    ? GpuGraphSubmissionTransaction::SubmissionOperationMode::WaitExclusiveBarrier
                    : GpuGraphSubmissionTransaction::SubmissionOperationMode::OrdinaryPacket,
                &artifactOperation
            );
        }
        if(submissionOperation && !submissionOperation->valid()){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
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
            externalCompletionTokens,
            externalCompletionTokenCount,
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

