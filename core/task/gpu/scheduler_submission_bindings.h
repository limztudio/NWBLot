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
    NotNull<GpuTimingSubmissionTicket*> timingTicket;
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
    Optional<HashMap<NotNull<GpuTimingSubmissionTicket*>, usize, Alloc::ScratchArena>> timingTicketIndices;
    Optional<HashMap<u32, TimingPacketChain, Alloc::ScratchArena>> timingPacketChains;
    Optional<HashMap<u32, usize, Alloc::ScratchArena>> submissionHookIndices;
    u64 packetGeneration = 0u;


public:
    explicit TaskSubmissionBindings(Alloc::ScratchArena& scratchArena)noexcept
        : scratchArena(scratchArena)
        , timingTickets(scratchArena)
        , submissionHooks(scratchArena)
        , nextTimingTicket(scratchArena)
    {}


public:
    // The caller holds validated declaration and compiled-plan views for the admitted packet range.
    [[nodiscard]] b8 resolve(
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketRange& range,
        Span<const GpuTaskGraphTaskTimingTicket> taskTimingTickets,
        Span<const GpuTaskGraphTaskSubmissionHook> taskSubmissionHooks
    );
    [[nodiscard]] b8 validateOwnedTimingTicket(GpuSubmissionPacketId packet, GpuTimingSubmissionTicket* timingTicket)const noexcept;
    [[nodiscard]] const QueueSubmissionPreSubmitHook* collectPacket(GpuSubmissionPacketId packet, Vector<NotNull<GpuTimingSubmissionTicket*>, Alloc::ScratchArena>& inOutTimingTickets)const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

