// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler_submission_bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_scheduler_submission_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_InlineBindingCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskSubmissionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_gpu_task_scheduler_submission_bindings;


[[nodiscard]] b8 TaskSubmissionBindings::resolve(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketRange& range,
    const Span<const GpuTaskGraphTaskTimingTicket> taskTimingTickets,
    const Span<const GpuTaskGraphTaskSubmissionHook> taskSubmissionHooks
){
    const usize taskTimingTicketCount = taskTimingTickets.size();
    const usize taskSubmissionHookCount = taskSubmissionHooks.size();
    if((taskTimingTicketCount != 0u && !taskTimingTickets.data()) || (taskSubmissionHookCount != 0u && !taskSubmissionHooks.data()))
        return false;
    timingTickets.clear();
    submissionHooks.clear();
    nextTimingTicket.clear();
    submissionHookIndices.clear();
    timingPacketChains.clear();
    timingTicketIndices.reset();
    packetGeneration = range.first.generation;
    packetRangeFirstIndex = range.first.index;
    timingTickets.reserve(taskTimingTicketCount);
    submissionHooks.reserve(taskSubmissionHookCount);
    const b8 useTimingTable = taskTimingTicketCount > s_InlineBindingCount;
    const b8 useHookTable = taskSubmissionHookCount > s_InlineBindingCount;
    Vector<u8, Alloc::ScratchArena> timingAnchors(scratchArena);
    if(useTimingTable){
        nextTimingTicket.reserve(taskTimingTicketCount);
        timingTicketIndices.emplace(AddSize(taskTimingTicketCount, taskTimingTicketCount), scratchArena);
        timingPacketChains.assign(range.packetCount, TimingPacketChain{});
        timingAnchors.assign(declarationAccess.taskCount(), 0u);
    }
    if(useHookTable)
        submissionHookIndices.assign(range.packetCount, Limit<u32>::s_Max);

    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize bindingIndex = 0u; bindingIndex < taskTimingTicketCount; ++bindingIndex){
        const GpuTaskGraphTaskTimingTicket& binding = taskTimingTickets[bindingIndex];
        if(!binding.timingTicket || !declarationAccess.validTask(binding.task))
            return false;
        const GpuCompiledTaskView task = planAccess.findTask(binding.task);
        if(!task.valid())
            return false;

        if(useTimingTable){
            if(timingAnchors[binding.task.index] != 0u)
                return false;
            timingAnchors[binding.task.index] = 1u;
        }
        else{
            for(usize previousBindingIndex = 0u; previousBindingIndex < bindingIndex; ++previousBindingIndex){
                if(taskTimingTickets[previousBindingIndex].task == binding.task)
                    return false;
            }
        }

        const GpuSubmissionPacketId packet = task.plan->packet;
        if(!packet.valid() || packet.index < range.first.index || static_cast<usize>(packet.index) >= rangeEnd)
            return false;

        b8 ticketAlreadyBound = false;
        if(timingTicketIndices){
            auto [it, inserted] = timingTicketIndices->try_emplace(MakeNotNull(binding.timingTicket), timingTickets.size());
            if(!inserted){
                if(timingTickets[it.value()].packet != packet)
                    return false;
                ticketAlreadyBound = true;
            }
        }
        else{
            for(const ResolvedTimingTicket& existing : timingTickets){
                if(existing.timingTicket.get() != binding.timingTicket)
                    continue;
                if(existing.packet != packet)
                    return false;
                ticketAlreadyBound = true;
                break;
            }
        }
        if(ticketAlreadyBound)
            continue;

        const usize ticketIndex = timingTickets.size();
        timingTickets.push_back(ResolvedTimingTicket{ .packet = packet, .timingTicket = MakeNotNull(binding.timingTicket) });
        if(useTimingTable){
            nextTimingTicket.push_back(Limit<usize>::s_Max);
            TimingPacketChain& chain = timingPacketChains[packet.index - packetRangeFirstIndex];
            if(chain.first == Limit<usize>::s_Max){
                chain.first = ticketIndex;
                chain.last = ticketIndex;
            }
            else{
                nextTimingTicket[chain.last] = ticketIndex;
                chain.last = ticketIndex;
            }
        }
    }

    for(usize bindingIndex = 0u; bindingIndex < taskSubmissionHookCount; ++bindingIndex){
        const GpuTaskGraphTaskSubmissionHook& binding = taskSubmissionHooks[bindingIndex];
        if(!binding.hook.valid() || !declarationAccess.validTask(binding.task))
            return false;
        const GpuCompiledTaskView task = planAccess.findTask(binding.task);
        if(!task.valid())
            return false;
        const GpuSubmissionPacketId packet = task.plan->packet;
        if(!packet.valid() || packet.index < range.first.index || static_cast<usize>(packet.index) >= rangeEnd)
            return false;

        if(useHookTable){
            u32& slot = submissionHookIndices[packet.index - packetRangeFirstIndex];
            if(slot != Limit<u32>::s_Max)
                return false;
            slot = static_cast<u32>(submissionHooks.size());
        }
        else{
            for(const ResolvedSubmissionHook& existing : submissionHooks){
                if(existing.packet == packet)
                    return false;
            }
        }
        submissionHooks.push_back(ResolvedSubmissionHook{ .packet = packet, .hook = binding.hook });
    }
    return true;
}

[[nodiscard]] b8 TaskSubmissionBindings::validateOwnedTimingTicket(const GpuSubmissionPacketId packet, GpuTimingSubmissionTicket* const timingTicket)const noexcept{
    if(!timingTicket)
        return true;
    if(timingTicketIndices){
        const auto it = timingTicketIndices->find(MakeNotNull(timingTicket));
        return it == timingTicketIndices->end() || timingTickets[it.value()].packet == packet;
    }
    for(const ResolvedTimingTicket& ticket : timingTickets){
        if(ticket.packet != packet && ticket.timingTicket.get() == timingTicket)
            return false;
    }
    return true;
}

const QueueSubmissionPreSubmitHook* TaskSubmissionBindings::collectPacket(const GpuSubmissionPacketId packet, Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena>& inOutTimingTickets)const{
    inOutTimingTickets.clear();
    const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr;
    if(timingTickets.empty() && submissionHooks.empty())
        return nullptr;

    const b8 packetInRange = packet.generation == packetGeneration && packet.index >= packetRangeFirstIndex;
    const usize packetOffset = packetInRange ? static_cast<usize>(packet.index - packetRangeFirstIndex) : 0u;
    if(!timingPacketChains.empty()){
        if(packetInRange && packetOffset < timingPacketChains.size()){
            for(usize index = timingPacketChains[packetOffset].first; index != Limit<usize>::s_Max; index = nextTimingTicket[index])
                inOutTimingTickets.push_back(timingTickets[index].timingTicket);
        }
    }
    else{
        for(const ResolvedTimingTicket& ticket : timingTickets){
            if(ticket.packet == packet)
                inOutTimingTickets.push_back(ticket.timingTicket);
        }
    }
    if(!submissionHookIndices.empty()){
        if(packetInRange && packetOffset < submissionHookIndices.size()){
            const u32 hookIndex = submissionHookIndices[packetOffset];
            if(hookIndex != Limit<u32>::s_Max)
                preSubmitHook = &submissionHooks[hookIndex].hook;
        }
    }
    else{
        for(const ResolvedSubmissionHook& hook : submissionHooks){
            if(hook.packet == packet){
                preSubmitHook = &hook.hook;
                break;
            }
        }
    }
    return preSubmitHook;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

