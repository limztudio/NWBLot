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
    PacketRecordingScratch serialRecordingScratch;
    GraphicsVector<PacketRecordingScratch> packetRecordingScratch;
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
    bool valid = false;


public:
    explicit ArtifactStorage(GraphicsArena& arena)noexcept
        : packets(arena)
        , packetRecordingOverlaps(arena)
        , packetTimingTickets(arena)
        , packetStateSeeds(arena)
        , serialRecordingScratch(arena)
        , packetRecordingScratch(arena)
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
    // Records one compiler-derived non-empty contiguous range.
    // Earlier producer packets needed by the range must already be recorded, keeping deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* outCommandIrCapture = nullptr
    )const;
    // Task endpoints resolve after compilation, keeping record spans independent of packet splitting while preserving intentional late tails.
    [[nodiscard]] bool recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* outCommandIrCapture = nullptr
    )const;
    // Records ready frontiers via cpuScheduler; parallel only on unanimous opt-in, else serial. Synchronous.
    [[nodiscard]] bool recordPacketRangeInReadyFrontiers(
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
    // Caller must own artifactAccess and complete prepareRecordingAttempt().
    [[nodiscard]] bool recordPreparedPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
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
    [[nodiscard]] bool recordPacket(
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        GpuCommandIrCapture* commandIrCapture,
        GpuPacketRecordingAbort* deferredAbort,
        Alloc::ScratchArena& stateFanInScratchArena,
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        GpuSubmissionPacketId packetID,
        u64 recordingWorkerDomain = 0u,
        u32 recordingWorkerIndex = 0u
    )const;
    [[nodiscard]] bool prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] bool preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        GpuSubmissionPacketId packetID,
        Alloc::ScratchArena& stateFanInScratchArena,
        const CommandListResourceStateHandoff* initialStates
    )const;


private:
    Device& m_device;
    // Optional; all-None graphs keep the plain recorder path. A timing-aware recorder must outlive its tickets.
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

[[nodiscard]] PacketWaitStatistics CountPacketWaitStatistics(GpuPhysicalQueueId queue, const QueueSubmissionToken* waitTokens, usize waitTokenCount, Alloc::ScratchArena& scratchArena);
[[nodiscard]] u64 AllocateAcceptanceRevision()noexcept;

[[nodiscard]] bool ValidateInitialOwnershipCompletionToken(const GpuTaskGraphInitialOwnerHandoffSourceView& source, const GpuCompiledBarrier& barrier, const GpuPhysicalQueueInfo& sourceQueue, const QueueSubmissionToken& token, u16 deviceGeneration)noexcept;
[[nodiscard]] bool ValidateInitialOwnershipCompletions(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId& packetID)noexcept;

// Reject missing declaration tokens across the entire range before any native packet can be accepted.
[[nodiscard]] bool ValidateExternalDependencyTokens(const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketRange& range)noexcept;
[[nodiscard]] const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(const GpuTaskGraphResourceView& resource, const GpuCompiledBarrier& barrier)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

