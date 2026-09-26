// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphAnalysis;
class GpuTaskGraphQueueAssignments;
struct GpuTaskGraphCompileOptions;


class GpuTaskScheduler final : NoCopy{
private:
    // Admission pins the lifecycle device while recording, callbacks, submission, or a GPU wait is in progress.
    // The metadata lock is never held across those operations, preserving concurrent scheduler execution.
    class DeviceOperation final : NoCopy{
        friend class GpuTaskScheduler;


    public:
        explicit DeviceOperation(const GpuTaskScheduler& scheduler)noexcept;
        ~DeviceOperation()noexcept;


    private:
        const GpuTaskScheduler& m_scheduler;
        bool m_admitted = false;
    };

    class PreparedTimingTicketsUnwindScope;
    class SubmittingPacketUnwindScope;

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
    GpuTaskScheduler()noexcept = default;
    explicit GpuTaskScheduler(Device& device)noexcept
        : m_device(&device)
    {}


public:
    // The device owner attaches after creation and detaches before native destruction. It must stop and join producers and finish required GPU waits first
    [[nodiscard]] bool attachDevice(Device& device)noexcept;
    [[nodiscard]] bool detachDevice(Device& device)noexcept;
    [[nodiscard]] bool isAttachedTo(const Device& device)const noexcept;
    [[nodiscard]] bool isInitialized()const noexcept;
    // A detached scheduler is idle. Token waits require an attached matching device and an accepted token.
    [[nodiscard]] bool wait()const;
    [[nodiscard]] bool wait(const QueueSubmissionToken& token)const;


public:
    // Freezes queue assignment at execution admission using a current physical-queue pressure snapshot, then initializes the caller-owned runtime artifacts for the resulting immutable plan. Explicit queue loads in compileOptions override automatic timeline sampling for deterministic tooling and tests.
    [[nodiscard]] bool scheduleGraph(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena
    )const;
    [[nodiscard]] bool scheduleGraph(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphCompileOptions& compileOptions
    )const;
    // Records and executes the compiler-frozen ordinary graph prefix. Native recorder construction and queue submission remain scheduler-owned
    [[nodiscard]] bool executeGraph(
        GpuTaskGraph& graph,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        const GpuTaskGraphNormalExecutionDesc& desc,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Executes one graph-owned terminal recovery/finalization task after ordinary graph work reaches an accepted frontier.
    // The scheduler owns recorder construction and all native queue-submission details.
    [[nodiscard]] bool executeAcceptedFrontierTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Executes one semantic late task after its graph dependencies have accepted.
    // Recorded and accepted callbacks retain the task-level lifecycle contract while the scheduler owns native recording and submission.
    [[nodiscard]] bool executeTask(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback = nullptr
    )const;


private:
    [[nodiscard]] bool compileGraph(
        const GpuTaskGraph& graph,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphCompileOptions& compileOptions
    )const;
    // Records then submits the descriptor-selected ordinary compiler prefix. Without a semantic terminal task, accepted-frontier packets must form one terminal suffix. The executor rejects a frontier inside its selected prefix before recording and never discards or submits later caller-owned work on the caller's behalf.
    [[nodiscard]] bool submitGraph(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        const GpuTaskGraphNormalExecutionDesc& desc,
        GpuGraphSubmissionTransaction& transaction,
        Alloc::ScratchArena& scratchArena,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;

private:
    // Records/submits one frontier recovery/finalization task (transaction supplies queue waits; no caller token ladder). Failure rejects the unaccepted task.
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
    // Records/submits one late task post-accept: recorded validates the final-state seed, accepted publishes from the token. Rejection stays transaction-owned (no renderer retry path).
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
    [[nodiscard]] Device& device()const noexcept;
    [[nodiscard]] bool recordPacketRange(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        const GpuSubmissionPacketRange& range,
        GpuRecordedGraph& recordedGraph,
        CpuTaskScheduler* readyFrontierScheduler,
        GpuCommandIrCapture* commandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // The caller owns the transaction's exclusive SubmissionOperation. This internal path lets the accepted-frontier composite reuse the ordinary task executor without attempting forbidden same-transaction gate reentry.
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
    // Arms one graph recording attempt, publishes that exact attempt into the recorded artifact for failure cleanup, then binds its sole submission transaction while the caller retains the exclusive operation.
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
    // The caller owns one active composite submission operation for the full native-accept, task-callback, and transaction-publication sequence.
    // It supplies semantic bindings while the packet primitive stays internal.
    [[nodiscard]] bool submitPacketRangeWithinSubmissionOperation(
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
        GpuSubmissionPacketId* outFailedPacket,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks,
        usize taskAcceptedCallbackCount,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks,
        usize taskSubmissionHookCount
    )const;
    // The caller owns one valid SubmissionOperation for the full native-accept, task-callback, and transaction-publication sequence.
    // Range submission supplies its synchronous semantic obligations here
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
    // Null only between device lifetimes; ownership stays with GraphicsRuntime or the standalone caller.
    Device* m_device = nullptr;
    mutable Futex m_lifecycleMutex;
    mutable usize m_activeOperations = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

