// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_packet_runtime_submission_tasks{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ValidateExternalCompletionBindings(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuTaskGraphExternalCompletionToken* const bindings,
    const usize bindingCount
){
    if(bindingCount != 0u && !bindings)
        return false;

    for(usize bindingIndex = 0u; bindingIndex < bindingCount; ++bindingIndex){
        const GpuTaskGraphExternalCompletionToken& binding = bindings[bindingIndex];
        if(!binding.validFallbackFor(graph, compiledGraph))
            return false;
        for(usize previousIndex = 0u; previousIndex < bindingIndex; ++previousIndex){
            if(bindings[previousIndex].completion == binding.completion)
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool ValidateTaskAcceptedCallbacks(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
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
            || !graph.validTask(callback.task)
            || !compiledGraph.findTask(callback.task)
        )
            return false;

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(callback.task);
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


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrder(
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
    if(outFailedPacket)
        *outFailedPacket = {};
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacketRange(range)
        || !recordedGraph.validFor(graph, compiledGraph)
        || !transaction.validFor(compiledGraph)
        || (taskTimingTicketCount != 0u && !taskTimingTickets)
        || (taskSubmissionHookCount != 0u && !taskSubmissionHooks)
        || !__hidden_gpu_packet_runtime_submission_tasks::ValidateExternalCompletionBindings(
            graph,
            compiledGraph,
            externalCompletionTokens,
            externalCompletionTokenCount
        )
        || !__hidden_gpu_packet_runtime_submission_tasks::ValidateTaskAcceptedCallbacks(
            graph,
            compiledGraph,
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
            || !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
        )
            return false;

        for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
            if(taskTimingTickets[previousBindingIndex].task == binding.task)
                return false;
        }

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(binding.task);
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
        for(usize ownerIndex = 0u; ownerIndex < compiledGraph.packetCount(); ++ownerIndex){
            const GpuSubmissionPacketId ownerPacket = compiledGraph.packetIdAt(ownerIndex);
            if(ownerPacket != ticket.packet && recordedGraph.packetTimingTicket(ownerPacket) == ticket.timingTicket)
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
            || !graph.validTask(binding.task)
            || !compiledGraph.findTask(binding.task)
        )
            return false;

        for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
            if(taskSubmissionHooks[previousBindingIndex].task == binding.task)
                return false;
        }

        const GpuSubmissionPacketId packet = compiledGraph.packetForTask(binding.task);
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
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
        const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
        GpuTimingSubmissionTicket* const ownedTimingTicket = recordedGraph.packetTimingTicket(packet);
        if(packetPlan.recordsTiming != static_cast<bool>(ownedTimingTicket))
            return false;
    }

    Vector<GpuTimingSubmissionTicket*, Alloc::ScratchArena> resolvedTimingTickets{ scratchArena };
    resolvedTimingTickets.reserve(packetTimingTickets.size());
    for(usize packetIndex = range.first.index; packetIndex < rangeEnd; ++packetIndex){
        const GpuSubmissionPacketId packet = compiledGraph.packetIdAt(packetIndex);
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
        GpuGraphSubmissionTransaction::SubmissionOperation submissionOperation(transaction);
        if(
            !submissionOperation.valid()
            || !submitPacketWithinSubmissionOperation(
                graph,
                compiledGraph,
                recordedGraph,
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
            )
        ){
            if(outFailedPacket)
                *outFailedPacket = packet;
            return false;
        }
    }
    return true;
}


bool GpuTaskGraphSubmitter::submitTaskRangeInCompileOrder(
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
    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.packetRangeForTasks(firstTask, lastTask),
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

