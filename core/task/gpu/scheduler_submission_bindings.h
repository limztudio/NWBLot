// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskSubmissionDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ResolvedTimingTicket{
    GpuSubmissionPacketId packet;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};

struct ResolvedSubmissionHook{
    GpuSubmissionPacketId packet;
    QueueSubmissionPreSubmitHook hook;
};

struct TimingPacketChain{
    usize first = Limit<usize>::s_Max;
    usize last = Limit<usize>::s_Max;
};

struct TaskSubmissionBindings : NoCopy{
    Alloc::ScratchArena& scratchArena;
    Vector<ResolvedTimingTicket, Alloc::ScratchArena> timingTickets;
    Vector<ResolvedSubmissionHook, Alloc::ScratchArena> submissionHooks;
    Vector<usize, Alloc::ScratchArena> nextTimingTicket;
    Optional<HashMap<GpuTimingSubmissionTicket*, usize, Alloc::ScratchArena>> timingTicketIndices;
    Optional<HashMap<u32, TimingPacketChain, Alloc::ScratchArena>> timingPacketChains;
    Optional<HashMap<u32, usize, Alloc::ScratchArena>> submissionHookIndices;
    u64 packetGeneration = 0u;


    explicit TaskSubmissionBindings(Alloc::ScratchArena& scratchArena);


    // The caller holds validated declaration and compiled-plan views for the admitted packet range.
    [[nodiscard]] bool resolve(
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketRange& range,
        const GpuTaskGraphTaskTimingTicket* taskTimingTickets,
        usize taskTimingTicketCount,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks,
        usize taskSubmissionHookCount
    );
    [[nodiscard]] bool validateOwnedTimingTicket(GpuSubmissionPacketId packet, GpuTimingSubmissionTicket* timingTicket)const noexcept;
    [[nodiscard]] const QueueSubmissionPreSubmitHook* collectPacket(
        GpuSubmissionPacketId packet,
        Vector<GpuTimingSubmissionTicket*, Alloc::ScratchArena>& inOutTimingTickets
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

