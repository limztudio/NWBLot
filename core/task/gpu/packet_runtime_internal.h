// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"

#include <core/graphics/gpu_timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRecordedGraph::ArtifactStorage final : NoCopy{
    GraphicsVector<GpuRecordedPacket> packets;
    GraphicsVector<u8> packetRecordingOverlaps;
    GraphicsVector<GlobalUniquePtr<GpuTimingSubmissionTicket>> packetTimingTickets;
    GraphicsVector<CommandListResourceStateHandoff> packetStateSeeds;
    GpuTimingRecorder* timingRecorder = nullptr;
    const GpuTaskGraph* graphIdentity = nullptr;
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;
    usize packetCount = 0u;
    u64 compiledObjectIdentity = 0u;
    u64 generation = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    b8 valid = false;


public:
    explicit ArtifactStorage(GraphicsArena& arena)noexcept
        : packets(arena)
        , packetRecordingOverlaps(arena)
        , packetTimingTickets(arena)
        , packetStateSeeds(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder final : NoCopy{
    friend class GpuTaskScheduler;


private:
    class PacketRecordingExceptionScope;
    class PacketArtifactPublicationScope;
    class ReadyFrontierRecordingUnwindScope;


private:
    explicit GpuNativePacketRecorder(Device& device, GpuTimingRecorder* const timingRecorder = nullptr)noexcept
        : m_device(device)
        , m_timingRecorder(timingRecorder)
    {}


private:
    [[nodiscard]] b8 recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* outCommandIrCapture = nullptr
    )const;
    [[nodiscard]] b8 recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* outCommandIrCapture = nullptr
    )const;
    [[nodiscard]] b8 recordPacketRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        CpuTaskScheduler& cpuScheduler,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* outCommandIrCapture = nullptr
    )const;


private:
    [[nodiscard]] b8 recordPreparedPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketRange& range,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        GpuRecordedGraph& outRecordedGraph,
        GpuCommandIrCapture* outCommandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    [[nodiscard]] b8 recordPacket(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        GpuSubmissionPacketId packetID,
        u64 recordingWorkerDomain,
        u32 recordingWorkerIndex,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        GpuRecordedGraph& outRecordedGraph,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        GpuPacketRecordingAbort* deferredAbort = nullptr
    )const;
    [[nodiscard]] b8 prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] b8 preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        GpuSubmissionPacketId packetID,
        const CommandListResourceStateHandoff* initialStates,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuRecordedGraph::PacketRecordingScratch& scratch
    )const;


private:
    Device& m_device;
    GpuTimingRecorder* m_timingRecorder = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PacketWaitStatistics{
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
};

[[nodiscard]] PacketWaitStatistics CountPacketWaitStatistics(GpuPhysicalQueueId queue, Span<const QueueSubmissionToken> waitTokens, Alloc::ScratchArena& scratchArena);
[[nodiscard]] u64 AllocateAcceptanceRevision()noexcept;

[[nodiscard]] b8 ValidateInitialOwnershipCompletionToken(const GpuTaskGraphInitialOwnerHandoffSourceView& source, const GpuCompiledBarrier& barrier, const GpuPhysicalQueueInfo& sourceQueue, const QueueSubmissionToken& token, u16 deviceGeneration)noexcept;
[[nodiscard]] b8 ValidateInitialOwnershipCompletions(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId& packetID)noexcept;

[[nodiscard]] b8 ValidateExternalDependencyTokens(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketRange& range)noexcept;

FOUNDATION_INLINE void WaitAtomicStateValue(const Atomic<u32>& state, Atomic<u32>& waiters, const u32 expected)noexcept{
    waiters.fetch_add(1u, MemoryOrder::seq_cst);
    if(state.load(MemoryOrder::acquire) == expected)
        state.wait(expected, MemoryOrder::acquire);
    waiters.fetch_sub(1u, MemoryOrder::relaxed);
}

FOUNDATION_INLINE void NotifyAllIfWaiting(Atomic<u32>& state, const Atomic<u32>& waiters)noexcept{
    AtomicThreadFence(MemoryOrder::seq_cst);
    if(waiters.load(MemoryOrder::relaxed) != 0u)
        state.notify_all();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

