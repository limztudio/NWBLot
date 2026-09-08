// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "packet_runtime.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    GpuTaskScheduler()noexcept = default;
    explicit GpuTaskScheduler(Device& device)noexcept
        : m_device(&device)
    {}


public:
    // The device owner attaches after creation and detaches before native destruction. It must stop and join
    // producers and finish required GPU waits first; detach rejects active operations without changing the binding.
    [[nodiscard]] bool attachDevice(Device& device)noexcept;
    [[nodiscard]] bool detachDevice(Device& device)noexcept;
    [[nodiscard]] bool isAttachedTo(const Device& device)const noexcept;
    [[nodiscard]] bool isInitialized()const noexcept;
    // A detached scheduler is idle. Token waits require an attached matching device and an accepted token.
    [[nodiscard]] bool wait()const;
    [[nodiscard]] bool wait(const QueueSubmissionToken& token)const;


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
    [[nodiscard]] bool submit(
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
        CpuTaskScheduler& cpuScheduler,
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
    [[nodiscard]] bool recordAndSubmitTaskRange(
        GpuTaskGraph& graph,
        const GpuCompiledGraph& compiledGraph,
        const GpuNativePacketRecorder& recorder,
        GpuRecordedGraph& recordedGraph,
        CpuTaskScheduler* readyFrontierScheduler,
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
    // Null only between device lifetimes; ownership stays with GraphicsRuntime or the standalone caller.
    Device* m_device = nullptr;
    mutable Futex m_lifecycleMutex;
    mutable usize m_activeOperations = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

