// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler_submission_bindings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_submission_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_InlineBindingCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskSubmissionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_gpu_task_submission_bindings;


TaskSubmissionBindings::TaskSubmissionBindings(Alloc::ScratchArena& scratchArena)
    : scratchArena(scratchArena)
    , timingTickets(scratchArena)
    , submissionHooks(scratchArena)
    , nextTimingTicket(scratchArena)
{}

[[nodiscard]] bool TaskSubmissionBindings::resolve(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketRange& range,
    const GpuTaskGraphTaskTimingTicket* const taskTimingTickets,
    const usize taskTimingTicketCount,
    const GpuTaskGraphTaskSubmissionHook* const taskSubmissionHooks,
    const usize taskSubmissionHookCount
){
    if((taskTimingTicketCount != 0u && !taskTimingTickets) || (taskSubmissionHookCount != 0u && !taskSubmissionHooks))
        return false;
    timingTickets.clear();
    submissionHooks.clear();
    nextTimingTicket.clear();
    submissionHookIndices.reset();
    timingPacketChains.reset();
    timingTicketIndices.reset();
    packetGeneration = range.first.generation;
    // Match member allocation and destruction order so a single admitted range releases scratch storage in LIFO order.
    timingTickets.reserve(taskTimingTicketCount);
    submissionHooks.reserve(taskSubmissionHookCount);
    if(taskTimingTicketCount > s_InlineBindingCount){
        nextTimingTicket.reserve(taskTimingTicketCount);
        timingTicketIndices.emplace(AddSize(taskTimingTicketCount, taskTimingTicketCount), scratchArena);
        const usize chainCount = Min(taskTimingTicketCount, range.packetCount);
        timingPacketChains.emplace(AddSize(chainCount, chainCount), scratchArena);
    }
    if(taskSubmissionHookCount > s_InlineBindingCount)
        submissionHookIndices.emplace(AddSize(taskSubmissionHookCount, taskSubmissionHookCount), scratchArena);

    Optional<HashSet<u32, Alloc::ScratchArena>> timingAnchors;
    if(taskTimingTicketCount > s_InlineBindingCount)
        timingAnchors.emplace(AddSize(taskTimingTicketCount, taskTimingTicketCount), scratchArena);
    const usize rangeEnd = static_cast<usize>(range.first.index) + range.packetCount;
    for(usize bindingIndex = 0u; bindingIndex < taskTimingTicketCount; ++bindingIndex){
        const GpuTaskGraphTaskTimingTicket& binding = taskTimingTickets[bindingIndex];
        if(!binding.timingTicket || !declarationAccess.validTask(binding.task))
            return false;
        const GpuCompiledTaskView task = planAccess.findTask(binding.task);
        if(!task.valid())
            return false;

        if(timingAnchors){
            // Declaration validity establishes the graph generation before index-only anchor membership.
            if(!timingAnchors->insert(binding.task.index).second)
                return false;
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

        bool ticketAlreadyBound = false;
        if(timingTicketIndices){
            auto [it, inserted] = timingTicketIndices->try_emplace(binding.timingTicket, timingTickets.size());
            if(!inserted){
                if(timingTickets[it.value()].packet != packet)
                    return false;
                ticketAlreadyBound = true;
            }
        }
        else{
            for(const ResolvedTimingTicket& existing : timingTickets){
                if(existing.timingTicket != binding.timingTicket)
                    continue;
                // One one-shot ticket may have semantic aliases only within the same merged packet.
                if(existing.packet != packet)
                    return false;
                ticketAlreadyBound = true;
                break;
            }
        }
        if(ticketAlreadyBound)
            continue;

        const usize ticketIndex = timingTickets.size();
        timingTickets.push_back(ResolvedTimingTicket{ .packet = packet, .timingTicket = binding.timingTicket });
        if(timingPacketChains){
            nextTimingTicket.push_back(Limit<usize>::s_Max);
            auto [it, inserted] = timingPacketChains->try_emplace(packet.index, TimingPacketChain{ ticketIndex, ticketIndex });
            if(!inserted){
                TimingPacketChain& chain = it.value();
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

        // Packet uniqueness also rejects duplicate task anchors: one native submission has one hook.
        if(submissionHookIndices){
            if(!submissionHookIndices->try_emplace(packet.index, submissionHooks.size()).second)
                return false;
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

[[nodiscard]] bool TaskSubmissionBindings::validateOwnedTimingTicket(
    const GpuSubmissionPacketId packet,
    GpuTimingSubmissionTicket* const timingTicket
)const noexcept{
    if(!timingTicket)
        return true;
    if(timingTicketIndices){
        const auto it = timingTicketIndices->find(timingTicket);
        return it == timingTicketIndices->end() || timingTickets[it.value()].packet == packet;
    }
    for(const ResolvedTimingTicket& ticket : timingTickets){
        if(ticket.packet != packet && ticket.timingTicket == timingTicket)
            return false;
    }
    return true;
}

void TaskSubmissionBindings::collectPacket(
    const GpuSubmissionPacketId packet,
    Vector<GpuTimingSubmissionTicket*, Alloc::ScratchArena>& outTimingTickets,
    const QueueSubmissionPreSubmitHook*& outPreSubmitHook
)const{
    outTimingTickets.clear();
    outPreSubmitHook = nullptr;
    if(timingTickets.empty() && submissionHooks.empty())
        return;

    if(timingPacketChains){
        if(packet.generation == packetGeneration){
            const auto it = timingPacketChains->find(packet.index);
            if(it != timingPacketChains->end()){
                // Links retain first authored occurrence, even when bindings interleave several packets.
                for(usize index = it.value().first; index != Limit<usize>::s_Max; index = nextTimingTicket[index])
                    outTimingTickets.push_back(timingTickets[index].timingTicket);
            }
        }
    }
    else{
        for(const ResolvedTimingTicket& ticket : timingTickets){
            if(ticket.packet == packet)
                outTimingTickets.push_back(ticket.timingTicket);
        }
    }
    if(submissionHookIndices){
        if(packet.generation == packetGeneration){
            const auto it = submissionHookIndices->find(packet.index);
            if(it != submissionHookIndices->end())
                outPreSubmitHook = &submissionHooks[it.value()].hook;
        }
    }
    else{
        for(const ResolvedSubmissionHook& hook : submissionHooks){
            if(hook.packet == packet){
                outPreSubmitHook = &hook.hook;
                break;
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

