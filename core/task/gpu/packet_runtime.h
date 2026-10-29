// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "packet_runtime_artifact.h"
#include "packet_runtime_recorded_graph.h"
#include "task_graph.h"
#include "scheduler_callback_bindings.h"

#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/graphics/rhi/device.h>

#include <global/sync.h>
#include <global/simd/vector.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;
class GpuTimingSubmissionTicket;
class GpuTaskScheduler;
class GpuNativePacketRecorder;
class GpuCommandIrCapture;
struct GpuTaskGraphRuntimeStatistics;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuPacketRuntimeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_ExternalResourceHandoffScratchArena("core/task/gpu/external_resource_handoff");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Timing ticket bound to semantic work (scheduler resolves to packet at admission). Same-packet aliases coalesce; one ticket never spans packets.
struct GpuTaskGraphTaskTimingTicket{
    GpuTaskId task;
    GpuTimingSubmissionTicket* timingTicket = nullptr;
};

// Pre-submit binding for one task. Admission attaches the hook to its packet; two bindings on one packet are rejected.
struct GpuTaskGraphTaskSubmissionHook{
    GpuTaskId task;
    QueueSubmissionPreSubmitHook hook;
};


// Semantic acceptance observer for one declared task.
// The scheduler invokes every matching binding in compiled task order even if an earlier callback returned false.
// The aggregate false result stops later range traversal only after the accepted token/frontier publishes.
// Callbacks run while the transaction submission gate is held and must not reenter or synchronously wait for work that needs the same gate.
struct GpuTaskGraphTaskAcceptedCallback{
    GpuTaskId task;
    void* context = nullptr;
    [[nodiscard]] b8 (*invoke)(void* context, const QueueSubmissionToken& token) = nullptr;
};

// Runs after a task records and exports packet state, before submit. The anchor checks final-state candidates.
struct GpuTaskGraphTaskRecordedCallback{
    GpuTaskId task;
    void* context = nullptr;
    [[nodiscard]] b8 (*invoke)(void* context, const CommandListResourceStateHandoff* finalState) = nullptr;
};


// Ordinary-packet prefix in compiler order; an optional terminal task includes its packet. Bindings survive re-splitting.
struct GpuTaskGraphNormalExecutionDesc{
    GpuTaskId terminalTask;
    // Invoked in compiler task order after the complete ordinary prefix records and before its first native submit.
    // A false result leaves every packet unaccepted so the caller can discard or recover transactionally.
    const GpuTaskGraphTaskRecordedCallback* taskRecordedCallbacks = nullptr;
    usize taskRecordedCallbackCount = 0u;
    // Null scheduler keeps serial compile-order recording. A scheduler enables ready-frontier policy; non-opt-in packets stay serial.
    CpuTaskScheduler* readyFrontierScheduler = nullptr;
    GpuCommandIrCapture* commandIrCapture = nullptr;
    const GpuTaskGraphTaskTimingTicket* taskTimingTickets = nullptr;
    usize taskTimingTicketCount = 0u;
    const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks = nullptr;
    usize taskAcceptedCallbackCount = 0u;
    const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks = nullptr;
    usize taskSubmissionHookCount = 0u;
};


// Submission telemetry. Wait counts follow the same elision/merge rules as Device::executeCommandLists().
struct GpuTaskGraphSubmissionStatistics{
    static constexpr usize s_QueueClassCount = static_cast<usize>(CommandQueue::kCount);

    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    u16 deviceGeneration = 0u;
    usize acceptedPacketCount = 0u;
    usize acceptedTaskCount = 0u;
    // Counts each packet when this transaction reaches terminal Rejected; repeat cleanup on an already terminal packet adds nothing.
    usize rejectedPacketCount = 0u;
    usize rejectedTaskCount = 0u;
    usize nativeSubmissionCount = 0u;
    // Native-submit failure subset of rejectedPacketCount; may precede any backend submit, so not a Vulkan rejection.
    usize rejectedSubmissionCount = 0u;
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    usize inheritedTimelineWaitElisionCount = 0u;
    usize acceptedFrontierSubmissionCount = 0u;
    usize recoverySubmissionCount = 0u;
    usize nativeSubmissionCountByQueueClass[s_QueueClassCount] = {};
    usize nativeCommandListCountByQueueClass[s_QueueClassCount] = {};
    usize timelineWaitCountByQueueClass[s_QueueClassCount] = {};
    f64 submissionSeconds = 0.0;


public:
    [[nodiscard]] b8 valid()const noexcept{ return graphGeneration != 0u && planGeneration != 0u; }
};


// Submission telemetry for one compiler packet. Takes the current plan handle whose packet reached Accepted.
struct GpuTaskGraphPacketSubmissionStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    GpuSubmissionPacketId packet;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    u16 deviceGeneration = 0u;
    b8 joinsAcceptedQueueFrontier = false;
    b8 isRecoverySubmission = false;
    usize taskCount = 0u;
    usize nativeCommandListCount = 0u;
    usize plannedWaitTokenCount = 0u;
    usize sameQueueWaitElisionCount = 0u;
    usize timelineWaitCount = 0u;
    usize mergedTimelineWaitCount = 0u;
    usize inheritedTimelineWaitElisionCount = 0u;
    f64 submissionSeconds = 0.0;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return
            graphGeneration != 0u
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


// Per-queue per-transaction submission telemetry (queue id carries device generation, so no aliasing across recreates).
struct GpuTaskGraphPhysicalQueueSubmissionStatistics{
    u64 graphGeneration = 0u;
    u64 planGeneration = 0u;
    u64 recordingAttemptGeneration = 0u;
    GpuPhysicalQueueId queue;
    CommandQueue::Enum queueClass = CommandQueue::kCount;
    u16 deviceGeneration = 0u;
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
    usize inheritedTimelineWaitElisionCount = 0u;
    usize acceptedFrontierSubmissionCount = 0u;
    usize recoverySubmissionCount = 0u;
    f64 submissionSeconds = 0.0;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return graphGeneration != 0u && planGeneration != 0u && deviceGeneration != 0u && queue.valid() && queue.deviceGeneration == deviceGeneration && queueClass < CommandQueue::kCount;
    }
};


struct GpuGraphSubmissionAcceptanceSnapshot{
    Vector<QueueSubmissionToken, Alloc::ScratchArena> packetTokens;
    u64 recordingAttemptGeneration = 0u;
    u64 acceptanceRevision = 0u;
};


class GpuGraphSubmissionTransaction final : NoCopy{
    friend GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuRecordedGraph& recordedGraph, const GpuGraphSubmissionTransaction& transaction)noexcept;
    friend class GpuRecordedGraph;
    friend class GpuTaskScheduler;


private:
    struct PacketRuntimeState{
        enum Enum : u8{
            Declared,
            Submitting,
            Rejecting,
            Accepted,
            Rejected,
        };
    };

    struct SubmissionOperationMode{
        enum Enum : u8{
            OrdinaryPacket,
            WaitExclusiveBarrier,
            TryExclusiveBarrier,
            CompositeBarrier,
            ExceptionFinalizer,
        };
    };

    struct PacketRuntime{
        QueueSubmissionToken token;
        // Native counters are committed only after the graph lifecycle and its Device submission both accept.
        usize nativeCommandListCount = 0u;
        usize plannedWaitTokenCount = 0u;
        usize sameQueueWaitElisionCount = 0u;
        usize timelineWaitCount = 0u;
        usize mergedTimelineWaitCount = 0u;
        usize inheritedTimelineWaitElisionCount = 0u;
        f64 submissionSeconds = 0.0;
        PacketRuntimeState::Enum state = PacketRuntimeState::Declared;
        // A post-reservation submit-path failure is terminally rejected, but ordinary discard/rejection never sets this flag.
        // It can occur before the backend execute call, such as while validating a timing ticket.
        b8 nativeSubmissionRejected = false;
    };

private:
    class SubmissionWriterReservation final : NoCopy{
    public:
        SubmissionWriterReservation()noexcept = default;
        ~SubmissionWriterReservation()noexcept;


    public:
        [[nodiscard]] b8 acquire(const GpuGraphSubmissionTransaction& transaction, b8 tryOnly)noexcept;
        void reset()noexcept;
        [[nodiscard]] b8 valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
    };

    class SubmissionOperation final : NoCopy{
    private:
        static thread_local SubmissionOperation* s_ActiveOperation;


    public:
        [[nodiscard]] static b8 Active()noexcept{ return s_ActiveOperation != nullptr; }

        [[nodiscard]] static b8 ActiveFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return s_ActiveOperation && s_ActiveOperation->m_transaction == &transaction;
        }

        [[nodiscard]] static b8 ActiveExclusiveFor(const GpuGraphSubmissionTransaction& transaction)noexcept{
            return ActiveFor(transaction) && s_ActiveOperation->m_exclusive;
        }


    public:
        SubmissionOperation(const GpuGraphSubmissionTransaction& transaction, SubmissionOperationMode::Enum mode, const GpuRecordedGraph::ArtifactOperation* borrowedArtifact = nullptr)noexcept;
        ~SubmissionOperation()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_transaction != nullptr; }


    private:
        const GpuGraphSubmissionTransaction* m_transaction = nullptr;
        SubmissionOperation* m_previousOperation = nullptr;
        SubmissionWriterReservation m_writerReservation;
        b8 m_exclusive = false;
        b8 m_composite = false;
    };

private:
    struct NativeSubmissionInfo{
        usize commandListCount = 0u;
        usize plannedWaitTokenCount = 0u;
        usize sameQueueWaitElisionCount = 0u;
        usize timelineWaitCount = 0u;
        usize mergedTimelineWaitCount = 0u;
        usize inheritedTimelineWaitElisionCount = 0u;
        f64 submissionSeconds = 0.0;
    };

    class AcceptedPacketPublicationGuard;
    class RejectingPacketUnwindScope;
    class RejectingSubmissionUnwindScope;
    class UnacceptedPacketsFinalizationScope;
    class UnacceptedPacketsUnwindScope;


private:
    // Wait for in-flight recording claims; false requires terminal handling of the unresolved exception close.


private:
    struct SubmissionExceptionClosing{
        u64 recordingAttemptGeneration = 0u;
        GpuGraphSubmissionBinding submissionBinding;
    };


private:
    struct LatestAcceptedQueueToken{
        GpuPhysicalQueueId queue;
        QueueSubmissionToken token;
    };


private:
    [[nodiscard]] static b8 WaitForSubmissionExceptionClosing(GpuGraphSubmissionTransaction& transaction, GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)noexcept{
        if(transaction.submissionExceptionClosingResolved(compiledGraph, recordingAttemptGeneration, submissionBinding))
            return true;
        if(!graph.waitForSubmissionExceptionRecordingClaims(compiledGraph, recordingAttemptGeneration, submissionBinding)){
            if(transaction.submissionExceptionClosingResolved(compiledGraph, recordingAttemptGeneration, submissionBinding))
                return true;
            TerminateInvariant();
        }
        return transaction.submissionExceptionClosingResolved(compiledGraph, recordingAttemptGeneration, submissionBinding);
    }


public:
    // Declare owner after every object borrowed by task payloads; end all recording/submission calls before it leaves scope.
    // Unaccepted packets finish before payload destruction; accepted state stays with graph/native storage.
    class PayloadLifetimeScope final : NoCopy{
    public:
        PayloadLifetimeScope(GpuGraphSubmissionTransaction& transaction, GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept
            : m_transaction(transaction)
            , m_graph(graph)
            , m_compiledGraph(compiledGraph)
            , m_payloadLifetime(graph)
        {}
        ~PayloadLifetimeScope()noexcept(false);


    private:
        GpuGraphSubmissionTransaction& m_transaction;
        GpuTaskGraph& m_graph;
        const GpuCompiledGraph& m_compiledGraph;
        GpuTaskGraph::PayloadLifetimeScope m_payloadLifetime;
    };


public:
    explicit GpuGraphSubmissionTransaction(GraphicsArena& arena)
        : m_arena(arena)
        , m_packets(arena)
        , m_latestAcceptedQueueTokens(arena)
        , m_resetStagingPackets(arena)
        , m_resetStagingLatestAcceptedQueueTokens(arena)
        , m_externalResourceHandoffBuildScratch(GpuPacketRuntimeDetail::s_ExternalResourceHandoffScratchArena)
        , m_transactionIdentity(GpuTaskGraph::AllocateGeneration())
    {}
    ~GpuGraphSubmissionTransaction()noexcept;


public:
    void reset(const GpuCompiledGraph& compiledGraph);
    // False while this transaction still owns any nonterminal packet; changes nothing.
    [[nodiscard]] b8 tryReset(const GpuCompiledGraph& compiledGraph);

    [[nodiscard]] b8 validFor(const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    // Task rejection resolves the current packet inside the transaction, so recovery revokes unaccepted work directly.
    void rejectTask(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, GpuTaskId task, u64 recordingAttemptGeneration);
    // Returns false when a packet is actively recording/submitting or the transaction no longer owns this attempt.
    // Callers must retain the graph until a true result confirms that every unaccepted packet was resolved.
    [[nodiscard]] b8 discardUnaccepted(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration);

    [[nodiscard]] b8 hasAcceptedPackets()const noexcept;
    [[nodiscard]] GpuTaskGraphSubmissionStatistics submissionStatistics()const noexcept;
    // Copies one accepted native packet's wait decomposition and compiler role under the transaction mutex.
    // The result owns every field; the required plan proof protects metadata during the copy.
    [[nodiscard]] GpuTaskGraphPacketSubmissionStatistics packetSubmissionStatistics(const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId& packetID)const noexcept;
    // Aggregates one physical-queue snapshot under the mutex. Bad queue IDs or another plan return an empty result.
    [[nodiscard]] GpuTaskGraphPhysicalQueueSubmissionStatistics physicalQueueSubmissionStatistics(const GpuCompiledGraph::ReadView& planAccess, const GpuPhysicalQueueId& queue)const noexcept;
    // Packet tokens and acceptance revisions are copied under one lock. Scratch storage is allocated before locking.
    [[nodiscard]] Expected<GpuGraphSubmissionAcceptanceSnapshot> copyAcceptedPacketTokens(const GpuCompiledGraph::ReadView& compiledPlan, Alloc::ScratchArena& scratchArena)const;
    [[nodiscard]] QueueSubmissionToken packetToken(const GpuSubmissionPacketId& packet)const noexcept;
    // Resolves the current compiler packet for semantic graph work before returning its accepted token.
    // Generation-checked: a task from an older compiled graph is never an accepted submission on a replacement device or packetization.
    [[nodiscard]] QueueSubmissionToken taskToken(const GpuCompiledGraph::ReadView& planAccess, GpuTaskId task)const noexcept;
    // Publishes the external handoff after every terminal producer packet accepts; the caller snapshot keeps state after admission.
    [[nodiscard]] b8 externalResourceHandoff(
        const GpuTaskGraph& graph,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        GpuGraphResourceId resource,
        GpuTaskGraphExternalResourceHandoffSnapshot& outSnapshot
    )const;


private:
    // Appends one latest accepted token for every physical queue other than `destinationQueue`.
    // A recovery packet submitted on that destination does not need to wait on its own queue because queue order already supplies the dependency.
    [[nodiscard]] b8 appendAcceptedQueueFrontierWaitTokens(const GpuPhysicalQueueId& destinationQueue, Vector<QueueSubmissionToken, Alloc::ScratchArena>& outTokens)const;

    [[nodiscard]] b8 validForLocked(const GpuCompiledGraph::ReadView& planAccess)const noexcept;
    [[nodiscard]] b8 hasUnresolvedSubmissionBinding(const GpuCompiledGraph& compiledGraph)const noexcept;
    [[nodiscard]] b8 waitForSubmissionPublicationAndHasAcceptedPacketsWithinSubmissionOperation()const noexcept;
    [[nodiscard]] QueueSubmissionToken packetTokenLocked(const GpuSubmissionPacketId& packet)const noexcept;
    [[nodiscard]] QueueSubmissionToken taskTokenLocked(const GpuCompiledGraph::ReadView& planAccess, GpuTaskId task)const noexcept;


private:
    void finishPayloadLifetime(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph);
    [[nodiscard]] b8 allPacketsTerminalLocked()const noexcept;
    [[nodiscard]] b8 bindRecordingAttemptWithinSubmissionOperation(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuRecordingAttemptScope* preparationAttempt = nullptr)noexcept;
    [[nodiscard]] b8 matchesRecordingAttemptBinding(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;


    [[nodiscard]] Expected<SubmissionExceptionClosing> beginSubmissionExceptionClosingWithinSubmissionOperation(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept;
    [[nodiscard]] b8 submissionExceptionClosingResolved(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)noexcept;
    void completeSubmissionExceptionClosingWithinSubmissionOperation(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )noexcept;
    void resolveSubmissionBindingIfTerminalLocked(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph)noexcept;
    void rejectTaskWithinSubmissionOperation(GpuTaskGraph& graph, const GpuTaskGraph::DeclarationReadView& declarationAccess, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuTaskId task, u64 recordingAttemptGeneration);

    // Reserves native submission first. While Submitting, cancellation cannot run the discard callback or claim the graph.
    [[nodiscard]] b8 beginPacketSubmission(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, GpuSubmissionPacketId packetID, u64 recordingAttemptGeneration, GpuPacketSubmissionLease& outLease)noexcept;
    [[nodiscard]] b8 acceptSubmittingPacket(
        GpuTaskGraph& graph,
        GpuPacketSubmissionLease& lease,
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packetID,
        const QueueSubmissionToken& token,
        const NativeSubmissionInfo& nativeSubmissionInfo,
        const NotNull<GpuTimingSubmissionTicket*>* timingTickets,
        usize timingTicketCount,
        const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks
    );
    void commitAcceptedPacket(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, GpuSubmissionPacketId packetID, const QueueSubmissionToken& token, const NativeSubmissionInfo& nativeSubmissionInfo)noexcept;
    void abandonTimingTicketsWithoutCallbacks(const NotNull<GpuTimingSubmissionTicket*>* timingTickets, usize timingTicketCount)noexcept;
    void rejectPacket(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId& packetID, u64 recordingAttemptGeneration);
    void rejectSubmittingPacket(GpuTaskGraph& graph, GpuPacketSubmissionLease& lease, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packetID);
    void abandonSubmittingPacketAfterExceptionWithinSubmissionOperation(GpuTaskGraph& graph, GpuPacketSubmissionLease& lease, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packetID)noexcept;
    void completeRejectedPacketWithinSubmissionOperation(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packetID, b8 nativeSubmissionRejected)noexcept;
    void abandonUnacceptedPacketsAfterExceptionWithinSubmissionOperation(GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess)noexcept;


private:
    GraphicsArena& m_arena;
    GraphicsVector<PacketRuntime> m_packets;
    GraphicsVector<LatestAcceptedQueueToken> m_latestAcceptedQueueTokens;
    // Reset fills these under its exclusive barrier and swaps them with the live arrays. They stay empty between resets and only keep capacity.
    GraphicsVector<PacketRuntime> m_resetStagingPackets;
    GraphicsVector<LatestAcceptedQueueToken> m_resetStagingLatestAcceptedQueueTokens;
    mutable Alloc::ScratchArena m_externalResourceHandoffBuildScratch;
    u64 m_generation = 0u;
    u64 m_planGeneration = 0u;
    u64 m_recordingAttemptGeneration = 0u;
    const u64 m_transactionIdentity = 0u;
    u64 m_resetGeneration = 0u;
    GpuGraphSubmissionBinding m_activeSubmissionBinding;
    u64 m_acceptedSubmissionCount = 0u;
    u64 m_acceptanceRevision = 0u;
    GpuTaskGraphSubmissionStatistics m_submissionStatistics;
    // Ready-frontier workers cannot inherit the caller's thread-local operation chain.
    // A composite writer therefore closes public operation admission across threads before it invokes or waits for arbitrary record callbacks.
    mutable AtomicFlag m_compositeOperationActive;
    // A scheduler-owned wait/notify RW gate makes operation acquisition and unwind genuinely non-throwing.
    // Pending blocking writers prevent new readers from overtaking them without relying on a vendor lock ABI.
    mutable Atomic<u32> m_submissionGateState{ 0u };
    mutable Atomic<u32> m_submissionGateWriterCount{ 0u };
    mutable AtomicFlag m_submissionExceptionClosing;
    u64 m_exceptionClosingRecordingAttemptGeneration = 0u;
    GpuGraphSubmissionBinding m_exceptionClosingBinding;
    // Native submission returns before timing, graph payload, task acceptance callback, and token/frontier resolution.
    // Keep that indivisible publication tail serialized while native queue work remains free to overlap.
    mutable Futex m_resolutionMutex;
    mutable Futex m_mutex;
    u16 m_deviceGeneration = 0u;
    b8 m_submissionBindingResolved = false;
    b8 m_valid = false;
};


struct GpuTaskGraphRuntimeStatistics{
    GpuTaskGraphCompileStatistics compile;
    GpuTaskGraphRecordingStatistics recording;
    GpuTaskGraphSubmissionStatistics submission;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return
            compile.valid()
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


// Returned values are immutable copies; the plan proof guards metadata during the copy.
[[nodiscard]] GpuTaskGraphRuntimeStatistics CollectGpuTaskGraphRuntimeStatistics(
    const GpuCompiledGraph& compiledGraph,
    const GpuCompiledGraph::ReadView& planAccess,
    const GpuRecordedGraph& recordedGraph,
    const GpuGraphSubmissionTransaction& transaction
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

