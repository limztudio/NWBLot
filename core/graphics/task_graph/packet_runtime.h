// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"

#include <core/alloc/scratch.h>
#include <core/alloc/thread.h>
#include <core/graphics/rhi/device.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket;
class GpuTaskGraph;
class GpuTaskGraphSubmitter;
class GpuTaskPacketSubmissionLease;
class GpuCommandIrCapture;
struct GpuExternalPacketStateSource;
struct GpuTaskPacketStateBinding;


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
    f64 recordingSeconds = 0.0;
    // Worker zero is serial/default recording. Ready-frontier workers retain both their process-unique ThreadPool
    // domain and pool-local index for transactional diagnostics and native arena-affinity smoke coverage.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;
};


// Immutable snapshot assembled from successfully published native packet slots. Recording can be parallel, so
// recordingSeconds is the sum of per-packet CPU work rather than elapsed wall-clock time for the whole frontier.
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
    usize parallelPacketCount = 0u;
    f64 commandListAcquisitionSeconds = 0.0;
    f64 graphBarrierRecordingSeconds = 0.0;
    f64 taskRecordSeconds = 0.0;
    f64 recordingSeconds = 0.0;

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

private:
    // Every ready-frontier worker receives isolated state-handoff scratch. This is separate from the per-packet
    // final-state slots, which are written only by the packet's own recording worker and read by later frontiers.
    struct PacketRecordingScratch final : NoCopy{
        explicit PacketRecordingScratch(GraphicsArena& arena)
            : initialStateSeed(arena)
            , stateSubsetScratch(arena)
            , stateMergeScratch(arena)
            , externalBaseStateSeed(arena)
            , externalMergedStateSeed(arena)
        {}

        CommandListResourceStateHandoff initialStateSeed;
        CommandListResourceStateHandoff stateSubsetScratch;
        CommandListResourceStateHandoff stateMergeScratch;
        CommandListResourceStateHandoff externalBaseStateSeed;
        CommandListResourceStateHandoff externalMergedStateSeed;
    };


public:
    explicit GpuRecordedGraph(GraphicsArena& arena)
        : m_arena(arena)
        , m_packets(arena)
        , m_packetStateSeeds(arena)
        , m_serialRecordingScratch(arena)
        , m_packetRecordingScratch(arena)
    {}


public:
    // Reset and scratch-backed handoff queries are externally serialized. reset() refuses while native submission
    // or cancellation is resolving so it cannot invalidate transaction storage used by a live packet operation.
    void reset(const GpuCompiledGraph& compiledGraph);
    void resetForRecording(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph);

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
    // Read-only export for a later graph or cross-frame state cache that needs this packet's actual native final
    // state. Graph-internal consumers use compiler-produced state seeds instead.
    [[nodiscard]] const CommandListResourceStateHandoff* packetFinalStateSeed(
        const GpuSubmissionPacketId& packet
    )const noexcept;
    // Semantic companion to packetFinalStateSeed. It validates this recorded graph against the current compiler
    // output before resolving the declared task's containing packet, so lifecycle consumers do not mirror packet
    // IDs merely to retain a graph-recorded native final state.
    [[nodiscard]] const CommandListResourceStateHandoff* taskFinalStateSeed(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )const noexcept;


private:
    [[nodiscard]] bool buildPacketInitialStateSeed(
        PacketRecordingScratch& scratch,
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const GpuExternalPacketStateSource* externalStateSources,
        usize externalStateSourceCount,
        const GpuTaskPacketStateBinding* taskStateBindings,
        usize taskStateBindingCount,
        const CommandListResourceStateHandoff*& outInitialStates
    );
    [[nodiscard]] CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)noexcept;
    [[nodiscard]] const CommandListResourceStateHandoff* packetStateSeed(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] PacketRecordingScratch* packetRecordingScratch(const GpuSubmissionPacketId& packet)noexcept;


private:
    GraphicsArena& m_arena;
    GraphicsVector<GpuRecordedPacket> m_packets;
    GraphicsVector<CommandListResourceStateHandoff> m_packetStateSeeds;
    PacketRecordingScratch m_serialRecordingScratch;
    GraphicsVector<PacketRecordingScratch> m_packetRecordingScratch;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u16 m_deviceGeneration = 0u;
    bool m_valid = false;
};


// An external producer's native final state. Packet recording filters every source through the consuming task's
// declared imported resources, including declared texture ranges, so renderer subsystems never hand-build state
// subsets or fan-ins for graph-owned packet boundaries.
struct GpuExternalPacketStateSource{
    const CommandListResourceStateHandoff* states = nullptr;
};

// A late external-state source anchored to semantic graph work rather than a compiler-generated packet ID.  The
// recorder resolves `task` to its current packet and filters every source through that packet's declared resource
// uses.  This preserves the legacy packet-wide handoff behavior across packetization changes while retiring
// renderer-owned packet selection for sources that are only available after an earlier packet records.
struct GpuTaskPacketStateBinding{
    GpuTaskId task;
    const GpuExternalPacketStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
};


// Native graph packets always receive compiler-produced internal state seeds, declaration-selected external state
// seeds, and compiler-planned packet-boundary barriers. Task thunks retain only intra-task synchronization.
struct GpuNativePacketRecordDesc{
    GpuSubmissionPacketId packet;
    const GpuExternalPacketStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
    // Internal ready-frontier lease selector. Direct callers leave both fields at zero; the recorder derives a
    // process-unique ThreadPool domain and nonzero local index without exposing backend-native pool handles.
    u64 recordingWorkerDomain = 0u;
    u32 recordingWorkerIndex = 0u;
};


class GpuNativePacketRecorder final : NoCopy{
public:
    explicit GpuNativePacketRecorder(Device& device)
        : m_device(device)
    {}


public:
    [[nodiscard]] bool recordPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecordDesc& desc,
        GpuRecordedGraph& outRecordedGraph,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        const GpuTaskPacketStateBinding* taskStateBindings = nullptr,
        usize taskStateBindingCount = 0u
    )const;
    // Records one compiler-derived non-empty contiguous range. `recordOverrides` is optional and sparse: packets
    // without an override receive the ordinary graph-owned state seed. Earlier producer packets needed by the range
    // must already be recorded, which keeps deliberate late tails separate from the ordinary graph prefix.
    [[nodiscard]] bool recordPacketRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        const GpuNativePacketRecordDesc* recordOverrides,
        usize recordOverrideCount,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        const GpuTaskPacketStateBinding* taskStateBindings = nullptr,
        usize taskStateBindingCount = 0u
    )const;
    // Semantic companion to the packet-range recorder. Task endpoints resolve only after compilation, keeping
    // renderer record spans independent from packet splitting and merging while preserving intentional late tails.
    [[nodiscard]] bool recordTaskRangeInCompileOrder(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        const GpuNativePacketRecordDesc* recordOverrides,
        usize recordOverrideCount,
        GpuRecordedGraph& outRecordedGraph,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        const GpuTaskPacketStateBinding* taskStateBindings = nullptr,
        usize taskStateBindingCount = 0u
    )const;
    // Records compiler-ready frontiers with `workerPool`. Only packets whose tasks all set
    // GpuTaskSchedulingHint::allowParallelRecording may share a worker frontier; every other packet remains serial.
    // Command-IR capture deliberately keeps the established serial order. The method is synchronous: callers may
    // submit or destroy the recorded graph once it returns.
    [[nodiscard]] bool recordPacketRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        const GpuNativePacketRecordDesc* recordOverrides,
        usize recordOverrideCount,
        GpuRecordedGraph& outRecordedGraph,
        Alloc::ThreadPool& workerPool,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        const GpuTaskPacketStateBinding* taskStateBindings = nullptr,
        usize taskStateBindingCount = 0u
    )const;
    // Semantic companion to ready-frontier recording. Worker eligibility and packet ordering remain compiler-owned.
    [[nodiscard]] bool recordTaskRangeInReadyFrontiers(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        const GpuNativePacketRecordDesc* recordOverrides,
        usize recordOverrideCount,
        GpuRecordedGraph& outRecordedGraph,
        Alloc::ThreadPool& workerPool,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        GpuCommandIrCapture* commandIrCapture = nullptr,
        const GpuTaskPacketStateBinding* taskStateBindings = nullptr,
        usize taskStateBindingCount = 0u
    )const;
private:
    [[nodiscard]] bool preflightPacketResources(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const CommandListResourceStateHandoff* initialStates
    )const noexcept;
    [[nodiscard]] bool prepareRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& outRecordedGraph
    )const;
    [[nodiscard]] bool recordPacketWithScratch(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecordDesc& desc,
        GpuRecordedGraph& outRecordedGraph,
        GpuRecordedGraph::PacketRecordingScratch& scratch,
        GpuCommandIrCapture* commandIrCapture,
        const GpuTaskPacketStateBinding* taskStateBindings,
        usize taskStateBindingCount
    )const;
    Device& m_device;
};


namespace GpuPacketRuntimeState{
    enum Enum : u8{
        Declared,
        Recorded,
        Submitting,
        Rejecting,
        Accepted,
        Rejected,
    };
};

struct GpuPacketRuntime{
    GpuPacketRuntimeState::Enum state = GpuPacketRuntimeState::Declared;
    QueueSubmissionToken token;
    // Native counters are committed only after the graph lifecycle accepts the submission. Direct diagnostic packet
    // acceptance remains visible through `state`/`token`, while these fields intentionally stay empty because no
    // Device::executeCommandLists() call occurred.
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    f64 submissionSeconds = 0.0;
    bool hasNativeSubmission = false;
    // A post-reservation submit-path failure is terminally rejected, but ordinary discard/rejection never sets this
    // flag. It can occur before the backend execute call, such as while validating a timing ticket.
    bool nativeSubmissionRejected = false;
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


// Associates one compiler-generated packet with the timing ticket that must submit its native command list.  A
// packet omitted from a compile-order submission uses the ordinary non-timing transport.
struct GpuTaskGraphPacketTimingTicket{
    GpuSubmissionPacketId packet;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};

// Binds a timing submission ticket to semantic graph work instead of a compiler-generated packet ID.  The
// submitter resolves the task to its current packet after compilation and rejects a range that would assign
// conflicting tickets to the same packet.  This is the migration path for renderer code that should not mirror
// packetization merely to route its timing transaction.
struct GpuTaskGraphTaskTimingTicket{
    GpuTaskId task;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};

// Associates one packet with a native pre-submit hook. The graph still owns packet routing and all timeline waits;
// this narrow escape hatch is for one-shot native signals such as the swap-chain binary semaphore that must be
// emitted by the terminal presentation packet itself.
struct GpuTaskGraphPacketSubmissionHook{
    GpuSubmissionPacketId packet;
    QueueSubmissionPreSubmitHook hook;
};

// Semantic pre-submit binding for one declared task. The submitter resolves the task after compilation and
// attaches the hook to its exact native packet. Multiple task bindings that resolve to one packet are rejected:
// one native submission has only one unambiguous pre-submit hook.
struct GpuTaskGraphTaskSubmissionHook{
    GpuTaskId task;
    QueueSubmissionPreSubmitHook hook;
};


// Synchronous compatibility obligation run after the graph's typed accepted hooks and Accepted lifecycle, but
// before the transaction publishes this packet's token or accepted frontier. Returning false stops later range
// traversal without hiding the native-accepted packet. The callback runs while the transaction's submission gate
// is held, but without its state mutex or the graph lifecycle mutex. It must not reenter or synchronously wait for
// work that needs the same transaction gate; same-transaction nested submission fails immediately.
struct GpuTaskGraphPacketAcceptedCallback{
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    ) = nullptr;
};

// Semantic compatibility binding for one declared task. After the packet-wide compatibility callback, the submitter
// invokes every matching binding in compiled task order even if an earlier callback returned false. The aggregate
// false result stops later range traversal only after the accepted token/frontier publishes. These callbacks share
// the same synchronous submission-gate and deadlock restrictions as GpuTaskGraphPacketAcceptedCallback.
struct GpuTaskGraphTaskAcceptedCallback{
    GpuTaskId task;
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const QueueSubmissionToken& token
    ) = nullptr;
};

// Runs after one semantic late task records and exports its packet state, but before that packet submits. A false
// result rejects the still-unaccepted task. This lets a caller validate an immutable final-state candidate without
// rebuilding the record/submit sequence around compiler packet IDs.
struct GpuTaskGraphTaskRecordedCallback{
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const CommandListResourceStateHandoff* finalState
    ) = nullptr;
};


// Describes one graph-owned execution of every ordinary packet in compiler order. The executor derives the normal
// prefix itself and leaves any terminal accepted-frontier suffix declared for the caller's explicit recovery or
// finalization policy. Semantic timing, completion, and callback bindings keep that ordinary execution independent
// from compiler packet splitting and merging.
struct GpuTaskGraphNormalExecutionDesc{
    const GpuNativePacketRecordDesc* recordOverrides = nullptr;
    usize recordOverrideCount = 0u;
    const GpuTaskPacketStateBinding* taskStateBindings = nullptr;
    usize taskStateBindingCount = 0u;
    // A null worker pool preserves serial compile-order recording. A supplied pool enables the recorder's
    // per-packet ready-frontier policy; packets without declaration opt-in still record serially.
    Alloc::ThreadPool* readyFrontierWorkerPool = nullptr;
    GpuCommandIrCapture* commandIrCapture = nullptr;
    const GpuTaskGraphExternalCompletionToken* externalCompletionTokens = nullptr;
    usize externalCompletionTokenCount = 0u;
    const GpuTaskGraphTaskTimingTicket* taskTimingTickets = nullptr;
    usize taskTimingTicketCount = 0u;
    const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr;
    const GpuTaskGraphPacketSubmissionHook* submissionHooks = nullptr;
    usize submissionHookCount = 0u;
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
    // Includes accepted diagnostic/manual packet completion in addition to submitter-owned native submissions.
    usize acceptedPacketCount = 0u;
    // Includes every task in an accepted packet, including the manual diagnostic acceptance seam above.
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
    usize nativeSubmissionCountByQueueClass[s_QueueClassCount] = {};
    usize nativeCommandListCountByQueueClass[s_QueueClassCount] = {};
    usize timelineWaitCountByQueueClass[s_QueueClassCount] = {};
    f64 submissionSeconds = 0.0;

    [[nodiscard]] bool valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Immutable-by-value native submission telemetry for one exact physical queue in one graph transaction. The queue
// identity includes its device generation, so an auxiliary same-class queue or a recreated device cannot alias this
// result. Native-success counters intentionally exclude manual diagnostic packet acceptance. `rejectedSubmissionCount`
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


class GpuGraphSubmissionTransaction final : NoCopy{
    friend class GpuTaskGraphSubmitter;


private:
    class SubmissionOperation final : NoCopy{
    private:
        static thread_local const SubmissionOperation* s_activeOperation;


    public:
        explicit SubmissionOperation(const GpuGraphSubmissionTransaction& transaction)noexcept;
        ~SubmissionOperation();


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
        const SubmissionOperation* m_previousOperation = nullptr;
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
    {}


public:
    void reset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
    // Test/diagnostic seam for manually completed recording. The explicit graph and attempt prevent an arbitrary
    // generation from binding this transaction to stale graph work.
    [[nodiscard]] bool markPacketRecorded(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        u64 recordingAttemptGeneration
    )noexcept;
    [[nodiscard]] bool acceptPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    )noexcept;
    // Convenience only after the transaction has been bound by an explicit attempt. An unbound cleanup must not
    // infer the graph's current attempt, because a stale transaction could otherwise discard a later retry.
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet
    )noexcept;
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        u64 recordingAttemptGeneration
    )noexcept;
    // Semantic companion to rejectPacket(). It resolves the current packet only inside the transaction, so
    // renderer recovery code can revoke unaccepted graph work without mirroring compiler packet identities.
    void rejectTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )noexcept;
    void rejectTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        u64 recordingAttemptGeneration
    )noexcept;
    // Returns false when a packet is actively recording/submitting or the transaction no longer owns this attempt.
    // Callers must retain the graph until a true result confirms that every unaccepted packet was resolved.
    [[nodiscard]] bool discardUnaccepted(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept;
    [[nodiscard]] bool discardUnaccepted(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )noexcept;

    [[nodiscard]] bool hasAcceptedPackets()const noexcept;
#if !defined(NWB_FINAL)
    [[nodiscard]] u32 submissionWaiterCountForTesting()const noexcept{
        return m_submissionWaiterCount.load(MemoryOrder::acquire);
    }
#endif
    [[nodiscard]] GpuTaskGraphSubmissionStatistics submissionStatistics()const noexcept;
    // Aggregates one full physical-queue snapshot while holding the transaction mutex. Invalid/stale queue IDs and
    // a transaction from another compiled plan return an empty result instead of borrowing packet-runtime storage.
    // The result owns every transaction field, but aggregation borrows compiled-graph plan storage. Callers must
    // externally serialize this query with compiled-graph reset/recompile, matching recorded-graph statistics.
    [[nodiscard]] GpuTaskGraphPhysicalQueueSubmissionStatistics physicalQueueSubmissionStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuPhysicalQueueId& queue
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
    [[nodiscard]] const QueueSubmissionToken* latestAcceptedToken(const GpuPhysicalQueueId& queue)const noexcept;
    // Appends one latest accepted token for every physical queue other than `destinationQueue`. A recovery packet
    // submitted on that destination does not need to wait on its own queue because queue order already supplies the
    // dependency; every other physical producer remains an explicit timeline wait.
    [[nodiscard]] bool appendAcceptedQueueFrontierWaitTokens(
        const GpuPhysicalQueueId& destinationQueue,
        Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens
    )const;
    [[nodiscard]] const GpuPacketRuntime* packetRuntime(const GpuSubmissionPacketId& packet)const noexcept;


private:
    [[nodiscard]] bool validForLocked(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] bool waitForSubmissionPublicationAndHasAcceptedPackets()const noexcept;


private:
    // Reserves native submission before Device::executeCommandLists() begins. While a packet is Submitting,
    // transaction cancellation cannot run its discarded callback or claim the graph for a retry.
    [[nodiscard]] bool beginPacketSubmission(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        GpuTaskPacketSubmissionLease& outLease
    )noexcept;
    [[nodiscard]] bool acceptSubmittingPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        GpuTaskPacketSubmissionLease& lease,
        const NativeSubmissionInfo* nativeSubmissionInfo = nullptr,
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u
    )noexcept;
    void rejectSubmittingPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        GpuTaskPacketSubmissionLease& lease
    )noexcept;
    [[nodiscard]] bool bindRecordingAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )noexcept;
    [[nodiscard]] bool matchesRecordedAttempt(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph
    )const noexcept;
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };


private:
    GraphicsArena& m_arena;
    GraphicsVector<GpuPacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    mutable GraphicsVector<ExternalResourceHandoffScratch> m_externalResourceHandoffScratch;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    u16 m_deviceGeneration = 0u;
    u64 m_acceptedSubmissionCount = 0u;
    GpuTaskGraphSubmissionStatistics m_submissionStatistics;
    bool m_valid = false;
#if !defined(NWB_FINAL)
    mutable Atomic<u32> m_submissionWaiterCount{ 0u };
#endif
    mutable Futex m_submissionMutex;
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
    // The caller owns one valid SubmissionOperation for the full native-accept, compatibility-callback, and
    // transaction-publication sequence. Range submission supplies its synchronous compatibility obligations here;
    // the public single-packet API deliberately supplies none.
    [[nodiscard]] bool submitPacketWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketId& packet,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuTimingSubmissionTicket* timingTicket,
        const QueueSubmissionPreSubmitHook* preSubmitHook,
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks,
        usize taskAcceptedCallbackCount
    )const;


public:
    explicit GpuTaskGraphSubmitter(Device& device)
        : m_device(device)
    {}


public:
    // One-packet compatibility entry point. It publishes graph and transaction acceptance synchronously after the
    // native submit and carries no packet/task compatibility callback obligation.
    [[nodiscard]] bool submitPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketId& packet,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuTimingSubmissionTicket* timingTicket = nullptr,
        const QueueSubmissionPreSubmitHook* preSubmitHook = nullptr
    )const;
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
        const GpuTaskGraphPacketTimingTicket* timingTickets,
        usize timingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr,
        const GpuTaskGraphPacketSubmissionHook* submissionHooks = nullptr,
        usize submissionHookCount = 0u,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u
    )const;
    // Semantic companion to packet-range submission. It resolves the inclusive compiler-order range from declared
    // task endpoints and leaves packet IDs only for narrow accepted-token and pre-submit hook integrations.
    [[nodiscard]] bool submitTaskRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        const GpuTaskGraphExternalCompletionToken* externalCompletionTokens,
        usize externalCompletionTokenCount,
        const GpuTaskGraphPacketTimingTicket* timingTickets,
        usize timingTicketCount,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr,
        const GpuTaskGraphPacketSubmissionHook* submissionHooks = nullptr,
        usize submissionHookCount = 0u,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u
    )const;
    // Records then submits every ordinary compiler packet. Accepted-frontier packets must form one terminal suffix;
    // the executor rejects a non-terminal frontier before recording so recovery/finalization remains explicit and
    // retry-safe. It never discards remaining work or submits the frontier suffix on the caller's behalf.
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
    // Records then submits one compiler-derived normal range in serial compile order. Recovery/finalization packets
    // that join the accepted queue frontier are deliberately rejected here: callers retain explicit ownership of
    // their late tail, cleanup, and recovery policy. This helper does not discard or reject any remaining work.
    [[nodiscard]] bool recordAndSubmitPacketRangeInCompileOrder(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketRange& range,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Semantic companion to the serial normal range executor. It resolves inclusive compiler-order bounds from
    // declared task endpoints, preserving the packet helper's recovery-tail preflight, failure result, and caller
    // owned cleanup policy.
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
    // Ready-frontier variant of the normal range executor. It preserves the serial helper's recovery-tail
    // preflight and submission order, but gives explicitly opted-in packets isolated worker recording leases.
    // Packets without opt-in retain the recorder's serial fallback. Callers retain all cleanup and recovery policy.
    [[nodiscard]] bool recordAndSubmitPacketRangeInReadyFrontiers(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        Alloc::ThreadPool& workerPool,
        const GpuSubmissionPacketRange& range,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Semantic ready-frontier companion. Task endpoints resolve through the current compiled graph before the
    // packet helper applies its normal recovery-tail preflight and preserves the caller's cleanup ownership.
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
    // Records and submits one semantic late task after its graph dependencies have accepted. Optional
    // task-anchored state bindings are resolved only while recording, and a recorded callback may validate the
    // packet's immutable final-state seed before submission. Any rejection leaves task lifecycle owned by the
    // transaction rather than a renderer-local packet/retry path.
    [[nodiscard]] bool recordAndSubmitTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        const GpuTaskPacketStateBinding* taskStateBindings,
        usize taskStateBindingCount,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Semantic companion to the packet-timing overload above.  It resolves timing tickets and one-shot native
    // pre-submit hooks through the current compiled graph, so packet splitting/merging remains compiler-owned.
    // Multiple timing bindings may target one packet only when they deliberately share the same timing ticket;
    // pre-submit hooks instead require one unambiguous packet target.
    [[nodiscard]] bool submitPacketRangeInCompileOrderFromTasks(
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
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr,
        const GpuTaskGraphPacketSubmissionHook* submissionHooks = nullptr,
        usize submissionHookCount = 0u,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr,
        usize taskSubmissionHookCount = 0u
    )const;
    // Resolves both the submitted range and timing bindings from semantic tasks after compilation. Multiple timing
    // anchors may share one packet only when they deliberately reference the same submission ticket.
    [[nodiscard]] bool submitTaskRangeInCompileOrderFromTasks(
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
        const GpuTaskGraphPacketAcceptedCallback* acceptedCallback = nullptr,
        const GpuTaskGraphPacketSubmissionHook* submissionHooks = nullptr,
        usize submissionHookCount = 0u,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr,
        usize taskSubmissionHookCount = 0u
    )const;


private:
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

