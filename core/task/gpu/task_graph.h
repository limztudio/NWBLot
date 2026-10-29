// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_contract.h"
#include "task_desc.h"
#include "task_graph_declaration_view.h"
#include "task_graph_nodes.h"
#include "task_graph_packet_claims.h"
#include "task_graph_submission_binding.h"

#include <core/alloc/scratch.h>

#include <global/arena_object.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuCompiledBarrier;

class GpuNativePacketRecorder;
class GpuGraphSubmissionTransaction;
class GpuRecordedGraph;
class GpuTaskScheduler;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph final : NoCopy{
    friend class GpuTaskGraphDeclarationReadView;
    friend class GpuRecordingAttemptScope;
    friend class GpuPacketRecordingLease;
    friend class GpuTaskGraphCompiler;
    friend class GpuTaskGraphQueueAssignmentTelemetryTracker;
    friend class GpuNativePacketRecorder;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuRecordedGraph;
    friend class GpuTaskScheduler;


public:
    using DeclarationReadView = GpuTaskGraphDeclarationReadView;


    // Declare owner after every object borrowed by task payloads. It retires CPU payloads after sync recording joins.
    // Reset the graph before another execution; every declaration/read/packet claim must finish before this scope ends.
    class PayloadLifetimeScope final : NoCopy{
    public:
        explicit PayloadLifetimeScope(GpuTaskGraph& graph)noexcept
            : m_graph(graph)
        {}
        ~PayloadLifetimeScope()noexcept(false);


    private:
        GpuTaskGraph& m_graph;
    };


private:
    struct RecordTaskFailure final{
        b8 recordThunkInvoked;
    };

    class RecordThunkClaimGuard;
    class RecordingAttemptResolutionGuard;

    template<typename PayloadT>
    using ProvisionalPayloadOwner = GpuTaskGraphDetail::ProvisionalPayloadOwner<PayloadT>;
    using TaskLifecycleState = GpuTaskGraphDetail::TaskLifecycleState;
    using SubmissionBindingState = GpuTaskGraphDetail::SubmissionBindingState;

    class DeclarationMutationScope final : NoCopy{
    public:
        explicit DeclarationMutationScope(GpuTaskGraph& graph);
        ~DeclarationMutationScope()noexcept;


    public:
        [[nodiscard]] b8 valid()const noexcept{ return m_valid; }

        [[nodiscard]] b8 validFor(const GpuTaskGraph& graph)const noexcept{ return m_valid && &m_graph == &graph; }


    private:
        GpuTaskGraph& m_graph;
        UniqueLock<RecursiveMutex> m_declarationLock;
        b8 m_valid = false;
    };

    class DiscardNotificationScope final : NoCopy{
    public:
        explicit DiscardNotificationScope(const GpuTaskGraph& graph)noexcept;
        ~DiscardNotificationScope()noexcept;


    public:
        void complete()noexcept;
        void activateWithinLock()noexcept;
        void adoptPacketRecordingClaimWithinLock()noexcept;


    private:
        const GpuTaskGraph& m_graph;
        b8 m_active = false;
        b8 m_ownsPacketRecordingClaim = false;
    };

    class TaskPayloadDestroyScope final : NoCopy{
    public:
        explicit TaskPayloadDestroyScope(GpuTaskGraph& graph)noexcept;
        ~TaskPayloadDestroyScope()noexcept;


    public:
        void activateWithinLock()noexcept;


    private:
        GpuTaskGraph& m_graph;
        b8 m_active = false;
    };

    class ResetCompletionScope final : NoCopy{
    public:
        explicit ResetCompletionScope(GpuTaskGraph& graph)noexcept;
        ~ResetCompletionScope()noexcept;


    public:
        void activateWithinLock()noexcept;
        void complete()noexcept;


    private:
        GpuTaskGraph& m_graph;
        b8 m_active = false;
    };



private:
    using GpuTaskNode = GpuTaskGraphDetail::GpuTaskNode;
    using GpuGraphResourceNode = GpuTaskGraphDetail::GpuGraphResourceNode;
    using ResourcePointerKey = GpuTaskGraphDetail::ResourcePointerKey;
    using ResourcePointerEqual = GpuTaskGraphDetail::ResourcePointerEqual;
    using ResourceImportMatch = GpuTaskGraphDetail::ResourceImportMatch;
    using ResourceBinding = GpuTaskGraphDetail::ResourceBinding;
    using GpuGraphResourceVersionNode = GpuTaskGraphDetail::GpuGraphResourceVersionNode;
    using GpuGraphResourceSetNode = GpuTaskGraphDetail::GpuGraphResourceSetNode;
    using GpuGraphPipelineNode = GpuTaskGraphDetail::GpuGraphPipelineNode;
    using PipelinePointerKey = GpuTaskGraphDetail::PipelinePointerKey;
    using PipelinePointerEqual = GpuTaskGraphDetail::PipelinePointerEqual;
    using PipelineImportMatch = GpuTaskGraphDetail::PipelineImportMatch;
    using PipelineBinding = GpuTaskGraphDetail::PipelineBinding;
    using GpuExternalCompletionNode = GpuTaskGraphDetail::GpuExternalCompletionNode;
    using GpuUploadBlobNode = GpuTaskGraphDetail::GpuUploadBlobNode;
    using MarkerLabelRange = GpuTaskGraphDetail::MarkerLabelRange;

    struct ResourcePointerHasher{
        [[nodiscard]] usize operator()(const ResourcePointerKey& key)const noexcept;
    };

    struct PipelinePointerHasher{
        [[nodiscard]] usize operator()(const PipelinePointerKey& key)const noexcept;
    };


private:
    using ImportIdentityIndex = HashMap<NameHash, u32, GraphicsArena>;
    using ResourcePointerIndex = HashMap<ResourcePointerKey, u32, GraphicsArena, ResourcePointerHasher, ResourcePointerEqual>;
    using PipelinePointerIndex = HashMap<PipelinePointerKey, u32, GraphicsArena, PipelinePointerHasher, PipelinePointerEqual>;


private:
    static constexpr usize s_InlineImportIndexCount = 32u;
    static constexpr usize s_InitialPointerIndexBucketCount = 2u;
    static constexpr u32 s_InvalidImportIndex = Limit<u32>::s_Max;



private:
    [[nodiscard]] static u64 AllocateGeneration()noexcept;
    [[nodiscard]] static ResourcePointerKey MakeResourcePointerKey(const GpuGraphResourceNode& resource)noexcept;
    [[nodiscard]] static PipelinePointerKey MakePipelinePointerKey(const GpuGraphPipelineNode& pipeline)noexcept;


private:
    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolvePacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet)noexcept{
        if(!planAccess.validFor(compiledGraph))
            return MakeUnexpected(Failure{});
        const GpuCompiledPacketView packetView = planAccess.packetWithTasks(packet);
        if(!packetView.valid())
            return MakeUnexpected(Failure{});
        return packetView;
    }

    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolveLeasedPacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet, const GpuPacketRecordingLease& lease)noexcept{
        const auto packetView = ResolvePacketView(compiledGraph, planAccess, packet);
        if(!packetView || !lease.valid() || lease.m_packet != packet || lease.m_planGeneration != planAccess.planGeneration())
            return MakeUnexpected(Failure{});
        return *packetView;
    }

    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolveAttemptPacketView(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuSubmissionPacketId packet,
        const u64 recordingAttemptGeneration,
        const b8 leaseValid
    )noexcept{
        const auto packetView = ResolvePacketView(compiledGraph, planAccess, packet);
        if(!packetView || recordingAttemptGeneration == 0u || leaseValid)
            return MakeUnexpected(Failure{});
        return *packetView;
    }


private:
    template<typename TaskT>
    static b8 RecordTaskPayload(const void* const payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept(IsSame_V<decltype(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context)), b8>
        && noexcept(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context))){
        using Payload = typename TaskT::Payload;
        return TaskT::Record(*MakeNotNull(static_cast<const Payload*>(payload)), commandList, context);
    }

    template<typename TaskT>
    static void AcceptTaskPayload(void* const payload, const QueueSubmissionToken& token)noexcept(noexcept(TaskT::Accepted(*static_cast<typename TaskT::Payload*>(payload), token))){
        using Payload = typename TaskT::Payload;
        TaskT::Accepted(*MakeNotNull(static_cast<Payload*>(payload)), token);
    }

    template<typename TaskT>
    static void DiscardTaskPayload(void* const payload)noexcept(noexcept(TaskT::Discarded(*static_cast<typename TaskT::Payload*>(payload)))){
        using Payload = typename TaskT::Payload;
        TaskT::Discarded(*MakeNotNull(static_cast<Payload*>(payload)));
    }

    template<typename PayloadT>
    static void DestroyTaskPayload(GraphicsArena& arena, void* payload)noexcept{
        static_assert(noexcept(DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload))));
        DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload));
    }


public:
    explicit GpuTaskGraph(GraphicsArena& arena);
    ~GpuTaskGraph()noexcept(false);


public:
    // Metadata-only tasks support analysis. Tasks run through scheduler recording need a payload thunk via the templated overload.
    [[nodiscard]] Expected<GpuTaskId> addTask(const GpuTaskDesc& desc, const GpuTaskCommandRequirements& commands = {});
    // The first isolated primary-Graphics task gates later normal roots without ordering independent branches.
    [[nodiscard]] b8 setNormalExecutionPrelude(const GpuTaskId& task);

    // Adds a buffer copy. The helper derives requirements and uses from its regions and retains the buffers. Caller passes no uses.
    [[nodiscard]] Expected<GpuTaskId> addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc);

    // Adds a texture copy. The helper derives requirements and uses from its regions and retains the textures. Caller passes no uses.
    [[nodiscard]] Expected<GpuTaskId> addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc);

    // Adds a texture resolve. The helper derives Graphics requirements and uses, retaining the textures. Caller passes no uses.
    [[nodiscard]] Expected<GpuTaskId> addResolveTextureTask(const GpuTaskDesc& desc, const GpuResolveTextureTaskDesc& resolveDesc);

    // Copies caller bytes into graph-owned storage. `alignment` is a nonzero power of two; upload tasks resolve the blob while recording.
    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadData(const void* data, usize byteSize, usize alignment = alignof(u8));

    // Adds graph-owned buffer/texture uploads.
    // Ordered uses expose the native CopyDest write and optional final transition; preflight and later packets observe the complete state contract.
    [[nodiscard]] Expected<GpuTaskId> addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc);
    [[nodiscard]] Expected<GpuTaskId> addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc);

    // Adds a native uint-buffer clear. The helper retains the buffer and derives Transfer requirements and its CopyDest write.
    [[nodiscard]] Expected<GpuTaskId> addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc);

    // Adds a texture clear. The helper retains the texture and derives its CopyDest write and requirements.
    [[nodiscard]] Expected<GpuTaskId> addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc);

    // Adds a rectangular uint texture clear. The helper retains the texture and derives its subresource CopyDest write.
    [[nodiscard]] Expected<GpuTaskId> addClearTextureRectUIntTask(const GpuTaskDesc& desc, const GpuClearTextureRectUIntTaskDesc& clearDesc);

    template<typename TaskT>
    [[nodiscard]] Expected<GpuTaskId> addTask(const GpuTaskDesc& desc, typename TaskT::Payload&& payload){
        using Payload = typename TaskT::Payload;
        static_assert(IsNothrowDestructible_V<Payload>, "GPU task payload cleanup must remain no-throw");

        DeclarationMutationScope mutation(*this);
        if(!mutation.valid())
            return MakeUnexpected(Failure{});

        Payload* const payloadObject = NewArenaObject<Payload>(m_arena, Move(payload));
        if(!payloadObject)
            return MakeUnexpected(Failure{});
        ProvisionalPayloadOwner<Payload> storedPayload(m_arena, payloadObject);

        GpuTaskRecordThunk recordPayload = nullptr;
        if constexpr(GpuGraphTaskContract::RecordApi<TaskT>)
            recordPayload = &RecordTaskPayload<TaskT>;

        GpuTaskAcceptedThunk acceptPayload = nullptr;
        if constexpr(GpuGraphTaskContract::AcceptedApi<TaskT>)
            acceptPayload = &AcceptTaskPayload<TaskT>;

        GpuTaskDiscardedThunk discardPayload = nullptr;
        if constexpr(GpuGraphTaskContract::DiscardedApi<TaskT>)
            discardPayload = &DiscardTaskPayload<TaskT>;

        const auto task = appendTaskWithinMutation(desc, GpuGraphTaskContract::CommandRequirements<TaskT>(*storedPayload.get()), recordPayload, acceptPayload, discardPayload, &DestroyTaskPayload<Payload>, sizeof(Payload), mutation, storedPayload.get());
        if(task)
            storedPayload.publish();
        else
            discardAndDestroyUnappendedPayload(storedPayload.release(), discardPayload, &DestroyTaskPayload<Payload>);
        return task;
    }

    template<typename TaskT>
    Expected<GpuTaskId> rejectInvalidBuiltinPayload(ProvisionalPayloadOwner<typename TaskT::Payload>& payload){
        using Payload = typename TaskT::Payload;
        discardAndDestroyUnappendedPayload(payload.release(), &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>);
        return MakeUnexpected(Failure{});
    }

    template<typename TaskT>
    [[nodiscard]] Expected<GpuTaskId> appendBuiltinTaskWithinMutation(
        ProvisionalPayloadOwner<typename TaskT::Payload>& payload,
        const GpuTaskDesc& resolvedDesc,
        const DeclarationMutationScope& mutation,
        const GpuTaskCommandRequirements& commands = { .requiredCapabilities = GpuQueueCapability::Transfer }
    ){
        using Payload = typename TaskT::Payload;
        const auto task = appendTaskWithinMutation(resolvedDesc, commands, &RecordTaskPayload<TaskT>, &AcceptTaskPayload<TaskT>, &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>, sizeof(Payload), mutation, payload.get());
        if(task)
            payload.publish();
        else
            discardAndDestroyUnappendedPayload(payload.release(), &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>);
        return task;
    }

    // This form covers abstract resources and conservative bindless hazard domains during the metadata-only phase.
    // Tasks that will be recorded later must use a typed import overload so the graph retains the resource.
    [[nodiscard]] Expected<GpuGraphResourceId> importResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importAccelStruct(const RayTracingAccelStructHandle& accelStruct, const GpuGraphResourceDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceId> importHazardDomain(const GpuGraphResourceDesc& desc);
    // One exact resource range gets one version ID; repeating a descriptor makes a new ID on purpose.
    [[nodiscard]] Expected<GpuGraphResourceVersionId> declareResourceVersion(const GpuGraphResourceVersionDesc& desc);
    // Stores an immutable resource collection. Set declarations expand to members at task creation.
    [[nodiscard]] Expected<GpuGraphResourceSetId> importResourceSet(const GpuGraphResourceSetDesc& desc);
    // Pipeline IDs are graph-local side-table entries. Metadata-only entries support analysis and capture setup.
    [[nodiscard]] Expected<GpuGraphPipelineId> importPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importGraphicsPipeline(const GraphicsPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importComputePipeline(const ComputePipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importMeshletPipeline(const MeshletPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importRayTracingPipeline(const RayTracingPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuExternalCompletionId> importExternalCompletion(const GpuExternalCompletionDesc& desc);
    // One graph publishes one present endpoint. The compiler checks backbuffer, single-sink contract, deps, and routing.
    [[nodiscard]] b8 declarePresentEndpoint(const GpuPresentEndpoint& endpoint);

    // Reset is externally serialized against *starting* native recording/submission entrypoints.
    // A bound partial attempt and every transient packet claim refuse teardown; callers must resolve the owner before retrying.
    [[nodiscard]] b8 tryReset();
    void reset();


private:
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    // Starts or validates one native-recording attempt for the compiler-owned packet.
    // A retry can begin only after every task from the previous attempt was discarded; accepted-frontier recovery remains in that same attempt.
    [[nodiscard]] b8 beginRecordingAttempt(const GpuCompiledGraph& compiledGraph, GpuSubmissionPacketId packet, const DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, GpuRecordingAttemptScope& outAttempt)const noexcept;
    void cancelRecordingAttempt(GpuRecordingAttemptScope& attempt)const noexcept;
    void completeRecordingPreparation(GpuRecordingAttemptScope& attempt)const noexcept;
    [[nodiscard]] b8 matchesRecordingAttempt(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 resolveRecordingAttemptIfTerminal(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] b8 bindSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding, const GpuRecordingAttemptScope* preparationAttempt = nullptr)const noexcept;
    [[nodiscard]] b8 matchesSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 resolveSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 beginSubmissionExceptionClosing(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] b8 waitForSubmissionExceptionRecordingClaims(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;

private:
    // Caller holds m_lifecycleMutex; preparation, submission binding and task-state checks remain path-specific.
    [[nodiscard]] b8 matchesPacketAttemptWithinLock(const GpuCompiledGraph& compiledGraph, const u64 planGeneration, const u64 recordingAttemptGeneration)const noexcept{
        return !m_teardownInProgress && m_activeCompiledGraph == &compiledGraph && m_activeRecordingPlanGeneration == planGeneration && m_activeRecordingAttemptGeneration == recordingAttemptGeneration;
    }

    [[nodiscard]] Expected<void, RecordTaskFailure> recordTask(const GpuTaskId& taskID, CommandList& commandList, const GpuTaskRecordContext& context, const GpuPacketRecordingLease& lease)const;

    // Caller owns teardown/binding validation, lease reset, and claim release; task mutation holds the lifecycle lock.
    void discardPacketTasksWithinLock(const GpuSubmissionPacket& packetPlan, const GpuTaskId* const tasks, const u64 notificationGeneration = 0u)const noexcept{
        for(usize taskIndex = 0u; taskIndex < packetPlan.taskCount; ++taskIndex){
            const GpuTaskNode& task = m_tasks[tasks[taskIndex].index];
            task.lifecycleState = TaskLifecycleState::Discarded;
            task.recordingClaimGeneration = 0u;
            task.submissionClaimGeneration = 0u;
            task.discardNotificationGeneration = notificationGeneration;
            task.recordThunkInProgress = false;
            task.recordThunkCompleted = false;
        }
    }

    // Claim packet tasks before recording. One artifact per packet per attempt; the lease names the recorder.
    [[nodiscard]] b8 beginPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration, GpuPacketRecordingLease& outLease)const noexcept;
    // A claimed packet becomes submission-eligible only after all its thunks complete.
    [[nodiscard]] b8 completePacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    // Transfers a failed recorder's exact claim without invoking user code or changing task lifecycle on its worker.
    [[nodiscard]] b8 deferPacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease, GpuPacketRecordingAbort& outAbort)const noexcept;
    // Invokes typed discard callbacks and finalizes one transferred abort. Callers choose the serial drain order.
    [[nodiscard]] b8 completePacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const;
    // Exception unwind consumes exact recording capabilities without invoking extensible discard observers.
    [[nodiscard]] b8 abandonPacketRecordingWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    [[nodiscard]] b8 abandonPacketRecordingAbortWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const noexcept;
    // Ordinary cleanup cannot discard a Recording packet: a retry must not re-arm payload while the original recorder owns it.
    void abortPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const;
    [[nodiscard]] b8 packetReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration)const noexcept;

private:
    [[nodiscard]] b8 beginPacketSubmission(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding,
        GpuPacketSubmissionLease& outLease
    )const noexcept;
    void beginPacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, const QueueSubmissionToken& token, GpuPacketSubmissionLease& lease)const noexcept;
    void notifyPacketSubmissionAccepted(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, const QueueSubmissionToken& token, const GpuPacketSubmissionLease& lease)const;
    void completePacketSubmissionAcceptance(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept;
    void abortPacketSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const;
    [[nodiscard]] b8 abandonPacketSubmissionWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept;
    // Atomically discards one non-recording packet.
    // A packet with an in-flight Recording claim is left intact so transaction cancellation cannot race native recording or reopen the graph.
    [[nodiscard]] b8 discardUnacceptedPacket(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const;
    // Unexpected observer failure must resolve the graph-owned attempt without invoking another callback while the original exception is active.
    // Payload destruction remains owned by normal graph teardown.
    [[nodiscard]] b8 abandonUnacceptedPacketWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;

private:
    // Lowers a compiler-owned packet-boundary barrier through the existing state tracker. Task thunks own only barriers internal to their sequence.
    [[nodiscard]] b8 applyCompiledBarrier(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    // Seeds retained states after barriers lower, keeping packet handoffs when the required state already matches.
    [[nodiscard]] b8 seedTaskRetainedResourceStates(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingAccess& recordingAccess, const GpuTaskId& taskID, CommandList& commandList)const;

private:
    [[nodiscard]] b8 validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] b8 validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] b8 validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] b8 validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept;
    [[nodiscard]] b8 validResourceSet(const GpuGraphResourceSetId& id)const noexcept;
    [[nodiscard]] b8 validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] b8 validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] b8 validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceVersionView resourceVersionAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceSetView resourceSetAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const noexcept;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(const GpuExternalCompletionId& completion)const noexcept;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Expected<BinaryByteView> uploadBlobData(const GpuUploadBlobId& blob)const noexcept;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] MeshletPipeline* meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] RayTracingPipeline* rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;

    [[nodiscard]] Expected<GpuTaskId> appendTaskWithinMutation(
        const GpuTaskDesc& desc,
        const GpuTaskCommandRequirements& commands,
        GpuTaskRecordThunk recordPayload,
        GpuTaskAcceptedThunk acceptPayload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload,
        usize payloadObjectSize,
        const DeclarationMutationScope& mutationAccess,
        void* payload
    );
    void discardAndDestroyUnappendedPayload(void* payload, GpuTaskDiscardedThunk discardPayload, GpuTaskPayloadDestroyThunk destroyPayload);
    [[nodiscard]] u32 findResourceIdentity(const Name& identity)const noexcept;
    // A null identity requests only the first matching typed pointer, without import metadata validation.
    [[nodiscard]] ResourceImportMatch findResourceImportMatch(const NameHash* identity, const ResourcePointerKey& pointer)const noexcept;
    [[nodiscard]] u32 findResourceSetIdentity(const Name& identity)const noexcept;
    [[nodiscard]] u32 findPipelineIdentity(const Name& identity)const noexcept;
    [[nodiscard]] PipelineImportMatch findPipelineImportMatch(const Name& identity, const PipelinePointerKey& pointer)const noexcept;
    [[nodiscard]] u32 findExternalCompletionIdentity(const Name& identity)const noexcept;
    void prepareResourceIndexes(const ResourcePointerKey& pendingPointer);
    void prepareResourceSetIndex();
    void preparePipelineIndexes(const PipelinePointerKey& pendingPointer);
    void prepareExternalCompletionIndex();
    [[nodiscard]] Expected<GpuGraphResourceId> appendResourceWithinMutation(const GpuGraphResourceDesc& desc, const ResourceQueueAdmissionSnapshot* queueAdmission, const ResourceBinding& binding, const DeclarationMutationScope& mutationAccess);
    [[nodiscard]] Expected<GpuGraphResourceVersionId> appendResourceVersion(const GpuGraphResourceVersionDesc& desc);
    [[nodiscard]] Expected<GpuGraphResourceSetId> appendResourceSet(const GpuGraphResourceSetDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> appendPipeline(const GpuGraphPipelineDesc& desc, const PipelineBinding& binding);
    [[nodiscard]] Expected<GpuExternalCompletionId> appendExternalCompletion(const GpuExternalCompletionDesc& desc);
    [[nodiscard]] const GpuUploadBlobNode* findUploadBlob(const GpuUploadBlobId& blob)const noexcept;


    [[nodiscard]] MarkerLabelRange appendMarkerLabel(AStringView text);
    [[nodiscard]] AStringView markerLabel(u32 offset, u32 size)const noexcept;
    [[nodiscard]] b8 retireTaskPayloads();
    [[nodiscard]] b8 destroyTaskPayloads();
    [[nodiscard]] b8 destroyTaskPayloadsWithoutCallbacks()noexcept;
    void releasePacketRecordingClaimWithinLock()const noexcept;
    void destroyTaskPayloadObjects()noexcept;
    void destroyTaskStateSnapshots()noexcept;
    void destroyResourceStateSnapshots()noexcept;
    void completeResetWithoutCallbacks()noexcept;


private:
    GraphicsArena& m_arena;
    Alloc::ScratchArena m_declarationScratch;
    mutable RecursiveMutex m_declarationMutex;
    mutable Futex m_lifecycleMutex;
    GraphicsVector<GpuTaskNode> m_tasks;
    GraphicsVector<GpuTaskId> m_dependencies;
    GpuTaskId m_normalExecutionPrelude;
    GraphicsVector<GpuExternalCompletionId> m_externalDependencies;
    GraphicsVector<GpuTaskExternalStateSource> m_externalStateSources;
    GraphicsVector<NotNull<CommandListResourceStateHandoff*>> m_externalStateSnapshots;
    GraphicsVector<GpuTaskResourceUse> m_resourceUses;
    GraphicsVector<GpuTaskResourceVersionUse> m_resourceVersionUses;
    GraphicsVector<GpuGraphResourceNode> m_resources;
    // Ordinals refer to immutable declarations in the current generation. Small graphs use their bounded existing node prefix.
    Optional<ImportIdentityIndex> m_resourceIdentityIndex;
    Optional<ResourcePointerIndex> m_resourcePointerIndex;
    GraphicsVector<GpuGraphResourceVersionNode> m_resourceVersions;
    GraphicsVector<GpuTaskGraphInitialOwnerHandoffSourceView> m_initialOwnerHandoffSources;
    GraphicsVector<NotNull<CommandListResourceStateHandoff*>> m_initialOwnerHandoffStateSnapshots;
    GraphicsVector<u32> m_queueFamilyIndices;
    GraphicsVector<GpuGraphResourceSetNode> m_resourceSets;
    Optional<ImportIdentityIndex> m_resourceSetIdentityIndex;
    GraphicsVector<GpuGraphResourceId> m_resourceSetMembers;
    GraphicsVector<GpuGraphPipelineNode> m_pipelines;
    Optional<ImportIdentityIndex> m_pipelineIdentityIndex;
    Optional<PipelinePointerIndex> m_pipelinePointerIndex;
    GraphicsVector<GpuExternalCompletionNode> m_externalCompletions;
    Optional<ImportIdentityIndex> m_externalCompletionIdentityIndex;
    GraphicsVector<GpuUploadBlobNode> m_uploadBlobs;
    GraphicsBytes m_markerText;
    GpuPresentEndpoint m_presentEndpoint;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    mutable u64 m_activeRecordingAttemptGeneration = 0u;
    mutable u64 m_activeRecordingPlanGeneration = 0u;
    mutable u64 m_activeRecordingPreparationSerial = 0u;
    mutable const GpuCompiledGraph* m_activeCompiledGraph = nullptr;
    mutable GpuGraphSubmissionBinding m_activeSubmissionBinding;
    mutable u32 m_activeDeclarationAccessCount = 0u;
    mutable u32 m_activeDeclarationReadCount = 0u;
    mutable u32 m_activeDiscardNotificationCount = 0u;
    mutable Atomic<u32> m_activePacketRecordingClaimCount{ 0u };
    mutable SubmissionBindingState::Enum m_submissionBindingState = SubmissionBindingState::None;
    u16 m_externalCompletionDeviceGeneration = 0u;
    b8 m_hasPresentEndpoint = false;
    mutable b8 m_teardownInProgress = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

