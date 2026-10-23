// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_contract.h"
#include "task_desc.h"
#include "task_graph_declaration_view.h"
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


    // Declare this operation owner after every object borrowed by its task payloads.
    // It retires CPU payloads after synchronous recording/submission has joined, while keeping graph declarations, resource owners and diagnostic metadata available.
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
        bool recordThunkInvoked;
    };

    class RecordThunkClaimGuard;
    class RecordingAttemptResolutionGuard;

    template<typename PayloadT>
    class ProvisionalPayloadOwner final : NoCopy{
        static_assert(IsNothrowDestructible_V<PayloadT>);


    public:
        ProvisionalPayloadOwner(GraphicsArena& arena, PayloadT* const payload)noexcept
            : m_arena(arena)
            , m_payload(payload)
        {}
        ~ProvisionalPayloadOwner()noexcept{
            DestroyArenaObjectNoexcept(m_arena, m_payload);
        }


    public:
        [[nodiscard]] PayloadT& operator*()const noexcept{ return *m_payload; }

        [[nodiscard]] PayloadT* operator->()const noexcept{ return m_payload; }


    public:
        [[nodiscard]] PayloadT* get()const noexcept{ return m_payload; }

        [[nodiscard]] PayloadT* release()noexcept{
            PayloadT* const payload = m_payload;
            m_payload = nullptr;
            return payload;
        }

        void publish()noexcept{ m_payload = nullptr; }


    private:
        GraphicsArena& m_arena;
        PayloadT* m_payload = nullptr;
    };

    enum class TaskLifecycleState : u8{
        Declared,
        Recording,
        Recorded,
        Submitting,
        Accepting,
        Accepted,
        Discarded,
    };

    enum class SubmissionBindingState : u8{
        None,
        Active,
        ExceptionClosing,
        Resolved,
    };

    class DeclarationMutationScope final : NoCopy{
    public:
        explicit DeclarationMutationScope(GpuTaskGraph& graph);
        ~DeclarationMutationScope()noexcept;


    public:
        [[nodiscard]] bool valid()const noexcept{ return m_valid; }

        [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept{ return m_valid && &m_graph == &graph; }


    private:
        GpuTaskGraph& m_graph;
        UniqueLock<RecursiveMutex> m_declarationLock;
        bool m_valid = false;
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
        bool m_active = false;
        bool m_ownsPacketRecordingClaim = false;
    };

    class TaskPayloadDestroyScope final : NoCopy{
    public:
        explicit TaskPayloadDestroyScope(GpuTaskGraph& graph)noexcept;
        ~TaskPayloadDestroyScope()noexcept;


    public:
        void activateWithinLock()noexcept;


    private:
        GpuTaskGraph& m_graph;
        bool m_active = false;
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
        bool m_active = false;
    };

private:
    struct GpuTaskNode{
        Name identity = s_NameNone;
        GpuTaskCommandRequirements commands;
        GpuTaskSchedulingHint scheduling;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 dependencyOffset = 0u;
        u32 dependencyCount = 0u;
        u32 externalDependencyOffset = 0u;
        u32 externalDependencyCount = 0u;
        u32 externalStateSourceOffset = 0u;
        u32 externalStateSourceCount = 0u;
        u32 resourceUseOffset = 0u;
        u32 resourceUseCount = 0u;
        u32 resourceVersionUseOffset = 0u;
        u32 resourceVersionUseCount = 0u;
        // Preserve declaration structure after resource-set uses expand into the materialized range above.
        u32 directResourceUseCount = 0u;
        u32 declaredResourceSetUseCount = 0u;
        u32 expandedResourceSetMemberUseCount = 0u;
        void* payload = nullptr;
        usize payloadObjectSize = 0u;
        GpuTaskRecordThunk recordPayload = nullptr;
        GpuTaskAcceptedThunk acceptPayload = nullptr;
        GpuTaskDiscardedThunk discardPayload = nullptr;
        GpuTaskPayloadDestroyThunk destroyPayload = nullptr;
        // Lifecycle callbacks are scoped to one graph-owned recording attempt.
        // A retry only re-arms every task after the preceding attempt fully discarded, so a stale native packet cannot publish a later attempt.
        mutable u64 lifecycleAttemptGeneration = 0u;
        mutable u64 recordingClaimGeneration = 0u;
        mutable u64 submissionClaimGeneration = 0u;
        mutable u64 discardNotificationGeneration = 0u;
        GpuTaskTimingMetadata timing;
        mutable TaskLifecycleState lifecycleState = TaskLifecycleState::Declared;
        mutable bool recordThunkInProgress = false;
        mutable bool recordThunkCompleted = false;
    };

    struct GpuGraphResourceNode{
        Name identity = s_NameNone;
        TextureHandle texture;
        BufferHandle buffer;
        RayTracingAccelStructHandle accelStruct;
        GpuExternalCompletionId initialAvailabilityCompletion;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 initialOwnerHandoffSourceOffset = 0u;
        u32 initialOwnerHandoffSourceCount = 0u;
        u32 queueFamilyIndexOffset = 0u;
        u32 queueFamilyIndexCount = 0u;
        ResourceStates::Mask initialState = ResourceStates::Unknown;
        ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
        GpuPhysicalQueueId directConsumerQueue;
        GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        u16 deviceGeneration = 0u;
        bool usesConcurrentSharing = false;
        bool hasQueueAdmission = false;
        GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
        GpuPhysicalQueueId initialOwnerQueue;
    };

    struct ResourcePointerKey{
        const void* pointer = nullptr;
        GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    };

    struct ResourcePointerEqual{
        [[nodiscard]] bool operator()(const ResourcePointerKey& left, const ResourcePointerKey& right)const noexcept{
            return left.pointer == right.pointer && left.type == right.type;
        }
    };

    struct ResourceImportMatch{
        u32 index = Limit<u32>::s_Max;
        bool samePointer = false;
    };

    struct ResourcePointerHasher{
        [[nodiscard]] usize operator()(const ResourcePointerKey& key)const noexcept;
    };

    // The binding borrows an import argument only until its pending resource node takes ownership.
    struct ResourceBinding{
        const TextureHandle* texture = nullptr;
        const BufferHandle* buffer = nullptr;
        const RayTracingAccelStructHandle* accelStruct = nullptr;
        u16 deviceGeneration = 0u;
    };

    struct GpuGraphResourceVersionNode{
        GpuGraphResourceId resource;
        GpuTaskResourceRange range;
        GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::kCount;
    };

    struct GpuGraphResourceSetNode{
        Name identity = s_NameNone;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 memberOffset = 0u;
        u32 memberCount = 0u;
    };

    struct GpuGraphPipelineNode{
        Name identity = s_NameNone;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u16 deviceGeneration = 0u;
        GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
        GraphicsPipelineHandle graphicsPipeline;
        ComputePipelineHandle computePipeline;
        MeshletPipelineHandle meshletPipeline;
        RayTracingPipelineHandle rayTracingPipeline;
    };

    struct PipelinePointerKey{
        const void* pointer = nullptr;
        GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
    };

    struct PipelinePointerEqual{
        [[nodiscard]] bool operator()(const PipelinePointerKey& left, const PipelinePointerKey& right)const noexcept{
            return left.pointer == right.pointer && left.type == right.type;
        }
    };

    struct PipelineImportMatch{
        u32 index = Limit<u32>::s_Max;
        bool samePointer = false;
    };

    struct PipelinePointerHasher{
        [[nodiscard]] usize operator()(const PipelinePointerKey& key)const noexcept;
    };

    // The pending pipeline node retains the imported handle before any registry entry is published.
    struct PipelineBinding{
        const GraphicsPipelineHandle* graphicsPipeline = nullptr;
        const ComputePipelineHandle* computePipeline = nullptr;
        const MeshletPipelineHandle* meshletPipeline = nullptr;
        const RayTracingPipelineHandle* rayTracingPipeline = nullptr;
        u16 deviceGeneration = 0u;
    };

    struct GpuExternalCompletionNode{
        Name identity = s_NameNone;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        QueueSubmissionToken token;
        bool hasToken = false;
    };

    struct GpuUploadBlobNode{
        GraphicsBytes bytes;


    public:
        explicit GpuUploadBlobNode(GraphicsArena& arena)noexcept
            : bytes(arena)
        {}
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
    struct MarkerLabelRange{
        u32 offset = 0u;
        u32 size = 0u;
    };


private:
    [[nodiscard]] static u64 AllocateGeneration()noexcept;
    [[nodiscard]] static ResourcePointerKey MakeResourcePointerKey(const GpuGraphResourceNode& resource)noexcept;
    [[nodiscard]] static PipelinePointerKey MakePipelinePointerKey(const GpuGraphPipelineNode& pipeline)noexcept;


private:
    [[nodiscard]] static Expected<GpuCompiledPacketView> ResolvePacketView(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuSubmissionPacketId packet)noexcept{
        if(!planAccess.validFor(compiledGraph) || !planAccess.validPacket(packet))
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
        const bool leaseValid
    )noexcept{
        const auto packetView = ResolvePacketView(compiledGraph, planAccess, packet);
        if(!packetView || recordingAttemptGeneration == 0u || leaseValid)
            return MakeUnexpected(Failure{});
        return *packetView;
    }


private:
    template<typename TaskT>
    static bool RecordTaskPayload(const void* const payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept(IsSame_V<decltype(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context)), bool>
        && noexcept(TaskT::Record(*static_cast<const typename TaskT::Payload*>(payload), commandList, context))){
        using Payload = typename TaskT::Payload;
        return TaskT::Record(*static_cast<const Payload*>(payload), commandList, context);
    }

    template<typename TaskT>
    static void AcceptTaskPayload(void* const payload, const QueueSubmissionToken& token)noexcept(noexcept(TaskT::Accepted(*static_cast<typename TaskT::Payload*>(payload), token))){
        using Payload = typename TaskT::Payload;
        TaskT::Accepted(*static_cast<Payload*>(payload), token);
    }

    template<typename TaskT>
    static void DiscardTaskPayload(void* const payload)noexcept(noexcept(TaskT::Discarded(*static_cast<typename TaskT::Payload*>(payload)))){
        using Payload = typename TaskT::Payload;
        TaskT::Discarded(*static_cast<Payload*>(payload));
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
    // Metadata-only tasks support graph analysis and scheduling. A task executed through scheduler-owned native recording must provide a payload thunk through the templated overload below.
    [[nodiscard]] Expected<GpuTaskId> addTask(const GpuTaskDesc& desc, const GpuTaskCommandRequirements& commands = {});
    // The first isolated primary-Graphics task gates later normal roots without ordering independent branches.
    [[nodiscard]] bool setNormalExecutionPrelude(const GpuTaskId& task);

    // Adds a graph-owned buffer copy. The helper derives requirements and CopySource/CopyDest uses from its regions and retains the buffers through recording. The caller provides no uses.
    [[nodiscard]] Expected<GpuTaskId> addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc);

    // Adds a graph-owned texture copy. The helper derives requirements and CopySource/CopyDest uses from its regions and retains the textures through recording. The caller provides no uses.
    [[nodiscard]] Expected<GpuTaskId> addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc);

    // Adds a graph-owned texture resolve. The helper derives Graphics requirements and ResolveSource/ResolveDest uses, retaining the textures through recording. The caller provides no uses.
    [[nodiscard]] Expected<GpuTaskId> addResolveTextureTask(const GpuTaskDesc& desc, const GpuResolveTextureTaskDesc& resolveDesc);

    // Copies caller-owned bytes into graph-owned storage. `alignment` must be a nonzero power of two; blobs expose only an opaque byte view. Upload tasks resolve the blob while recording.
    [[nodiscard]] Expected<GpuUploadBlobId> copyUploadData(const void* data, usize byteSize, usize alignment = alignof(u8));

    // Adds graph-owned buffer/texture uploads.
    // Ordered uses expose the native CopyDest write and optional final transition; preflight and later packets observe the complete state contract.
    [[nodiscard]] Expected<GpuTaskId> addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc);
    [[nodiscard]] Expected<GpuTaskId> addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc);

    // Adds a graph-owned native uint-buffer clear. The helper retains the imported buffer and derives Transfer requirements and its CopyDest write. No separate uses.
    [[nodiscard]] Expected<GpuTaskId> addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc);

    // Adds a graph-owned texture clear. The helper retains the texture and derives its CopyDest write and requirements, including Compute/Graphics shader-clear options. No separate uses.
    [[nodiscard]] Expected<GpuTaskId> addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc);

    // Adds a graph-owned rectangular uint texture clear. The helper retains the texture and derives its subresource CopyDest write and requirements. No separate uses.
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

        const auto task = appendTaskWithinMutation(desc, GpuGraphTaskContract::CommandRequirements<TaskT>(*storedPayload.get()), storedPayload.get(), recordPayload, acceptPayload, discardPayload, &DestroyTaskPayload<Payload>, sizeof(Payload), mutation);
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
        const GpuTaskDesc& resolvedDesc,
        ProvisionalPayloadOwner<typename TaskT::Payload>& payload,
        const DeclarationMutationScope& mutation,
        const GpuTaskCommandRequirements& commands = { .requiredCapabilities = GpuQueueCapability::Transfer }
    ){
        using Payload = typename TaskT::Payload;
        const auto task = appendTaskWithinMutation(resolvedDesc, commands, payload.get(), &RecordTaskPayload<TaskT>, &AcceptTaskPayload<TaskT>, &DiscardTaskPayload<TaskT>, &DestroyTaskPayload<Payload>, sizeof(Payload), mutation);
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
    // Declares a distinct semantic value for one exact physical resource range. Repeating an identical descriptor intentionally produces a different version ID.
    [[nodiscard]] Expected<GpuGraphResourceVersionId> declareResourceVersion(const GpuGraphResourceVersionDesc& desc);
    // Stores an immutable dynamic resource collection.
    // Task resource-set declarations expand to concrete members at task creation; compilation and recording keep their resource-level contracts.
    [[nodiscard]] Expected<GpuGraphResourceSetId> importResourceSet(const GpuGraphResourceSetDesc& desc);
    // Pipeline IDs are graph-local side-table entries. Metadata-only entries support analysis and capture setup.
    [[nodiscard]] Expected<GpuGraphPipelineId> importPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importGraphicsPipeline(const GraphicsPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importComputePipeline(const ComputePipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importMeshletPipeline(const MeshletPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuGraphPipelineId> importRayTracingPipeline(const RayTracingPipelineHandle& pipeline, const GpuGraphPipelineDesc& desc);
    [[nodiscard]] Expected<GpuExternalCompletionId> importExternalCompletion(const GpuExternalCompletionDesc& desc);
    // One graph may publish one presentation completion. The compiler validates the typed backbuffer, its single-sink contract, writer deps, and exact Graphics routing before policy sees it.
    [[nodiscard]] bool declarePresentEndpoint(const GpuPresentEndpoint& endpoint);

    // Reset is externally serialized against *starting* native recording/submission entrypoints.
    // A bound partial attempt and every transient packet claim refuse teardown; callers must resolve the owner before retrying.
    [[nodiscard]] bool tryReset();
    void reset();


private:
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    // Starts or validates one native-recording attempt for the compiler-owned packet.
    // A retry can begin only after every task from the previous attempt was discarded; accepted-frontier recovery remains in that same attempt.
    [[nodiscard]] bool beginRecordingAttempt(const GpuCompiledGraph& compiledGraph, GpuSubmissionPacketId packet, const DeclarationReadView& declarationAccess, const GpuCompiledGraph::ReadView& planAccess, GpuRecordingAttemptScope& outAttempt)const noexcept;
    void cancelRecordingAttempt(GpuRecordingAttemptScope& attempt)const noexcept;
    void completeRecordingPreparation(GpuRecordingAttemptScope& attempt)const noexcept;
    [[nodiscard]] bool matchesRecordingAttempt(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] bool resolveRecordingAttemptIfTerminal(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration)const noexcept;
    [[nodiscard]] bool bindSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding, const GpuRecordingAttemptScope* preparationAttempt = nullptr)const noexcept;
    [[nodiscard]] bool matchesSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] bool resolveSubmissionTransaction(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] bool beginSubmissionExceptionClosing(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;
    [[nodiscard]] bool waitForSubmissionExceptionRecordingClaims(const GpuCompiledGraph& compiledGraph, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const noexcept;

private:
    // Caller holds m_lifecycleMutex; preparation, submission binding and task-state checks remain path-specific.
    [[nodiscard]] bool matchesPacketAttemptWithinLock(const GpuCompiledGraph& compiledGraph, const u64 planGeneration, const u64 recordingAttemptGeneration)const noexcept{
        return !m_teardownInProgress && m_activeCompiledGraph == &compiledGraph && m_activeRecordingPlanGeneration == planGeneration && m_activeRecordingAttemptGeneration == recordingAttemptGeneration;
    }

    [[nodiscard]] Expected<void, RecordTaskFailure> recordTask(const GpuTaskId& task, CommandList& commandList, const GpuTaskRecordContext& context, const GpuPacketRecordingLease& lease)const;

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

    // Claim packet tasks before recording. One attempt has exactly one artifact per packet even with concurrent outputs. The lease names the recorder that may run thunks, complete, or abort.
    [[nodiscard]] bool beginPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration, GpuPacketRecordingLease& outLease)const noexcept;
    // A claimed packet becomes submission-eligible only after all its thunks complete.
    [[nodiscard]] bool completePacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    // Transfers a failed recorder's exact claim without invoking user code or changing task lifecycle on its worker.
    [[nodiscard]] bool deferPacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease, GpuPacketRecordingAbort& outAbort)const noexcept;
    // Invokes typed discard callbacks and finalizes one transferred abort. Callers choose the serial drain order.
    [[nodiscard]] bool completePacketRecordingAbort(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const;
    // Exception unwind consumes exact recording capabilities without invoking extensible discard observers.
    [[nodiscard]] bool abandonPacketRecordingWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const noexcept;
    [[nodiscard]] bool abandonPacketRecordingAbortWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuPacketRecordingAbort& abort)const noexcept;
    // Ordinary cleanup cannot discard a Recording packet: a retry must not re-arm payload while the original recorder owns it.
    void abortPacketRecording(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketRecordingLease& lease)const;
    [[nodiscard]] bool packetReadyForSubmission(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration)const noexcept;

private:
    [[nodiscard]] bool beginPacketSubmission(
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
    [[nodiscard]] bool abandonPacketSubmissionWithoutCallbacks(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, GpuPacketSubmissionLease& lease)const noexcept;
    // Atomically discards one non-recording packet.
    // A packet with an in-flight Recording claim is left intact so transaction cancellation cannot race native recording or reopen the graph.
    [[nodiscard]] bool discardUnacceptedPacket(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, GpuSubmissionPacketId packet, u64 recordingAttemptGeneration, const GpuGraphSubmissionBinding& submissionBinding)const;
    // Unexpected observer failure must resolve the graph-owned attempt without invoking another callback while the original exception is active.
    // Payload destruction remains owned by normal graph teardown.
    [[nodiscard]] bool abandonUnacceptedPacketWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;

private:
    // Lowers a compiler-owned packet-boundary barrier through the existing state tracker. Task thunks own only barriers internal to their sequence.
    [[nodiscard]] bool applyCompiledBarrier(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const GpuPacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    // Materializes retained resource states after barriers lower. This preserves graph-owned packet handoffs when a required state already matches an imported resource and needs no transition.
    [[nodiscard]] bool seedTaskRetainedResourceStates(const GpuCompiledGraph& compiledGraph, const GpuCompiledGraph::ReadView& planAccess, const GpuPacketRecordingAccess& recordingAccess, const GpuTaskId& task, CommandList& commandList)const;

private:
    [[nodiscard]] bool validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] bool validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] bool validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept;
    [[nodiscard]] bool validResourceSet(const GpuGraphResourceSetId& id)const noexcept;
    [[nodiscard]] bool validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] bool validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] bool validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
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
        void* payload,
        GpuTaskRecordThunk recordPayload,
        GpuTaskAcceptedThunk acceptPayload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload,
        usize payloadObjectSize,
        const DeclarationMutationScope& mutationAccess
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
    [[nodiscard]] bool retireTaskPayloads();
    [[nodiscard]] bool destroyTaskPayloads();
    [[nodiscard]] bool destroyTaskPayloadsWithoutCallbacks()noexcept;
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
    mutable SubmissionBindingState m_submissionBindingState = SubmissionBindingState::None;
    u16 m_externalCompletionDeviceGeneration = 0u;
    bool m_hasPresentEndpoint = false;
    mutable bool m_teardownInProgress = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

