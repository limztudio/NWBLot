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

namespace GpuTaskSubmissionDetail{
    struct TaskSubmissionBindings;
};


class GpuTaskScheduler final : NoCopy{
private:
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
    struct BatchEntryState;
    class BatchEntryBuffer;
    class BatchOwnershipBuffer;
    class BatchPreparedTimingTicketsUnwindScope;
    class BatchSubmittingPacketsUnwindScope;

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
    [[nodiscard]] b8 attachDevice(Device& device)noexcept;
    [[nodiscard]] b8 detachDevice(Device& device)noexcept;
    [[nodiscard]] b8 isAttachedTo(const Device& device)const noexcept;
    [[nodiscard]] b8 isInitialized()const noexcept;
    [[nodiscard]] b8 wait()const;
    [[nodiscard]] b8 wait(const QueueSubmissionToken& token)const;


public:
    [[nodiscard]] b8 scheduleGraph(
        const GpuTaskGraph& graph, const GpuTaskGraphCompileOptions& options, GpuTaskGraphAnalysis& analysis,
        GpuTaskGraphQueueAssignments& assignments, GpuCompiledGraph& compiled, GpuRecordedGraph& recorded, GpuGraphSubmissionTransaction& transaction
    )const;
    [[nodiscard]] b8 executeGraph(
        const GpuTaskGraphNormalExecutionDesc& desc, GpuTaskGraph& graph,
        GpuCompiledGraph& compiled, GpuRecordedGraph& recorded, GpuGraphSubmissionTransaction& transaction
    )const;
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

    [[nodiscard]] b8 executeGraphWithTiming(
        const GpuTaskGraphNormalExecutionDesc& desc,
        GpuTaskGraph& graph,
        GpuCompiledGraph& compiled,
        GpuRecordedGraph& recorded,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder& timing
    )const;
    [[nodiscard]] b8 executeAcceptedFrontierTask(
        const GpuCompiledGraph& compiled,
        GpuTaskId task,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recorded,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder& timing
    )const;
    [[nodiscard]] b8 executeTask(
        const GpuCompiledGraph& compiled,
        GpuTaskId task,
        const GpuTaskGraphTaskRecordedCallback& onRecorded,
        const GpuTaskGraphTaskAcceptedCallback& onAccepted,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recorded,
        GpuGraphSubmissionTransaction& transaction,
        GpuTimingRecorder& timing
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
    [[nodiscard]] b8 submitGraphWithinDeviceOperation(
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
    [[nodiscard]] b8 prepareRecordingAttemptAndBindTransactionWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuTimingRecorder* timingRecorder,
        GpuTaskGraph& graph,
        GpuRecordedGraph& recordedGraph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    [[nodiscard]] b8 submitPacketRangeWithinSubmissionOperation(
        const GpuCompiledGraph& compiledGraph,
        const GpuRecordedGraph& recordedGraph,
        const GpuSubmissionPacketRange& range,
        Span<const GpuTaskGraphTaskTimingTicket> taskTimingTickets,
        Span<const GpuTaskGraphTaskAcceptedCallback> taskAcceptedCallbacks,
        Span<const GpuTaskGraphTaskSubmissionHook> taskSubmissionHooks,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket
    )const;
    [[nodiscard]] b8 submitPacketWithinSubmissionOperation(
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        const GpuSubmissionPacketId& packetID,
        Span<const NotNull<GpuTimingSubmissionTicket*>> timingTickets,
        const QueueSubmissionPreSubmitHook* preSubmitHook,
        const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuGraphSubmissionTransaction& transaction
    )const;
    [[nodiscard]] b8 submitPacketBatchWithinSubmissionOperation(
        const GpuTaskGraph::DeclarationReadView& declarationAccess,
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuRecordedGraph& recordedGraph,
        const GpuRecordedGraph::ArtifactOperation& artifactAccess,
        usize batchStart,
        usize batchEnd,
        const GpuPhysicalQueueId& batchQueue,
        const GpuTaskSubmissionDetail::TaskSubmissionBindings& bindings,
        const GpuTaskSubmissionDetail::TaskCallbackBindings<GpuTaskGraphTaskAcceptedCallback>& taskAcceptedCallbacks,
        Alloc::ScratchArena& scratchArena,
        GpuTaskGraph& graph,
        GpuGraphSubmissionTransaction& transaction,
        GpuSubmissionPacketId* outFailedPacket,
        b8 packetsValidatedReady = false
    )const;


private:
    Device* m_device = nullptr;
    mutable Futex m_lifecycleMutex;
    mutable usize m_activeOperations = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

