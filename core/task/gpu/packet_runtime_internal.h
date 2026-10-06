// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuNativePacketRecorder final : NoCopy{
    friend class GpuTaskScheduler;

private:
    class PacketRecordingExceptionScope;
    class PacketArtifactPublicationScope;
    class ReadyFrontierRecordingUnwindScope;


private:
    explicit GpuNativePacketRecorder(Device& device)noexcept
        : m_device(device)
    {}
    GpuNativePacketRecorder(Device& device, GpuTimingRecorder& timingRecorder)noexcept
        : m_device(device)
        , m_timingRecorder(&timingRecorder)
    {}


private:
    // Records one compiler-derived non-empty contiguous range. Earlier producer packets needed by the range must already be recorded,
    // keeps deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Semantic companion to the packet-range recorder. Task endpoints resolve only after compilation, keeping renderer record spans independent from packet splitting and merging while preserving intentional late tails.
    [[nodiscard]] bool recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Records ready frontiers via cpuScheduler; parallel only when every task opts in, else serial (command-IR keeps serial order). Synchronous: submittable on return.
    [[nodiscard]] bool recordPacketRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        CpuTaskScheduler& cpuScheduler,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;


private:
    // Caller must own artifactAccess and complete prepareRecordingAttempt().
    [[nodiscard]] bool recordPreparedPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuCommandIrCapture* commandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    [[nodiscard]] bool recordPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        GpuSubmissionPacketId packet,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        GpuCommandIrCapture* commandIrCapture,
        u64 recordingWorkerDomain = 0u,
        u32 recordingWorkerIndex = 0u,
        GpuTaskGraph::PacketRecordingAbort* deferredAbort = nullptr
    )const;
    [[nodiscard]] bool prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess
    )const;
    [[nodiscard]] bool preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuTaskGraph::PacketRecordingAccess& recordingAccess,
        GpuSubmissionPacketId packet,
        Alloc::ScratchArena& stateFanInScratchArena,
        const CommandListResourceStateHandoff* initialStates
    )const;


private:
    Device& m_device;
    // Optional because all-None graphs retain the existing recorder path. A timing-aware recorder must outlive every GpuRecordedGraph ticket created through this instance.
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

[[nodiscard]] PacketWaitStatistics CountPacketWaitStatistics(
    GpuPhysicalQueueId queue,
    const QueueSubmissionToken* waitTokens,
    usize waitTokenCount,
    Alloc::ScratchArena& scratchArena
);
[[nodiscard]] u64 AllocateAcceptanceRevision()noexcept;

[[nodiscard]] bool ValidateInitialOwnershipCompletionToken(
    const GpuTaskGraphInitialOwnerHandoffSourceView& source,
    const GpuCompiledBarrier& barrier,
    const GpuPhysicalQueueInfo& sourceQueue,
    const QueueSubmissionToken& token,
    u16 deviceGeneration
)noexcept;
[[nodiscard]] bool ValidateInitialOwnershipCompletions(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketId& packetID
);

// Reject missing declaration tokens across the entire range before any native packet can be accepted.
[[nodiscard]] bool ValidateExternalDependencyTokens(
    const GpuTaskGraph::DeclarationReadView& declarationAccess,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuSubmissionPacketRange& range
)noexcept;
[[nodiscard]] const GpuTaskGraphInitialOwnerHandoffSourceView* FindInitialOwnerHandoffSource(
    const GpuTaskGraphResourceView& resource,
    const GpuCompiledBarrier& barrier
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

