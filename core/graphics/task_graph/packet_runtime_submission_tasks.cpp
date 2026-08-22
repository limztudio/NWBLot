// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "packet_runtime.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskGraphSubmitter::submitPacketRangeInCompileOrderFromTasks(
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
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
)const{
    if(
        !compiledGraph.validFor(graph)
        || !compiledGraph.validPacketRange(range)
        || (taskTimingTicketCount != 0u && !taskTimingTickets)
        || (submissionHookCount != 0u && !submissionHooks)
        || (taskSubmissionHookCount != 0u && !taskSubmissionHooks)
    )
        return false;

    Vector<GpuTaskGraphPacketTimingTicket, Alloc::ScratchArena> packetTimingTickets{ scratchArena };
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

        bool packetAlreadyBound = false;
        for(const GpuTaskGraphPacketTimingTicket& existing : packetTimingTickets){
            if(existing.packet != packet)
                continue;
            // A merged packet submits exactly one native command list.  Let semantic anchors intentionally share
            // that one ticket, but reject silently choosing between two independent timing transactions.
            if(existing.timingTicket != binding.timingTicket)
                return false;
            packetAlreadyBound = true;
            break;
        }
        if(!packetAlreadyBound){
            packetTimingTickets.push_back(GpuTaskGraphPacketTimingTicket{
                .packet = packet,
                .timingTicket = binding.timingTicket,
            });
        }
    }

    Vector<GpuTaskGraphPacketSubmissionHook, Alloc::ScratchArena> packetSubmissionHooks{ scratchArena };
    packetSubmissionHooks.reserve(submissionHookCount + taskSubmissionHookCount);
    for(usize hookIndex = 0u; hookIndex < submissionHookCount; ++hookIndex)
        packetSubmissionHooks.push_back(submissionHooks[hookIndex]);
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

        for(const GpuTaskGraphPacketSubmissionHook& existing : packetSubmissionHooks){
            // A native submission can emit one unambiguous one-shot signal. Do not silently choose a packet
            // compatibility binding over a semantic target, or two merged semantic targets over each other.
            if(existing.packet == packet)
                return false;
        }
        packetSubmissionHooks.push_back(GpuTaskGraphPacketSubmissionHook{
            .packet = packet,
            .hook = binding.hook,
        });
    }

    return submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        range,
        externalCompletionTokens,
        externalCompletionTokenCount,
        packetTimingTickets.data(),
        packetTimingTickets.size(),
        transaction,
        scratchArena,
        outFailedPacket,
        acceptedCallback,
        packetSubmissionHooks.data(),
        packetSubmissionHooks.size(),
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount
    );
}


bool GpuTaskGraphSubmitter::submitTaskRangeInCompileOrderFromTasks(
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
    const GpuTaskGraphPacketAcceptedCallback* const acceptedCallback,
    const GpuTaskGraphPacketSubmissionHook* const submissionHooks,
    const usize submissionHookCount,
    const GpuTaskGraphTaskAcceptedCallback* const taskAcceptedCallbacks,
    const usize taskAcceptedCallbackCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
)const{
    return submitPacketRangeInCompileOrderFromTasks(
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
        acceptedCallback,
        submissionHooks,
        submissionHookCount,
        taskAcceptedCallbacks,
        taskAcceptedCallbackCount,
        taskSubmissionHooks,
        taskSubmissionHookCount
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

