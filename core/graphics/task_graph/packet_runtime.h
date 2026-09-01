// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_graph.h"

#include <core/alloc/scratch.h>
#include <core/alloc/thread.h>
#include <core/graphics/rhi/device.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;
class GpuTimingSubmissionTicket;
class GpuTaskGraphSubmitter;
class GpuCommandIrCapture;


struct GpuRecordedPacket{
    static constexpr usize s_MaxCommandLists = 12u;

    GpuSubmissionPacketId packet;
    // Native recording owns its newly-created lists through submission.
    CommandListHandle ownedCommandLists[s_MaxCommandLists] = {};
    CommandList* commandLists[s_MaxCommandLists] = {};
    u8 commandListCount = 0u;
    // These fields are written before commandListCount publishes the slot. They intentionally describe the packet
    // after graph lowering, so compile tooling can distinguish declared work from the native work that was recorded.
    u32 taskCount = 0u;
    u32 barrierCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    // Monotonic steady-clock endpoints make actual CPU recording overlap observable without exposing Timer in the
    // public packet snapshot. Both endpoints are published before commandListCount makes the slot visible.
    u64 recordingBeginNanoseconds = 0u;
    u64 recordingEndNanoseconds = 0u;
    f64 recordingSeconds = 0.0;
    // Worker zero is serial/default recording. Ready-frontier workers retain both their process-unique ThreadPool
    // domain and pool-local index for transactional diagnostics and native arena-affinity smoke coverage.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;
};


// Immutable snapshot assembled from successfully published native packet slots. Recording can be parallel, so
// recordingSeconds is the sum of per-packet steady-clock spans rather than elapsed wall-clock time for the whole
// recording operation. It approximates logical recording-slot occupancy, not operating-system CPU consumption.
// The phase counters isolate native list acquisition, graph-owned barrier lowering, and task callbacks; they do not
// sum to recordingSeconds because graph preparation, markers, close, and lifecycle work intentionally remain there.
struct GpuTaskGraphRecordingStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    usize packetCount = 0u;
    usize taskCount = 0u;
    usize commandListCount = 0u;
    usize barrierCount = 0u;
    // Worker-routed packets used a non-default ready-frontier command-arena shard. A one-packet worker batch is
    // still routed but is not parallel; parallelPacketCount instead requires strict overlap of packet intervals.
    usize workerRoutedPacketCount = 0u;
    usize parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;
    // Sum of successful outer recorder-operation wall spans. Multiple incremental range calls accumulate without
    // including unrelated caller work between them.
    f64 recordingElapsedSeconds = 0.0;
    f64 readyFrontierElapsedSeconds = 0.0;
    // Busy is summed packet-span occupancy across logical recording slots, not operating-system CPU time. Capacity
    // is readyFrontierElapsedSeconds weighted per call by every callable ThreadPool slot, including its caller.
    f64 readyFrontierWorkerBusySeconds = 0.0;
    f64 readyFrontierWorkerCapacitySeconds = 0.0;

    [[nodiscard]] f64 readyFrontierWorkerUtilization()const noexcept{
        return readyFrontierWorkerCapacitySeconds > 0.0
            ? Saturate(readyFrontierWorkerBusySeconds / readyFrontierWorkerCapacitySeconds)
            : 0.0
        ;
    }
    [[nodiscard]] bool valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Immutable-by-value native recording telemetry for one exact physical queue in one compiled graph recording
// attempt. The queue identity includes its device generation, so an auxiliary same-class queue or a recreated
// device cannot alias this result. Counts include only successfully published native packet slots.
struct GpuTaskGraphPhysicalQueueRecordingStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    usize packetCount = 0u;
    usize taskCount = 0u;
    usize commandListCount = 0u;
    usize barrierCount = 0u;
    usize workerRoutedPacketCount = 0u;
    usize parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;

    [[nodiscard]] bool valid()const noexcept{
        return graphGeneration != 0u
            && planGeneration != 0u
            && deviceGeneration != 0u
            && queue.valid()
            && queue.deviceGeneration == deviceGeneration
            && queueClass < CommandQueue::kCount
        ;
    }
};


class GpuRecordedGraph final : NoCopy{
    friend class GpuNativePacketRecorder;
    friend class GpuTaskGraphSubmitter;

private:
    // Every ready-frontier worker receives isolated state-handoff scratch. This is separate from the per-packet
    // final-state slots, which are written only by the packet's own recording worker and read by later frontiers.
    struct PacketRecordingScratch final : NoCopy{
        CommandListResourceStateHandoff initialStateSeed;
        CommandListResourceStateHandoff stateSubsetScratch;
        CommandListResourceStateHandoff stateMergeScratch;
        CommandListResourceStateHandoff externalBaseStateSeed;
        CommandListResourceStateHandoff externalMergedStateSeed;


        explicit PacketRecordingScratch(GraphicsArena& arena)
            : initialStateSeed(arena)
            , stateSubsetScratch(arena)
            , stateMergeScratch(arena)
            , externalBaseStateSeed(arena)
            , externalMergedStateSeed(arena)
        {}
    };


public:
    explicit GpuRecordedGraph(GraphicsArena& arena);
    ~GpuRecordedGraph();


public:
    // Reset and scratch-backed handoff queries are externally serialized. reset() refuses while native submission
    // or cancellation is resolving so it cannot invalidate transaction storage used by a live packet operation.
    void reset(const GpuCompiledGraph& compiledGraph);


private:
    void resetForRecording(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph);


public:
    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept{ return m_recordingAttemptGeneration; }
    // Like reset()/find(), this aggregate inspection is externally serialized with recording and reset. Individual
    // packet slots are published only after their native list and counters are complete.
    [[nodiscard]] GpuTaskGraphRecordingStatistics recordingStatistics(const GpuCompiledGraph& compiledGraph)const noexcept;
    // Aggregates one full physical-queue snapshot from published native packet slots. Invalid/stale queue IDs and
    // a recorded graph from another compiled plan return an empty result. Callers serialize this query with native
    // recording and compiled-graph reset/recompile, matching recordingStatistics(); in particular, query only after
    // recordPacketRangeInReadyFrontiers() has synchronously joined its recording workers.
    [[nodiscard]] GpuTaskGraphPhysicalQueueRecordingStatistics physicalQueueRecordingStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuPhysicalQueueId& queue
    )const noexcept;
    [[nodiscard]] const GpuRecordedPacket* find(const GpuSubmissionPacketId& packet)const noexcept;
    // Validates this recorded graph against the current compiler output and resolves the declared task's containing
    // packet. The result is that packet's actual native final state, not a task-local intermediate snapshot; merged
    // tasks therefore resolve to the same state.
    [[nodiscard]] const CommandListResourceStateHandoff* taskFinalStateSeed(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )const noexcept;


private:
    [[nodiscard]] bool prepareTimingTickets(const GpuCompiledGraph& compiledGraph, GpuTimingRecorder& recorder);
    void discardPacketTimingTicket(const GpuSubmissionPacketId& packet);
    [[nodiscard]] GpuTimingSubmissionTicket* packetTimingTicket(
        const GpuSubmissionPacketId& packet
    )const noexcept;
    [[nodiscard]] bool buildPacketInitialStateSeed(
        PacketRecordingScratch& scratch,
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const CommandListResourceStateHandoff*& outInitialStates
    );
    [[nodiscard]] CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)noexcept;
    [[nodiscard]] const CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] PacketRecordingScratch* packetRecordingScratch(const GpuSubmissionPacketId& packet)noexcept;
    void cachePacketRecordingOverlaps(
        const GpuCompiledGraph& compiledGraph,
        const Vector<u32, Alloc::ScratchArena>& packetIndices,
        Alloc::ScratchArena& scratchArena
    );


private:
    GraphicsArena& m_arena;
    GpuTimingRecorder* m_timingRecorder = nullptr;
    GraphicsVector<GpuRecordedPacket> m_packets;
    // The recorder caller writes these graph-wide flags only after a ready-frontier worker batch joins. Queries
    // then read them without lazy mutation until reset clears the next recording attempt.
    GraphicsVector<u8> m_packetRecordingOverlaps;
    GraphicsVector<GlobalUniquePtr<GpuTimingSubmissionTicket>> m_packetTimingTickets;
    GraphicsVector<CommandListResourceStateHandoff> m_packetStateSeeds;
    PacketRecordingScratch m_serialRecordingScratch;
    GraphicsVector<PacketRecordingScratch> m_packetRecordingScratch;
    f64 m_recordingElapsedSeconds = 0.0;
    f64 m_readyFrontierElapsedSeconds = 0.0;
    f64 m_readyFrontierWorkerBusySeconds = 0.0;
    f64 m_readyFrontierWorkerCapacitySeconds = 0.0;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


class GpuNativePacketRecorder final : NoCopy{
public:
    explicit GpuNativePacketRecorder(Device& device)
        : m_device(device)
    {}
    GpuNativePacketRecorder(Device& device, GpuTimingRecorder& timingRecorder)
        : m_device(device)
        , m_timingRecorder(&timingRecorder)
    {}


public:
    // Records one compiler-derived non-empty contiguous range. Earlier producer packets needed by the range must
    // already be recorded, which keeps deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Semantic companion to the packet-range recorder. Task endpoints resolve only after compilation, keeping
    // renderer record spans independent from packet splitting and merging while preserving intentional late tails.
    [[nodiscard]] bool recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;
    // Records compiler-ready frontiers with `workerPool`. Only packets whose tasks all set
    // GpuTaskSchedulingHint::allowParallelRecording may share a worker frontier; every other packet remains serial.
    // Command-IR capture deliberately keeps the established serial order. The method is synchronous: callers may
    // submit or destroy the recorded graph once it returns.
    [[nodiscard]] bool recordPacketRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph,
        Alloc::ThreadPool& workerPool,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr
    )const;


private:
    [[nodiscard]] bool recordPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        GpuCommandIrCapture* commandIrCapture,
        u64 recordingWorkerDomain = 0u,
        u32 recordingWorkerIndex = 0u,
        GpuTaskGraph::PacketRecordingAbort* deferredAbort = nullptr
    )const;
    [[nodiscard]] bool prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] bool preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const CommandListResourceStateHandoff* initialStates
    )const noexcept;


private:
    Device& m_device;
    // Optional because all-None graphs retain the existing recorder path. A timing-aware recorder must outlive every
    // GpuRecordedGraph ticket created through this instance.
    GpuTimingRecorder* m_timingRecorder = nullptr;
};


struct GpuTaskGraphExternalCompletionToken{
    GpuExternalCompletionId completion;
    // The token must retain the exact physical queue and device-generation identity returned by native submission.
    // Graph waits reject broad CommandQueue-only completions so a stale timeline value cannot alias a recreated
    // device or a future same-class queue.
    QueueSubmissionToken token;

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    // Compatibility bindings are valid only for metadata-only nodes. A graph-owned completion deliberately rejects
    // a second runtime token so one semantic edge can never acquire two competing native timeline identities.
    [[nodiscard]] bool validFallbackFor(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph
    )const noexcept;
};

// One accepted terminal packet that contributes to a graph-to-external resource handoff. Several disjoint texture
// subresource ranges may be published by different packets; a consumer waits on the returned compact token frontier
// before opening the handoff's merged native state source.
struct GpuTaskGraphExternalResourceHandoffProducer{
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// One exact terminal range that contributes to a graph-to-external handoff. A later graph can turn each range into
// an immutable initial-owner source with the corresponding graph-local completion token; direct native consumers
// normally use the compact `waitTokens` frontier instead.
struct GpuTaskGraphExternalResourceHandoffRange{
    GpuTaskResourceRange range;
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    QueueSubmissionToken token;
};

// Acceptance-gated graph-to-external handoff for an imported texture, buffer, or acceleration structure. The
// descriptor fixes the external destination before compilation. `producers` records the semantic terminal packet
// sources, while `waitTokens` is the compact physical-queue frontier required by a direct consumer. For a true
// multi-producer export, `stateSource` contains the exact union of terminal exported ranges; a one-packet export
// preserves its original packet snapshot. The legacy scalar producer fields remain populated only when all terminal
// ranges came from one packet. This value borrows transaction-owned storage and remains valid until that transaction
// resets or the same resource handoff is queried again.
struct GpuTaskGraphExternalResourceHandoff{
    // The handoff borrows recorded/transaction state from one immutable compiled plan.  A same-graph recompile
    // must not let a caller publish its prior packet state or completion tokens through the replacement plan.
    u64 planGeneration = 0u;
    GpuGraphResourceId resource;
    // One-producer compatibility fields. These are invalid for a true multi-producer handoff; use `producers` and
    // `waitTokens` instead.
    GpuTaskId producerTask;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    ResourceStates::Mask finalState = ResourceStates::Unknown;
    QueueSubmissionToken token;
    const GpuTaskGraphExternalResourceHandoffProducer* producers = nullptr;
    usize producerCount = 0u;
    const QueueSubmissionToken* waitTokens = nullptr;
    usize waitTokenCount = 0u;
    // Number of exact terminal resource ranges merged into `stateSource`. This may exceed `producerCount` when
    // several terminal tasks were packet-merged.
    usize terminalRangeCount = 0u;
    const GpuTaskGraphExternalResourceHandoffRange* terminalRanges = nullptr;
    const CommandListResourceStateHandoff* stateSource = nullptr;

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
};


// Binds a timing submission ticket to semantic graph work instead of a compiler-generated packet ID. The submitter
// resolves the task to its current packet after compilation. Semantic anchors sharing a packet may bind the same or
// distinct external tickets; identical ticket aliases in that packet coalesce, while one ticket cannot span native
// packets. The graph may additionally own its automatic packet ticket.
struct GpuTaskGraphTaskTimingTicket{
    GpuTaskId task;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};

// Semantic pre-submit binding for one declared task. The submitter resolves the task after compilation and
// attaches the hook to its exact native packet. Multiple task bindings that resolve to one packet are rejected:
// one native submission has only one unambiguous pre-submit hook.
struct GpuTaskGraphTaskSubmissionHook{
    GpuTaskId task;
    QueueSubmissionPreSubmitHook hook;
};


// Semantic compatibility binding for one declared task. The submitter invokes every matching binding in compiled
// task order even if an earlier callback returned false. The aggregate false result stops later range traversal only
// after the accepted token/frontier publishes. Callbacks run while the transaction submission gate is held and must
// not reenter or synchronously wait for work that needs the same gate.
struct GpuTaskGraphTaskAcceptedCallback{
    GpuTaskId task;
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const QueueSubmissionToken& token
    ) = nullptr;
};

// Runs after a semantic task records and exports its packet state, but before that packet submits. The task anchor
// lets whole-graph execution validate immutable final-state candidates without rebuilding its record/submit sequence
// around compiler packet IDs.
struct GpuTaskGraphTaskRecordedCallback{
    GpuTaskId task;
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const CommandListResourceStateHandoff* finalState
    ) = nullptr;
};


// Describes one graph-owned execution of an ordinary packet prefix in compiler order. An optional terminal task
// includes its complete packet and leaves every later packet declared for caller-owned late-tail policy. Without an
// endpoint, the executor derives every ordinary packet before the terminal accepted-frontier suffix. Semantic timing,
// completion, and callback bindings keep execution independent from compiler packet splitting and merging.
struct GpuTaskGraphNormalExecutionDesc{
    GpuTaskId terminalTask;
    // Invoked in compiler task order after the complete ordinary prefix records and before its first native submit.
    // A false result leaves every packet unaccepted so the caller can discard or recover transactionally.
    const GpuTaskGraphTaskRecordedCallback* taskRecordedCallbacks = nullptr;
    usize taskRecordedCallbackCount = 0u;
    // A null worker pool preserves serial compile-order recording. A supplied pool enables the recorder's
    // per-packet ready-frontier policy; packets without declaration opt-in still record serially.
    Alloc::ThreadPool* readyFrontierWorkerPool = nullptr;
    GpuCommandIrCapture* commandIrCapture = nullptr;
    const GpuTaskGraphExternalCompletionToken* externalCompletionTokens = nullptr;
    usize externalCompletionTokenCount = 0u;
    const GpuTaskGraphTaskTimingTicket* taskTimingTickets = nullptr;
    usize taskTimingTicketCount = 0u;
    const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr;
    usize taskAcceptedCallbackCount = 0u;
    const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr;
    usize taskSubmissionHookCount = 0u;
};


// Transaction-owned native submission telemetry. Wait counts describe graph-provided timeline tokens after applying
// the same physical-queue elision and per-producer merge rules as Device::executeCommandLists(); backend-internal
// waits outside this graph submission are intentionally excluded.
struct GpuTaskGraphSubmissionStatistics{
    static constexpr usize s_QueueClassCount = static_cast<usize>(CommandQueue::kCount);

    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    // Counts packets whose submitter-owned native submission reached Accepted.
    usize acceptedPacketCount = 0u;
    // Includes every declared task in an accepted native packet.
    usize acceptedTaskCount = 0u;
    // Counts each packet when this transaction reaches its terminal Rejected state. Repeated cleanup against an
    // already terminal packet does not contribute another sample.
    usize rejectedPacketCount = 0u;
    // Includes every declared task in a packet when that packet reaches terminal Rejected state.
    usize rejectedTaskCount = 0u;
    usize nativeSubmissionCount = 0u;
    // The narrower native-submit failure subset of rejectedPacketCount. This can occur before the backend sees a
    // native submit (for example while a timing ticket validates), so it is deliberately not labelled as a Vulkan
    // rejection.
    usize rejectedSubmissionCount = 0u;
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    usize acceptedFrontierSubmissionCount = 0u;
    usize recoverySubmissionCount = 0u;
    usize nativeSubmissionCountByQueueClass[s_QueueClassCount] = {};
    usize nativeCommandListCountByQueueClass[s_QueueClassCount] = {};
    usize timelineWaitCountByQueueClass[s_QueueClassCount] = {};
    f64 submissionSeconds = 0.0;

    [[nodiscard]] bool valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Immutable-by-value native submission telemetry for one compiler packet. The query accepts only an exact current
// compiled-plan handle whose packet reached Accepted through Device::executeCommandLists(); every rejected or
// unresolved lifecycle state deliberately returns an invalid value. Wait counters preserve the
// native submitter decomposition: planned tokens equal same-queue elisions plus emitted and merged timeline waits.
struct GpuTaskGraphPacketSubmissionStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    usize taskCount = 0u;
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    f64 submissionSeconds = 0.0;
    bool joinsAcceptedQueueFrontier = false;
    bool isRecoverySubmission = false;

    [[nodiscard]] bool valid()const noexcept{
        return graphGeneration != 0u
            && planGeneration != 0u
            && recordingAttemptGeneration != 0u
            && deviceGeneration != 0u
            && packet.valid()
            && packet.generation == planGeneration
            && queue.valid()
            && queue.deviceGeneration == deviceGeneration
            && queueClass < CommandQueue::kCount
            && taskCount != 0u
        ;
    }
};


// Immutable-by-value native submission telemetry for one exact physical queue in one graph transaction. The queue
// identity includes its device generation, so an auxiliary same-class queue or a recreated device cannot alias this
// result. Every accepted-packet counter represents a submitter-owned native submission. `rejectedSubmissionCount`
// separately reports packets rejected after the transaction reserves the submit path, including a failure before
// the backend execute call.
struct GpuTaskGraphPhysicalQueueSubmissionStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    usize acceptedPacketCount = 0u;
    usize acceptedTaskCount = 0u;
    usize rejectedPacketCount = 0u;
    usize rejectedTaskCount = 0u;
    usize nativeSubmissionCount = 0u;
    usize rejectedSubmissionCount = 0u;
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    usize acceptedFrontierSubmissionCount = 0u;
    usize recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;

    [[nodiscard]] bool valid()const noexcept{
        return graphGeneration != 0u
            && planGeneration != 0u
            && deviceGeneration != 0u
            && queue.valid()
            && queue.deviceGeneration == deviceGeneration
            && queueClass < CommandQueue::kCount
        ;
    }
};


struct GpuGraphSubmissionAcceptanceSnapshot{
    u64 recordingAttemptGeneration = 0u;
    u64 acceptanceRevision = 0u;
};


class GpuGraphSubmissionTransaction final : NoCopy{
    friend class GpuTaskGraphSubmitter;


private:
    enum class PacketRuntimeState : u8{
        Declared,
        Submitting,
        Rejecting,
        Accepted,
        Rejected,
    };

    enum class SubmissionOperationMode : u8{
        OrdinaryPacket,
        ExclusiveBarrier,
        CompositeBarrier,
    };

    struct PacketRuntime{
        PacketRuntimeState state = PacketRuntimeState::Declared;
        QueueSubmissionToken token;
        // Native counters are committed only after the graph lifecycle and its Device submission both accept.
        usize nativeCommandListCount = 0u;
        usize plannedWaitTokenCount = 0u;
        usize sameQueueWaitElisionCount = 0u;
        usize timelineWaitCount = 0u;
        usize mergedTimelineWaitCount = 0u;
        f64 submissionSeconds = 0.0;
        // A post-reservation submit-path failure is terminally rejected, but ordinary discard/rejection never sets
        // this flag. It can occur before the backend execute call, such as while validating a timing ticket.
        bool nativeSubmissionRejected = false;
    };

private:
    class SubmissionOperation final : NoCopy{
    private:
        static thread_local const SubmissionOperation* s_activeOperation;


    public:
        [[nodiscard]] static bool activeFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return s_activeOperation && s_activeOperation->m_transaction == &transaction;
        }
        [[nodiscard]] static bool activeExclusiveFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return activeFor(transaction) && s_activeOperation->m_exclusive;
        }


    public:
        SubmissionOperation(
            const GpuGraphSubmissionTransaction& transaction,
            SubmissionOperationMode mode
        )noexcept;
        ~SubmissionOperation();


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
        const SubmissionOperation* m_previousOperation = nullptr;
        SharedQueuingMutex::scoped_lock m_gateLock;
        bool m_exclusive = false;
        bool m_composite = false;
    };

private:
    struct NativeSubmissionInfo{
        usize commandListCount = 0u;
        usize plannedWaitTokenCount = 0u;
        usize sameQueueWaitElisionCount = 0u;
        usize timelineWaitCount = 0u;
        usize mergedTimelineWaitCount = 0u;
        f64 submissionSeconds = 0.0;
    };

private:
    // Each compiled external release receives stable transaction-owned query storage.  The merged state is rebuilt
    // only when its resource is queried, while the outer vector is fully allocated at reset so a handoff for one
    // resource remains valid when another resource is queried.
    struct ExternalResourceHandoffScratch final : NoCopy{
        GraphicsArena& m_arena;
        GpuGraphResourceId resource;
        CommandListResourceStateHandoff stateSource;
        CommandListResourceStateHandoff stateMerge;
        GraphicsVector<CommandListResourceStateHandoff> stateBranches;
        GraphicsVector<const CommandListResourceStateHandoff*> stateBranchPointers;
        GraphicsVector<GpuTaskGraphExternalResourceHandoffProducer> producers;
        GraphicsVector<GpuTaskGraphExternalResourceHandoffRange> terminalRanges;
        GraphicsVector<QueueSubmissionToken> waitTokens;


        explicit ExternalResourceHandoffScratch(GraphicsArena& arena)
            : m_arena(arena)
            , stateSource(arena)
            , stateMerge(arena)
            , stateBranches(arena)
            , stateBranchPointers(arena)
            , producers(arena)
            , terminalRanges(arena)
            , waitTokens(arena)
        {}

        void reset(){
            stateSource.reset();
            stateMerge.reset();
            stateBranches.clear();
            stateBranchPointers.clear();
            producers.clear();
            terminalRanges.clear();
            waitTokens.clear();
        }
    };


public:
    explicit GpuGraphSubmissionTransaction(GraphicsArena& arena)
        : m_arena(arena)
        , m_packets(arena)
        , m_latestAcceptedQueueTokens(arena)
        , m_externalResourceHandoffScratch(arena)
        , m_transactionIdentity(GpuTaskGraph::allocateGeneration())
    {}
    ~GpuGraphSubmissionTransaction();


public:
    void reset(const GpuCompiledGraph& compiledGraph);
    // Returns false without changing packet state, tokens, frontier, or statistics while this logical transaction
    // still owns any nonterminal packet in a graph recording attempt.
    [[nodiscard]] bool tryReset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    // Semantic task rejection resolves the current packet only inside the transaction, so renderer recovery code
    // can revoke unaccepted graph work without mirroring compiler packet identities.
    void rejectTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        u64 recordingAttemptGeneration
    )noexcept;
    // Returns false when a packet is actively recording/submitting or the transaction no longer owns this attempt.
    // Callers must retain the graph until a true result confirms that every unaccepted packet was resolved.
    [[nodiscard]] bool discardUnaccepted(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )noexcept;

    [[nodiscard]] bool hasAcceptedPackets()const noexcept;
    [[nodiscard]] GpuTaskGraphSubmissionStatistics submissionStatistics()const noexcept;
    // Copies one accepted native packet's exact wait decomposition and compiler role while holding the transaction
    // mutex. The result owns every field, but resolving its packet and queue borrows immutable compiled-plan storage;
    // callers must externally serialize the query with compiled-graph reset/recompile.
    [[nodiscard]] GpuTaskGraphPacketSubmissionStatistics packetSubmissionStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet
    )const noexcept;
    // Aggregates one full physical-queue snapshot while holding the transaction mutex. Invalid/stale queue IDs and
    // a transaction from another compiled plan return an empty result instead of borrowing packet-runtime storage.
    // The result owns every transaction field, but aggregation borrows compiled-graph plan storage. Callers must
    // externally serialize this query with compiled-graph reset/recompile, matching recorded-graph statistics.
    [[nodiscard]] GpuTaskGraphPhysicalQueueSubmissionStatistics physicalQueueSubmissionStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuPhysicalQueueId& queue
    )const noexcept;
    // Copies one packet-indexed acceptance snapshot while holding the transaction mutex once. The caller supplies
    // exactly compiledGraph.packetCount() entries; rejected and unresolved packets are represented by invalid
    // tokens. The accompanying recording attempt and process-unique acceptance revision are copied under that same
    // lock. Invalid, stale, mismatched-plan, and wrong-sized requests leave both caller outputs untouched.
    // Callers must externally serialize this snapshot with compiled-graph reset and recompilation.
    [[nodiscard]] bool copyAcceptedPacketTokens(
        const GpuCompiledGraph& compiledGraph,
        QueueSubmissionToken* outTokens,
        usize tokenCount,
        GpuGraphSubmissionAcceptanceSnapshot& outSnapshot
    )const noexcept;
    [[nodiscard]] QueueSubmissionToken packetToken(const GpuSubmissionPacketId& packet)const noexcept;
    // Resolves the current compiler packet for semantic graph work before returning its accepted submission token.
    // This is generation-checked so renderer lifecycle code cannot treat a task from an older compiled graph as
    // an accepted submission on a replacement device or packetization.
    [[nodiscard]] QueueSubmissionToken taskToken(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )const noexcept;
    // Publishes the exact external final-state/ownership handoff only after every compiler-selected terminal
    // producer packet accepted. Multi-producer exports retain all semantic producers, compact their waits to one
    // latest token per physical queue, and merge the exact terminal state ranges without exposing packet IDs. The
    // returned storage is transaction scratch; callers must serialize handoff queries with reset and later queries.
    [[nodiscard]] GpuTaskGraphExternalResourceHandoff externalResourceHandoff(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        GpuGraphResourceId resource
    )const noexcept;
    // Source-compatible one-producer query. A true multi-producer texture export requires the graph-aware overload
    // above so it can filter and merge the exact terminal ranges; this legacy form deliberately returns invalid for
    // that case instead of publishing a partial state snapshot. Its return storage has the same serialized-query
    // lifetime as the graph-aware overload.
    [[nodiscard]] GpuTaskGraphExternalResourceHandoff externalResourceHandoff(
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        GpuGraphResourceId resource
    )const noexcept;


private:
    // Appends one latest accepted token for every physical queue other than `destinationQueue`. A recovery packet
    // submitted on that destination does not need to wait on its own queue because queue order already supplies the
    // dependency; every other physical producer remains an explicit timeline wait.
    [[nodiscard]] bool appendAcceptedQueueFrontierWaitTokens(
        const GpuPhysicalQueueId& destinationQueue,
        Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens
    )const;

    [[nodiscard]] bool validForLocked(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] bool waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()const noexcept;
    [[nodiscard]] QueueSubmissionToken packetTokenLocked(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] QueueSubmissionToken taskTokenLocked(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )const noexcept;


private:
    [[nodiscard]] bool allPacketsTerminalLocked()const noexcept;
    [[nodiscard]] bool bindRecordingAttemptWithinSubmissionOperation(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )noexcept;
    [[nodiscard]] bool matchesRecordingAttemptBinding(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;
    void resolveSubmissionBindingIfTerminalLocked(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph
    )noexcept;
    void rejectTaskWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        u64 recordingAttemptGeneration
    )noexcept;

    // Reserves native submission before Device::executeCommandLists() begins. While a packet is Submitting,
    // transaction cancellation cannot run its discarded callback or claim the graph for a retry.
    [[nodiscard]] bool beginPacketSubmission(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        GpuTaskGraph::PacketSubmissionLease& outLease
    )noexcept;
    [[nodiscard]] bool acceptSubmittingPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        GpuTaskGraph::PacketSubmissionLease& lease,
        const NativeSubmissionInfo& nativeSubmissionInfo,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u
    )noexcept;
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        u64 recordingAttemptGeneration
    )noexcept;
    void rejectSubmittingPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        GpuTaskGraph::PacketSubmissionLease& lease
    )noexcept;
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };


private:
    GraphicsArena& m_arena;
    GraphicsVector<PacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    mutable GraphicsVector<ExternalResourceHandoffScratch> m_externalResourceHandoffScratch;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    const u64 m_transactionIdentity = 0u;
    u64 m_resetGeneration = 0u;
    GpuGraphSubmissionBinding m_activeSubmissionBinding;
    bool m_submissionBindingResolved = false;
    u16 m_deviceGeneration = 0u;
    u64 m_acceptedSubmissionCount = 0u;
    u64 m_acceptanceRevision = 0u;
    GpuTaskGraphSubmissionStatistics m_submissionStatistics;
    bool m_valid = false;
    // Ready-frontier workers cannot inherit the caller's thread-local operation chain. A composite writer therefore
    // closes public operation admission across threads before it invokes or waits for arbitrary record callbacks.
    mutable AtomicFlag m_compositeOperationActive;
    // Ordinary packets may reach independent native queues concurrently. Frontier joins and lifecycle mutations use
    // the fair writer side so they observe one complete publication boundary without starving behind new readers.
    mutable SharedQueuingMutex m_submissionGate;
    // Native submission returns before timing, graph payload, compatibility callback, and token/frontier resolution.
    // Keep that indivisible publication tail serialized while native queue work remains free to overlap.
    mutable Futex m_resolutionMutex;
    mutable Futex m_mutex;
};


struct GpuTaskGraphRuntimeStatistics{
    GpuTaskGraphCompileStatistics compile;
    GpuTaskGraphRecordingStatistics recording;
    GpuTaskGraphSubmissionStatistics submission;

    [[nodiscard]] bool valid()const noexcept{
        return compile.valid()
            && recording.valid()
            && submission.valid()
            && compile.graphGeneration == recording.graphGeneration
            && compile.graphGeneration == submission.graphGeneration
            && compile.planGeneration == recording.planGeneration
            && compile.planGeneration == submission.planGeneration
            && compile.deviceGeneration == recording.deviceGeneration
            && compile.deviceGeneration == submission.deviceGeneration
            && recording.recordingAttemptGeneration != 0u
            && recording.recordingAttemptGeneration == submission.recordingAttemptGeneration
        ;
    }
};


// The returned values are immutable copies, but callers serialize this helper with recorded-graph recording/reset,
// exactly like GpuRecordedGraph::recordingStatistics(). Transaction submission data itself is mutex-protected.
[[nodiscard]] GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept;


class GpuTaskGraphSubmitter final : NoCopy{
private:
    enum class PacketRangeSubmissionOperationPolicy : u8{
        PerPacket,
        ActiveExclusiveBarrier,
    };


public:
    explicit GpuTaskGraphSubmitter(Device& device)
        : m_device(device)
    {}


public:
    // Submits one compiler-derived non-empty contiguous range. Dependencies outside the range must already be
    // accepted in the transaction; this preserves graph-owned waits while allowing intentional late tails. Every
    // accepted callback completes synchronously before that packet's token/frontier becomes observable. A callback
    // false result stops later packets after publishing the accepted packet for recovery or finalization.
    [[nodiscard]] bool submitPacketRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketRange& range,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphTaskTimingTicket* taskTimingTickets,
        usize taskTimingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr,
        usize taskSubmissionHookCount = 0u
    )const;
    // Semantic companion to packet-range submission. It resolves the inclusive compiler-order range and all
    // optional timing, accepted-callback, and pre-submit bindings from declared tasks after compilation.
    [[nodiscard]] bool submitTaskRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphTaskTimingTicket* taskTimingTickets,
        usize taskTimingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr,
        usize taskSubmissionHookCount = 0u
    )const;
    // Records then submits the descriptor-selected ordinary compiler prefix. Without a semantic terminal task,
    // accepted-frontier packets must form one terminal suffix. The executor rejects a frontier inside its selected
    // prefix before recording and never discards or submits later caller-owned work on the caller's behalf.
    [[nodiscard]] bool recordAndSubmitNormalGraph(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        const GpuTaskGraphNormalExecutionDesc& desc,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Records then submits the inclusive compiler-order range resolved from declared task endpoints. Recovery and
    // finalization packets that join the accepted queue frontier are deliberately rejected: callers retain explicit
    // ownership of their late tail, cleanup, and recovery policy. This helper does not discard remaining work.
    [[nodiscard]] bool recordAndSubmitTaskRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Ready-frontier variant of semantic task-range execution. It preserves the serial helper's recovery-tail
    // preflight and submission order, but gives explicitly opted-in packets isolated worker recording leases.
    // Packets without opt-in retain the recorder's serial fallback. Callers retain all cleanup and recovery policy.
    [[nodiscard]] bool recordAndSubmitTaskRangeInReadyFrontiers(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        Alloc::ThreadPool& workerPool,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Records and submits one semantic late-recovery/finalization task whose compiled packet joins the accepted
    // physical-queue frontier. The transaction supplies the exact current queue waits during submission; callers
    // never assemble a renderer-local frontier token list or compiler packet range. Record or submit failure
    // rejects the still-unaccepted task before returning false.
    [[nodiscard]] bool recordAndSubmitAcceptedFrontierTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Records and submits one semantic late task after its graph dependencies have accepted. A recorded callback may
    // validate the packet's immutable final-state seed before submission, and an accepted callback may publish
    // semantic state synchronously from the native submission token. Any rejection leaves task lifecycle owned by
    // the transaction rather than a renderer-local packet/retry path.
    [[nodiscard]] bool recordAndSubmitTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback = nullptr
    )const;


private:
    // The caller owns the transaction's exclusive SubmissionOperation. This internal path lets the accepted-frontier
    // composite reuse the ordinary task executor without attempting forbidden same-transaction gate reentry.
    [[nodiscard]] bool recordAndSubmitTaskWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback
    )const;
    // Arms one graph recording attempt, publishes that exact attempt into the recorded artifact for failure
    // cleanup, then binds its sole submission transaction while the caller retains the exclusive operation.
    [[nodiscard]] bool prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    // Standalone ranges retain per-packet reader/writer concurrency. Composite ranges explicitly borrow their one
    // outer writer so recorded callbacks cannot be overtaken before their submission decision is published.
    [[nodiscard]] bool submitPacketRangeInCompileOrderWithOperationPolicy(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketRange& range,
        PacketRangeSubmissionOperationPolicy operationPolicy,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphTaskTimingTicket* taskTimingTickets,
        usize taskTimingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks,
        usize taskAcceptedCallbackCount,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks,
        usize taskSubmissionHookCount
    )const;
    // The caller owns one valid SubmissionOperation for the full native-accept, task-callback, and
    // transaction-publication sequence. Range submission supplies its synchronous semantic obligations here; the
    // native packet primitive is never exposed as a public entry point.
    [[nodiscard]] bool submitPacketWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketId& packet,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuTimingSubmissionTicket* const* timingTickets,
        usize timingTicketCount,
        const QueueSubmissionPreSubmitHook* preSubmitHook,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks,
        usize taskAcceptedCallbackCount
    )const;


private:
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

