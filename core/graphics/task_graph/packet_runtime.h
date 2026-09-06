// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "packet_runtime_artifact.h"
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
class GpuGraphSubmissionTransactionGateTestAccess;
struct GpuTaskGraphRuntimeStatistics;


struct GpuRecordedPacket{
    static constexpr usize s_MaxCommandLists = 12u;

    GpuSubmissionPacketId packet;
    // Native recording owns its newly-created lists through submission.
    CommandListHandle ownedCommandLists[s_MaxCommandLists] = {};
    CommandList* commandLists[s_MaxCommandLists] = {};
    // Exact graph-publication identities let reset/destruction revoke an unsubmitted list without touching a later
    // recording that happens to reuse the same retained CommandList object.
    u64 commandListRecordingLeaseSerials[s_MaxCommandLists] = {};
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
    friend GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuGraphSubmissionTransaction& transaction
    )noexcept;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuGraphSubmissionTransactionGateTestAccess;
    friend class GpuNativePacketRecorder;
    friend class GpuTaskGraphSubmitter;

private:
    struct ArtifactStorage;

    static constexpr u32 s_ArtifactOperationWriterBit = 1u << 31u;
    static constexpr u32 s_ArtifactOperationReaderMask = s_ArtifactOperationWriterBit - 1u;


private:
    enum class ArtifactOperationMode : u8{
        Read,
        WaitRead,
        Exclusive,
    };

    class ArtifactOperation final : NoCopy{
    private:
        static thread_local ArtifactOperation* s_activeOperation;


    public:
        [[nodiscard]] static bool active()noexcept{ return s_activeOperation != nullptr; }
        [[nodiscard]] static bool activeFor(const GpuRecordedGraph& recordedGraph)noexcept;
        [[nodiscard]] static bool activeExclusiveFor(const GpuRecordedGraph& recordedGraph)noexcept;
        [[nodiscard]] static bool activeScopeIs(const ArtifactOperation& operation)noexcept{
            if(
                !operation.m_ownsAdmission
                || !operation.m_recordedGraph
            )
                return false;
            bool foundOperation = false;
            for(const ArtifactOperation* active = s_activeOperation; active; active = active->m_previousOperation){
                if(active->m_recordedGraph != operation.m_recordedGraph)
                    return false;
                if(active == &operation)
                    foundOperation = true;
            }
            return foundOperation;
        }


    public:
        ArtifactOperation(const GpuRecordedGraph& recordedGraph, ArtifactOperationMode mode)noexcept;
        ~ArtifactOperation()noexcept;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_recordedGraph != nullptr; }
        [[nodiscard]] bool validFor(const GpuRecordedGraph& recordedGraph)const noexcept{
            return m_recordedGraph == &recordedGraph;
        }
        [[nodiscard]] bool exclusiveFor(const GpuRecordedGraph& recordedGraph)const noexcept{
            return m_recordedGraph == &recordedGraph && m_exclusive;
        }


    private:
        const GpuRecordedGraph* m_recordedGraph = nullptr;
        ArtifactOperation* m_previousOperation = nullptr;
        bool m_exclusive = false;
        bool m_ownsAdmission = false;
    };

private:
    // Every ready-frontier worker receives isolated state-handoff scratch. This is separate from the per-packet
    // final-state slots, which are written only by the packet's own recording worker and read by later frontiers.
    struct PacketRecordingScratch final : NoCopy{
        GlobalUniquePtr<Alloc::ScratchArena> stateFanInScratchArena;
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

        [[nodiscard]] bool ensureValid(GraphicsArena& arena);
        void reset()noexcept;
        [[nodiscard]] bool valid()const noexcept{ return stateFanInScratchArena != nullptr; }
    };


public:
    explicit GpuRecordedGraph(GraphicsArena& arena);
    ~GpuRecordedGraph();


public:
    // A live record/submit operation leases every packet, timing ticket, state seed, and command-list handle in this
    // artifact. Failed reset leaves all storage and exact publication identities unchanged so the operation can
    // finish or unwind safely; reset() retains the assertion-style compatibility contract for serialized callers.
    [[nodiscard]] bool tryReset(const GpuCompiledGraph& compiledGraph);
    void reset(const GpuCompiledGraph& compiledGraph);


private:
    void revokeCommandListPublicationsWithoutCallbacks(ArtifactStorage& storage)noexcept;
    static void retireStorageWithoutCallbacks(ArtifactStorage& storage)noexcept;
    [[nodiscard]] bool prepareStorageCandidateLayout(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    );
    [[nodiscard]] bool prepareResetStorageCandidate(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    );
    [[nodiscard]] bool prepareRecordingStorageCandidate(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTimingRecorder* timingRecorder
    );
    void publishStorageCandidate(
        const GpuTaskGraph* graphIdentity,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        u64 recordingAttemptGeneration,
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] bool validForWithinArtifactOperation(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const ArtifactOperation& artifactAccess
    )const noexcept;
    [[nodiscard]] bool validForWithinArtifactOperation(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const ArtifactOperation& artifactAccess
    )const noexcept;


public:
    [[nodiscard]] bool validFor(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;
    [[nodiscard]] bool validFor(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    // Returns an owned aggregate from packet slots published under artifact admission. The exact plan proof keeps
    // packet metadata alive and rejects a recorded artifact from another compiled object.
    [[nodiscard]] GpuTaskGraphRecordingStatistics recordingStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;
    // Returns an owned physical-queue aggregate. Invalid/stale queue IDs and a recorded artifact from another exact
    // compiled object return an empty result.
    [[nodiscard]] GpuTaskGraphPhysicalQueueRecordingStatistics physicalQueueRecordingStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPhysicalQueueId& queue
    )const noexcept;
    [[nodiscard]] Optional<GpuRecordedPacket> packetSnapshot(const GpuSubmissionPacketId& packet)const noexcept;
    // Validates this recorded graph against the current compiler output and resolves the declared task's containing
    // packet. The result is that packet's actual native final state, not a task-local intermediate snapshot; merged
    // tasks therefore resolve to the same state.
    [[nodiscard]] bool hasTaskFinalStateSeed(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskId task
    )const noexcept;
    [[nodiscard]] bool copyTaskFinalStateSeed(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskId task,
        CommandListResourceStateHandoff& outStateSeed
    )const;


private:
    void discardPacketTimingTicket(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    );
    void abandonPacketTimingTicketWithoutCallbacks(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] GpuTimingSubmissionTicket* packetTimingTicket(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )const noexcept;
    [[nodiscard]] bool buildPacketInitialStateSeed(
        PacketRecordingScratch& scratch,
        Alloc::ScratchArena& stateFanInScratchArena,
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketId& packet,
        const CommandListResourceStateHandoff*& outInitialStates
    );
    [[nodiscard]] CommandListResourceStateHandoff* packetStateSeed(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] const CommandListResourceStateHandoff* packetStateSeed(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )const noexcept;
    [[nodiscard]] PacketRecordingScratch* packetRecordingScratch(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] PacketRecordingScratch* serialRecordingScratch(
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] GpuRecordedPacket* packetStorage(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )noexcept;
    void clearPacketPublicationWithoutCallbacks(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )noexcept;
    [[nodiscard]] const GpuRecordedPacket* findWithinArtifactOperation(
        const GpuSubmissionPacketId& packet,
        const ArtifactOperation& artifactAccess
    )const noexcept;
    [[nodiscard]] GpuTimingRecorder* timingRecorderWithinArtifactOperation(
        const ArtifactOperation& artifactAccess
    )const noexcept;
    [[nodiscard]] u64 recordingAttemptGenerationWithinArtifactOperation(
        const ArtifactOperation& artifactAccess
    )const noexcept;
    void addRecordingElapsedSeconds(f64 elapsedSeconds, const ArtifactOperation& artifactAccess)noexcept;
    void addReadyFrontierStatistics(
        f64 elapsedSeconds,
        f64 workerBusySeconds,
        f64 workerCapacitySeconds,
        const ArtifactOperation& artifactAccess
    )noexcept;
    void cachePacketRecordingOverlaps(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const Vector<u32, Alloc::ScratchArena>& packetIndices,
        Alloc::ScratchArena& scratchArena,
        const ArtifactOperation& artifactAccess
    );


private:
    GraphicsArena& m_arena;
    GlobalUniquePtr<ArtifactStorage> m_activeStorage;
    GlobalUniquePtr<ArtifactStorage> m_candidateStorage;
    // Submission readers may overlap on independent queues. Conflicting record/submit/reset admission is
    // nonblocking, while destruction alone joins operations that already own the artifact.
    mutable Atomic<u32> m_operationState{ 0u };
};


class GpuNativePacketRecorder final : NoCopy{
    friend class GpuTaskGraphSubmitter;

private:
    class PacketRecordingExceptionScope;
    class PacketArtifactPublicationScope;
    class ReadyFrontierRecordingUnwindScope;


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

    [[nodiscard]] bool validFor(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;
    // Compatibility bindings are valid only for metadata-only nodes. A graph-owned completion deliberately rejects
    // a second runtime token so one semantic edge can never acquire two competing native timeline identities.
    [[nodiscard]] bool validFallbackFor(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )const noexcept;
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
    friend GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuGraphSubmissionTransaction& transaction
    )noexcept;
    friend class GpuRecordedGraph;
    friend class GpuGraphSubmissionTransactionGateTestAccess;
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
        WaitExclusiveBarrier,
        TryExclusiveBarrier,
        CompositeBarrier,
        ExceptionFinalizer,
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
    class SubmissionWriterReservation final : NoCopy{
    public:
        SubmissionWriterReservation()noexcept = default;
        ~SubmissionWriterReservation()noexcept;


    public:
        [[nodiscard]] bool acquire(
            const GpuGraphSubmissionTransaction& transaction,
            bool tryOnly
        )noexcept;
        void reset()noexcept;
        [[nodiscard]] bool valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
    };

    class SubmissionOperation final : NoCopy{
    private:
        static thread_local SubmissionOperation* s_activeOperation;


    public:
        [[nodiscard]] static bool active()noexcept{ return s_activeOperation != nullptr; }
        [[nodiscard]] static bool activeFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return s_activeOperation && s_activeOperation->m_transaction == &transaction;
        }
        [[nodiscard]] static bool activeExclusiveFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return activeFor(transaction) && s_activeOperation->m_exclusive;
        }
    public:
        SubmissionOperation(
            const GpuGraphSubmissionTransaction& transaction,
            SubmissionOperationMode mode,
            const GpuRecordedGraph::ArtifactOperation* borrowedArtifact = nullptr
        )noexcept;
        ~SubmissionOperation()noexcept;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
        SubmissionOperation* m_previousOperation = nullptr;
        SubmissionWriterReservation m_writerReservation;
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

    class AcceptedPacketPublicationGuard;
    class RejectingPacketUnwindScope;
    class RejectingSubmissionUnwindScope;
    class UnacceptedPacketsFinalizationScope;
    class UnacceptedPacketsUnwindScope;

public:
    explicit GpuGraphSubmissionTransaction(GraphicsArena& arena)
        : m_arena(arena)
        , m_packets(arena)
        , m_latestAcceptedQueueTokens(arena)
        , m_externalResourceHandoffBuildScratch(Name("core/graphics/task_graph/external_resource_handoff"))
        , m_transactionIdentity(GpuTaskGraph::allocateGeneration())
    {}
    ~GpuGraphSubmissionTransaction()noexcept;


public:
    void reset(const GpuCompiledGraph& compiledGraph);
    // Returns false without changing packet state, tokens, frontier, or statistics while this logical transaction
    // still owns any nonterminal packet in a graph recording attempt.
    [[nodiscard]] bool tryReset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    // Semantic task rejection resolves the current packet only inside the transaction, so renderer recovery code
    // can revoke unaccepted graph work without mirroring compiler packet identities.
    void rejectTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        u64 recordingAttemptGeneration
    );
    // Returns false when a packet is actively recording/submitting or the transaction no longer owns this attempt.
    // Callers must retain the graph until a true result confirms that every unaccepted packet was resolved.
    [[nodiscard]] bool discardUnaccepted(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    );

    [[nodiscard]] bool hasAcceptedPackets()const noexcept;
    [[nodiscard]] GpuTaskGraphSubmissionStatistics submissionStatistics()const noexcept;
    // Copies one accepted native packet's exact wait decomposition and compiler role while holding the transaction
    // mutex. The result owns every field; the required plan proof protects metadata during the copy.
    [[nodiscard]] GpuTaskGraphPacketSubmissionStatistics packetSubmissionStatistics(
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId& packet
    )const noexcept;
    // Aggregates one full physical-queue snapshot while holding the transaction mutex. Invalid/stale queue IDs and
    // a transaction from another compiled plan return an empty owned result.
    [[nodiscard]] GpuTaskGraphPhysicalQueueSubmissionStatistics physicalQueueSubmissionStatistics(
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPhysicalQueueId& queue
    )const noexcept;
    // Copies one packet-indexed acceptance snapshot while holding the transaction mutex once. The caller supplies
    // exactly compiledPlan.packetCount() entries; rejected and unresolved packets are represented by invalid
    // tokens. The accompanying recording attempt and process-unique acceptance revision are copied under that same
    // lock. Invalid, stale, mismatched-plan, and wrong-sized requests leave both caller outputs untouched. The plan
    // proof remains live for the complete packet-indexed copy.
    [[nodiscard]] bool copyAcceptedPacketTokens(
        const GpuCompiledGraph::ReadView& compiledPlan,
        QueueSubmissionToken* outTokens,
        usize tokenCount,
        GpuGraphSubmissionAcceptanceSnapshot& outSnapshot
    )const noexcept;
    [[nodiscard]] QueueSubmissionToken packetToken(const GpuSubmissionPacketId& packet)const noexcept;
    // Resolves the current compiler packet for semantic graph work before returning its accepted submission token.
    // This is generation-checked so renderer lifecycle code cannot treat a task from an older compiled graph as
    // an accepted submission on a replacement device or packetization.
    [[nodiscard]] QueueSubmissionToken taskToken(
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskId task
    )const noexcept;
    // Publishes the exact external final-state/ownership handoff only after every compiler-selected terminal
    // producer packet accepted. The caller-owned snapshot retains every state, producer, range, and wait token after
    // transaction and recorded-artifact read admission ends. Failure leaves a prior snapshot unchanged.
    [[nodiscard]] bool externalResourceHandoff(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        GpuGraphResourceId resource,
        GpuTaskGraphExternalResourceHandoffSnapshot& outSnapshot
    )const;


private:
    // Appends one latest accepted token for every physical queue other than `destinationQueue`. A recovery packet
    // submitted on that destination does not need to wait on its own queue because queue order already supplies the
    // dependency; every other physical producer remains an explicit timeline wait.
    [[nodiscard]] bool appendAcceptedQueueFrontierWaitTokens(
        const GpuPhysicalQueueId& destinationQueue,
        Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens
    )const;

    [[nodiscard]] bool validForLocked(const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    [[nodiscard]] bool hasUnresolvedSubmissionBinding(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] bool waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()const noexcept;
    [[nodiscard]] QueueSubmissionToken packetTokenLocked(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] QueueSubmissionToken taskTokenLocked(
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskId task
    )const noexcept;


private:
    [[nodiscard]] bool allPacketsTerminalLocked()const noexcept;
    [[nodiscard]] bool bindRecordingAttemptWithinSubmissionOperation(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuTaskGraph::RecordingAttemptScope* preparationAttempt = nullptr
    )noexcept;
    [[nodiscard]] bool matchesRecordingAttemptBinding(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;
    [[nodiscard]] bool beginSubmissionExceptionClosingWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        u64& outRecordingAttemptGeneration,
        GpuGraphSubmissionBinding& outSubmissionBinding
    )noexcept;
    [[nodiscard]] bool submissionExceptionClosingResolved(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )noexcept;
    void completeSubmissionExceptionClosingWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )noexcept;
    void resolveSubmissionBindingIfTerminalLocked(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph
    )noexcept;
    void rejectTaskWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskId task,
        u64 recordingAttemptGeneration
    );

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
        GpuTimingSubmissionTicket* const* timingTickets,
        usize timingTicketCount,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr,
        usize taskAcceptedCallbackCount = 0u
    );
    void commitAcceptedPacket(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        const NativeSubmissionInfo& nativeSubmissionInfo
    )noexcept;
    void abandonTimingTicketsWithoutCallbacks(
        GpuTimingSubmissionTicket* const* timingTickets,
        usize timingTicketCount
    )noexcept;
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId& packet,
        u64 recordingAttemptGeneration
    );
    void rejectSubmittingPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        GpuTaskGraph::PacketSubmissionLease& lease
    );
    void abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        GpuTaskGraph::PacketSubmissionLease& lease
    )noexcept;
    void completeRejectedPacketWithinSubmissionOperation(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        bool nativeSubmissionRejected
    )noexcept;
    void abandonUnacceptedPacketsAfterExceptionWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess
    )noexcept;
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };


private:
    GraphicsArena& m_arena;
    GraphicsVector<PacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    mutable Alloc::ScratchArena m_externalResourceHandoffBuildScratch;
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
    // A scheduler-owned wait/notify RW gate makes operation acquisition and unwind genuinely non-throwing. Pending
    // blocking writers prevent new readers from overtaking them without relying on a vendor lock ABI.
    mutable Atomic<u32> m_submissionGateState{ 0u };
    mutable Atomic<u32> m_submissionGateWriterCount{ 0u };
    mutable AtomicFlag m_submissionExceptionClosing;
    u64 m_exceptionClosingRecordingAttemptGeneration = 0u;
    GpuGraphSubmissionBinding m_exceptionClosingBinding;
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


// The returned values are immutable copies. The exact plan proof protects compiler metadata while artifact and
// transaction admission produce one internally consistent snapshot.
[[nodiscard]] GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept;


class GpuTaskGraphSubmitter final : NoCopy{
private:
    class PreparedTimingTicketsUnwindScope;
    class SubmittingPacketUnwindScope;

    enum class PacketRangeSubmissionOperationPolicy : u8{
        PerPacket,
        ActiveExclusiveBarrier,
    };

    class SubmissionAttemptExceptionFinalizer final : NoCopy{
    private:
        static thread_local SubmissionAttemptExceptionFinalizer* s_activeFinalizer;


    public:
        [[nodiscard]] static SubmissionAttemptExceptionFinalizer* activeFor(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuRecordedGraph& recordedGraph,
            const GpuGraphSubmissionTransaction& transaction
        )noexcept;


    public:
        SubmissionAttemptExceptionFinalizer(
            GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuRecordedGraph& recordedGraph,
            GpuGraphSubmissionTransaction& transaction
        )noexcept;
        ~SubmissionAttemptExceptionFinalizer()noexcept;


    public:
        void beginClosingWithinSubmissionOperation()noexcept;


    private:
        GpuTaskGraph& m_graph;
        const GpuCompiledGraph& m_compiledGraph;
        const GpuRecordedGraph& m_recordedGraph;
        GpuGraphSubmissionTransaction& m_transaction;
        SubmissionAttemptExceptionFinalizer* m_previousFinalizer = nullptr;
        SubmissionAttemptExceptionFinalizer* m_owner = nullptr;
        u64 m_recordingAttemptGeneration = 0u;
        GpuGraphSubmissionBinding m_submissionBinding;
        i32 m_uncaughtExceptionCount = 0;
        bool m_installed = false;
        bool m_armed = false;
    };

    class SubmissionAttemptExceptionScope final : NoCopy{
    public:
        SubmissionAttemptExceptionScope(
            GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuRecordedGraph& recordedGraph,
            GpuGraphSubmissionTransaction& transaction,
            GpuSubmissionPacketId* outFailedPacket = nullptr
        )noexcept;
        ~SubmissionAttemptExceptionScope()noexcept;


    public:
        void setFailedPacket(GpuSubmissionPacketId packet)noexcept{ m_failedPacket = packet; }
        void complete()noexcept{ m_active = false; }


    private:
        SubmissionAttemptExceptionFinalizer* m_finalizer = nullptr;
        GpuSubmissionPacketId* m_outFailedPacket = nullptr;
        GpuSubmissionPacketId m_failedPacket;
        i32 m_uncaughtExceptionCount = 0;
        bool m_active = true;
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
    // A non-null pool opts native recording into Vulkan ready-frontier parallelism.
    [[nodiscard]] bool recordPacketRange(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& recordedGraph,
        Alloc::ThreadPool* readyFrontierWorkerPool,
        GpuCommandIrCapture* commandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // A non-null pool changes only native recording; submission and recovery-tail ownership stay shared.
    [[nodiscard]] bool recordAndSubmitTaskRange(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        Alloc::ThreadPool* readyFrontierWorkerPool,
        GpuTaskId firstTask,
        GpuTaskId lastTask,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // The caller owns the transaction's exclusive SubmissionOperation. This internal path lets the accepted-frontier
    // composite reuse the ordinary task executor without attempting forbidden same-transaction gate reentry.
    [[nodiscard]] bool recordAndSubmitTaskWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
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
        GpuTimingRecorder* timingRecorder,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess
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
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
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

