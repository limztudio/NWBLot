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
        b8 m_admitted = false;
    };

    class PreparedTimingTicketsUnwindScope;
    class SubmittingPacketUnwindScope;

    class SubmissionAttemptExceptionFinalizer final : NoCopy{
    private:
        static thread_local SubmissionAttemptExceptionFinalizer* s_ActiveFinalizer;


    public:
        [[nodiscard]] static SubmissionAttemptExceptionFinalizer* ActiveFor(const GpuTaskGraph& graph, const GpuCompiledGraph& compiledGraph, const GpuRecordedGraph& recordedGraph, const GpuGraphSubmissionTransaction& transaction)noexcept;


    public:
        SubmissionAttemptExceptionFinalizer(GpuTaskGraph& graph, GpuGraphSubmissionTransaction& transaction, const GpuCompiledGraph& compiledGraph, const GpuRecordedGraph& recordedGraph)noexcept;
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
        b8 m_installed = false;
        b8 m_armed = false;
    };

    class SubmissionAttemptExceptionScope final : NoCopy{
    public:
        SubmissionAttemptExceptionScope(const GpuCompiledGraph& compiledGraph, const GpuRecordedGraph& recordedGraph, GpuTaskGraph& graph, GpuGraphSubmissionTransaction& transaction, GpuSubmissionPacketId* outFailedPacket = nullptr)noexcept;
        ~SubmissionAttemptExceptionScope()noexcept;


    public:
        void setFailedPacket(GpuSubmissionPacketId packet)noexcept{ m_failedPacket = packet; }

        void complete()noexcept{ m_active = false; }


    private:
        SubmissionAttemptExceptionFinalizer* m_finalizer = nullptr;
        GpuSubmissionPacketId* m_outFailedPacket = nullptr;
        GpuSubmissionPacketId m_failedPacket;
        i32 m_uncaughtExceptionCount = 0;
        b8 m_active = true;
    };


public:
    GpuTaskScheduler()noexcept = default;
    explicit GpuTaskScheduler(Device& device)noexcept
        : m_device(&device)
    {}


public:
    // Device owner attaches after creation, detaches before destruction; stop/join producers and finish GPU waits first.
    [[nodiscard]] b8 attachDevice(Device& device)noexcept;
    [[nodiscard]] b8 detachDevice(Device& device)noexcept;
    [[nodiscard]] b8 isAttachedTo(const Device& device)const noexcept;
    [[nodiscard]] b8 isInitialized()const noexcept;
    // A detached scheduler is idle. Token waits require an attached matching device and an accepted token.
    [[nodiscard]] b8 wait()const;
    [[nodiscard]] b8 wait(const QueueSubmissionToken& token)const;


public:
    // Freezes queue assignment at admission from a current pressure snapshot, then inits caller-owned artifacts for the immutable plan.
    // Explicit queue loads in compileOptions override automatic timeline sampling for deterministic tooling and tests.
    [[nodiscard]] b8 scheduleGraph(
        const GpuTaskGraph& graph,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    [[nodiscard]] b8 scheduleGraph(
        const GpuTaskGraph& graph,
        const GpuTaskGraphCompileOptions& compileOptions,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    // Records and executes the compiler-frozen ordinary graph prefix. Native recorder construction and queue submission remain scheduler-owned.
    [[nodiscard]] b8 executeGraph(
        const GpuTaskGraphNormalExecutionDesc& desc,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Executes one graph-owned terminal recovery/finalization task after ordinary graph work reaches an accepted frontier.
    // The scheduler owns recorder construction and all native queue-submission details.
    [[nodiscard]] b8 executeAcceptedFrontierTask(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Executes one semantic late task after its graph dependencies have accepted.
    // Recorded and accepted callbacks retain the task-level lifecycle contract while the scheduler owns native recording and submission.
    [[nodiscard]] b8 executeTask(
        const GpuCompiledGraph& compiledGraph,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder* timingRecorder,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;


private:
    [[nodiscard]] b8 compileGraph(
        const GpuTaskGraph& graph,
        const GpuTaskGraphCompileOptions& compileOptions,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments,
        GpuCompiledGraph& compiledGraph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    // Records/submits the selected ordinary prefix. Without a terminal task, frontier packets must form one terminal suffix.
    // The executor rejects a frontier inside its prefix before recording; it never touches later caller-owned work.
    [[nodiscard]] b8 submitGraph(
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        const GpuTaskGraphNormalExecutionDesc& desc,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;


private:
    // Records/submits one frontier recovery/finalization task. Failure rejects the unaccepted task.
    [[nodiscard]] b8 recordAndSubmitAcceptedFrontierTask(
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuTaskId task,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;
    // Records/submits one late task post-accept: recorded checks the final-state seed, accepted publishes from the token.
    [[nodiscard]] b8 recordAndSubmitTask(
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket = nullptr
    )const;


private:
    [[nodiscard]] Device& device()const noexcept;
    [[nodiscard]] b8 recordPacketRange(
        const GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        const GpuSubmissionPacketRange& range,
        CpuTaskScheduler* readyFrontierScheduler,
        Alloc::ScratchArena& scratchArena,
        GpuRecordedGraph& recordedGraph,
        GpuCommandIrCapture* commandIrCapture,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // Caller-owned exclusive SubmissionOperation; this path reuses the executor without same-transaction gate reentry.
    [[nodiscard]] b8 recordAndSubmitTaskWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuNativePacketRecorder& recorder,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback* recordedCallback,
        const GpuTaskGraphTaskAcceptedCallback* acceptedCallback,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // Arms one recording attempt and binds its transaction while the caller keeps the exclusive operation.
    [[nodiscard]] b8 prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTaskGraph& graph,
        GpuTimingRecorder* timingRecorder,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    // The caller owns one active composite submission operation for the full native-accept, task-callback, and transaction-publication sequence.
    // It supplies semantic bindings while the packet primitive stays internal.
    [[nodiscard]] b8 submitPacketRangeWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketRange& range,
        const GpuTaskGraphTaskTimingTicket* taskTimingTickets,
        usize taskTimingTicketCount,
        const GpuTaskGraphTaskAcceptedCallback* taskAcceptedCallbacks,
        usize taskAcceptedCallbackCount,
        const GpuTaskGraphTaskSubmissionHook* taskSubmissionHooks,
        usize taskSubmissionHookCount,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    // The caller owns one valid SubmissionOperation for the full native-accept, task-callback, and transaction-publication sequence.
    // Range submission supplies its synchronous semantic obligations here.
    [[nodiscard]] b8 submitPacketWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketId& packetID,
        usize timingTicketCount,
        const QueueSubmissionPreSubmitHook* preSubmitHook,
        const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks,
        Alloc::ScratchArena& scratchArena,
        const NotNull<GpuTimingSubmissionTicket*>* timingTickets,
        GpuTaskGraph& graph,
        GpuGraphSubmissionTransaction& transaction
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

