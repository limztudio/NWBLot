// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"

#include <core/alloc/scratch.h>
#include <core/alloc/thread.h>
#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingSubmissionTicket;
class GpuTaskGraph;
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
    // Zero is serial/default recording. Nonzero values identify the ready-frontier worker lease that opened this
    // packet's native command list; kept for transactional diagnostics and worker-affinity smoke coverage.
    u32 recordingWorkerIndex = 0u;
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
    void reset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
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
    // Internal ready-frontier lease selector. Direct callers leave this at zero; the recorder derives nonzero
    // values from Alloc::ThreadPool rather than exposing backend-native command-pool handles to task payloads.
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
        Accepted,
        Rejected,
    };
};

struct GpuPacketRuntime{
    GpuPacketRuntimeState::Enum state = GpuPacketRuntimeState::Declared;
    QueueSubmissionToken token;
};

struct GpuTaskGraphExternalCompletionToken{
    GpuExternalCompletionId completion;
    // The token must retain the exact physical queue and device-generation identity returned by native submission.
    // Graph waits reject broad CommandQueue-only completions so a stale timeline value cannot alias a recreated
    // device or a future same-class queue.
    QueueSubmissionToken token;

    [[nodiscard]] bool validFor(const GpuCompiledGraph& compiledGraph)const noexcept;
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


// Runs immediately after a packet accepts its submission token. Returning false stops the current range traversal,
// but retains the accepted packet so the caller can submit an appropriate recovery or finalization tail.
struct GpuTaskGraphPacketAcceptedCallback{
    void* context = nullptr;
    [[nodiscard]] bool (*invoke)(
        void* context,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    ) = nullptr;
};

// Semantic acceptance binding for one declared task. The submitter resolves the task after compilation and invokes
// bindings in compiled task order once their containing native packet has accepted. Returning false stops range
// traversal after retaining that accepted packet, just like the packet-wide compatibility callback.
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


class GpuGraphSubmissionTransaction final : NoCopy{
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
    [[nodiscard]] bool markPacketRecorded(const GpuSubmissionPacketId& packet)noexcept;
    [[nodiscard]] bool acceptPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet,
        const QueueSubmissionToken& token
    )noexcept;
    void rejectPacket(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuSubmissionPacketId& packet
    )noexcept;
    // Semantic companion to rejectPacket(). It resolves the current packet only inside the transaction, so
    // renderer recovery code can revoke unaccepted graph work without mirroring compiler packet identities.
    void rejectTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task
    )noexcept;
    void discardUnaccepted(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept;

    [[nodiscard]] bool hasAcceptedPackets()const noexcept{ return m_valid && m_acceptedSubmissionCount != 0u; }
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
    // latest token per physical queue, and merge the exact terminal state ranges without exposing packet IDs.
    [[nodiscard]] GpuTaskGraphExternalResourceHandoff externalResourceHandoff(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        GpuGraphResourceId resource
    )const noexcept;
    // Source-compatible one-producer query. A true multi-producer texture export requires the graph-aware overload
    // above so it can filter and merge the exact terminal ranges; this legacy form deliberately returns invalid for
    // that case instead of publishing a partial state snapshot.
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
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };

    // Each compiled external release receives stable transaction-owned query storage.  The merged state is rebuilt
    // only when its resource is queried, while the outer vector is fully allocated at reset so a handoff for one
    // resource remains valid when another resource is queried.
    struct ExternalResourceHandoffScratch final : NoCopy{
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

        GraphicsArena& m_arena;
        GpuGraphResourceId resource;
        CommandListResourceStateHandoff stateSource;
        CommandListResourceStateHandoff stateMerge;
        GraphicsVector<CommandListResourceStateHandoff> stateBranches;
        GraphicsVector<const CommandListResourceStateHandoff*> stateBranchPointers;
        GraphicsVector<GpuTaskGraphExternalResourceHandoffProducer> producers;
        GraphicsVector<GpuTaskGraphExternalResourceHandoffRange> terminalRanges;
        GraphicsVector<QueueSubmissionToken> waitTokens;
    };

    GraphicsArena& m_arena;
    GraphicsVector<GpuPacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    mutable GraphicsVector<ExternalResourceHandoffScratch> m_externalResourceHandoffScratch;
    u64 m_generation = 0u;
    u16 m_deviceGeneration = 0u;
    u64 m_acceptedSubmissionCount = 0u;
    bool m_valid = false;
};


class GpuTaskGraphSubmitter final : NoCopy{
public:
    explicit GpuTaskGraphSubmitter(Device& device)
        : m_device(device)
    {}


public:
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
    // accepted in the transaction; this preserves graph-owned waits while allowing intentional late tails. An
    // accepted callback may stop the range after retaining its packet for recovery or finalization.
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

