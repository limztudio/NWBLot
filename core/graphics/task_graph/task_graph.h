// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_contract.h"
#include "task_desc.h"

#include <core/alloc/scratch.h>
#include <global/arena_object.h>
#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraphAnalysis;
class GpuTaskGraphQueueAssignments;
class GpuTaskGraphQueueAssignmentTelemetryTracker;
struct GpuCompiledBarrier;

namespace Telemetry{
    class FrameGraphBuilder;
};


struct GpuTaskGraphTaskView{
    GpuTaskId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuQueueRequest queue;
    GpuTaskSchedulingHint scheduling;
    GpuTaskTimingMetadata timing;
    const GpuTaskId* dependencies = nullptr;
    usize dependencyCount = 0u;
    const GpuExternalCompletionId* externalDependencies = nullptr;
    usize externalDependencyCount = 0u;
    // Declaration-supplied sources point to immutable graph-owned snapshots.
    const GpuTaskExternalStateSource* externalStateSources = nullptr;
    usize externalStateSourceCount = 0u;
    const GpuTaskResourceUse* resourceUses = nullptr;
    usize resourceUseCount = 0u;
    const GpuTaskResourceVersionUse* resourceVersionUses = nullptr;
    usize resourceVersionUseCount = 0u;
    usize payloadObjectSize = 0u;
    usize directResourceUseCount = 0u;
    usize declaredResourceSetUseCount = 0u;
    usize expandedResourceSetMemberUseCount = 0u;
    bool hasPayload = false;
    bool hasRecordPayload = false;
    bool hasAcceptedPayload = false;
};

// Graph-owned immutable view of one texture range released by external work. State sources are copied while the
// resource is declared, so an accepted source graph or native producer may retire its original handoff before this
// graph records its first consumer packet.
struct GpuTaskGraphInitialOwnerHandoffSourceView{
    GpuTaskResourceRange range;
    GpuPhysicalQueueId sourceQueue;
    GpuPhysicalQueueId destinationQueue;
    GpuExternalCompletionId completion;
    QueueSubmissionToken minimumCompletionToken;
    const CommandListResourceStateHandoff* stateSource = nullptr;
};

struct GpuTaskGraphResourceView{
    GpuGraphResourceId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    GpuPhysicalQueueId initialOwnerQueue;
    GpuPhysicalQueueId initialOwnerReleaseDestinationQueue;
    GpuExternalCompletionId initialOwnerCompletion;
    QueueSubmissionToken initialOwnerMinimumCompletionToken;
    // For imported exclusive-owner handoffs this is a graph-owned immutable snapshot, captured at declaration.
    const CommandListResourceStateHandoff* initialOwnerStateSource = nullptr;
    // Texture-only multi-producer ownership handoff sources. A first graph use must be fully covered by exactly one
    // source that names its selected physical consumer queue; broad or ambiguous uses fail compilation.
    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    GpuExternalCompletionId initialAvailabilityCompletion;
    // Typed resource imports own an exact copy of the backend's immutable physical admission facts. Metadata-only
    // resources have no snapshot and retain the logical queue-sharing resolver.
    ResourceQueueAdmissionSnapshot queueAdmission;
    bool hasQueueAdmission = false;
    bool hasBackendResource = false;
};

struct GpuTaskGraphResourceVersionView{
    GpuGraphResourceVersionId id;
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::kCount;
};

struct GpuTaskGraphResourceSetView{
    GpuGraphResourceSetId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;
};

struct GpuTaskGraphPipelineView{
    GpuGraphPipelineId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
    bool hasBackendPipeline = false;
};

struct GpuTaskGraphExternalCompletionView{
    GpuExternalCompletionId id;
    Name identity = NAME_NONE;
    AStringView markerLabel;
    QueueSubmissionToken token;
    bool hasToken = false;
};

// One graph-declared presentation completion. The backbuffer is a retained typed texture captured in its native
// Unknown/Present acquisition state and exported only to the Present sink, without a second ownership release.
// Every declared user must reach the Graphics producer and compile onto its exact physical queue. The producer may
// omit a direct use so a terminal timing/finalization task can publish after the earlier backbuffer writers.
struct GpuPresentEndpoint{
    GpuTaskId producer;
    GpuGraphResourceId backBuffer;
};

struct GpuTaskGraphTelemetryOptions{
    const GpuTaskGraphQueueAssignments* queueAssignments = nullptr;
    const GpuCompiledGraph::ReadView* compiledPlan = nullptr;
    const GpuTaskGraphQueueAssignmentTelemetryTracker* queueAssignmentTelemetry = nullptr;
};

class GpuTaskGraph;
class GpuNativePacketRecorder;
class GpuGraphSubmissionTransaction;
class GpuRecordedGraph;
class GpuTaskGraphSubmitter;


// Lexical proof that immutable declaration storage cannot be reset or mutated. Compiler/tooling callers acquire a
// waiting view before any other operation gate; runtime paths use nonblocking acquisition after taking other gates.
class GpuTaskGraphDeclarationReadView final : NoCopy{
    friend class GpuCompiledGraph::ReadView;
    friend class GpuTaskGraph;

private:
    struct TryAcquireTag{};


public:
    [[nodiscard]] static GpuTaskGraphDeclarationReadView tryAcquire(const GpuTaskGraph& graph)noexcept;


public:
    explicit GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph);
    GpuTaskGraphDeclarationReadView(GpuTaskGraphDeclarationReadView&&) = delete;
    ~GpuTaskGraphDeclarationReadView()noexcept;


public:
    [[nodiscard]] bool valid()const noexcept{ return m_graph != nullptr; }
    [[nodiscard]] bool validFor(const GpuTaskGraph& graph)const noexcept{ return m_graph == &graph; }
    [[nodiscard]] u64 generation()const noexcept;
    [[nodiscard]] u64 declarationRevision()const noexcept;
    [[nodiscard]] bool validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] bool validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] bool validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept;
    [[nodiscard]] bool validResourceSet(const GpuGraphResourceSetId& id)const noexcept;
    [[nodiscard]] bool validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] bool validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] bool validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] usize taskCount()const noexcept;
    [[nodiscard]] usize resourceCount()const noexcept;
    [[nodiscard]] usize resourceVersionCount()const noexcept;
    [[nodiscard]] usize resourceSetCount()const noexcept;
    [[nodiscard]] usize uploadBlobCount()const noexcept;
    [[nodiscard]] usize pipelineCount()const noexcept;
    [[nodiscard]] usize externalCompletionCount()const noexcept;
    // Borrowed declaration storage remains valid only while this named lvalue view stays alive.
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const & noexcept;
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const && = delete;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const & noexcept;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const && = delete;
    [[nodiscard]] GpuTaskGraphResourceVersionView resourceVersionAt(usize index)const noexcept;
    [[nodiscard]] GpuTaskGraphResourceSetView resourceSetAt(usize index)const & noexcept;
    [[nodiscard]] GpuTaskGraphResourceSetView resourceSetAt(usize index)const && = delete;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const & noexcept;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const && = delete;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const & noexcept;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const && = delete;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(
        const GpuExternalCompletionId& completion
    )const & noexcept;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(const GpuExternalCompletionId& completion)const && = delete;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] const void* uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const & noexcept;
    [[nodiscard]] const void* uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const && = delete;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const && = delete;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const && = delete;
    [[nodiscard]] MeshletPipeline* meshletPipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept;
    [[nodiscard]] MeshletPipeline* meshletPipelineFor(const GpuGraphPipelineId& pipeline)const && = delete;
    [[nodiscard]] RayTracingPipeline* rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept;
    [[nodiscard]] RayTracingPipeline* rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const && = delete;
    [[nodiscard]] GpuGraphResourceId findImportedTexture(const TextureHandle& texture)const noexcept;
    [[nodiscard]] GpuGraphResourceId findImportedBuffer(const BufferHandle& buffer)const noexcept;
    [[nodiscard]] const GpuPresentEndpoint* presentEndpoint()const & noexcept;
    [[nodiscard]] const GpuPresentEndpoint* presentEndpoint()const && = delete;
    [[nodiscard]] bool appendFrameGraphTelemetry(
        Telemetry::FrameGraphBuilder& builder,
        const GpuTaskGraphAnalysis& analysis,
        Alloc::ScratchArena& scratchArena,
        const GpuTaskGraphTelemetryOptions& options = {}
    )const;


private:
    GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph, TryAcquireTag)noexcept;
    void acquire(const GpuTaskGraph& graph)noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
};


class GpuGraphSubmissionBinding final{
    friend class GpuCompiledGraph;
    friend class GpuTaskGraph;
    friend class GpuGraphSubmissionTransaction;
    friend bool operator==(const GpuGraphSubmissionBinding& lhs, const GpuGraphSubmissionBinding& rhs)noexcept;


public:
    GpuGraphSubmissionBinding() = default;


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_transactionIdentity != 0u && m_resetGeneration != 0u;
    }


private:
    GpuGraphSubmissionBinding(const u64 transactionIdentity, const u64 resetGeneration)noexcept
        : m_transactionIdentity(transactionIdentity)
        , m_resetGeneration(resetGeneration)
    {}


private:
    u64 m_transactionIdentity = 0u;
    u64 m_resetGeneration = 0u;
};


[[nodiscard]] inline bool operator==(
    const GpuGraphSubmissionBinding& lhs,
    const GpuGraphSubmissionBinding& rhs
)noexcept{
    return lhs.m_transactionIdentity == rhs.m_transactionIdentity
        && lhs.m_resetGeneration == rhs.m_resetGeneration
    ;
}

[[nodiscard]] inline bool operator!=(
    const GpuGraphSubmissionBinding& lhs,
    const GpuGraphSubmissionBinding& rhs
)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph final : NoCopy{
    friend class GpuTaskGraphDeclarationReadView;
    friend class GpuTaskGraphCompiler;
    friend class GpuTaskGraphQueueAssignmentTelemetryTracker;
    friend class GpuNativePacketRecorder;
    friend class GpuGraphSubmissionTransaction;
    friend class GpuRecordedGraph;
    friend class GpuTaskGraphSubmitter;


public:
    using DeclarationReadView = GpuTaskGraphDeclarationReadView;


private:
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
        ~DeclarationMutationScope();


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
        ~DiscardNotificationScope();


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
        ~TaskPayloadDestroyScope();


    public:
        void activateWithinLock()noexcept;


    private:
        GpuTaskGraph& m_graph;
        bool m_active = false;
    };

    class ResetCompletionScope final : NoCopy{
    public:
        explicit ResetCompletionScope(GpuTaskGraph& graph)noexcept;
        ~ResetCompletionScope();


    public:
        void activateWithinLock()noexcept;
        void complete()noexcept;


    private:
        GpuTaskGraph& m_graph;
        bool m_active = false;
    };

    class RecordingAttemptScope final : NoCopy{
        friend class GpuTaskGraph;
        friend class GpuGraphSubmissionTransaction;
        friend class GpuNativePacketRecorder;
        friend class GpuTaskGraphSubmitter;

    public:
        RecordingAttemptScope() = default;
        ~RecordingAttemptScope()noexcept;


    public:
        void complete()noexcept;


    private:
        void activateWithinLock(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            u64 recordingAttemptGeneration,
            u64 preparationSerial,
            bool previousPlanWasActive
        )noexcept;
        void completeWithinLock()noexcept;
        [[nodiscard]] bool validPreparationWithinLock(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            u64 recordingAttemptGeneration,
            u64 preparationSerial
        )const noexcept;


    private:
        const GpuTaskGraph* m_graph = nullptr;
        const GpuCompiledGraph* m_compiledGraph = nullptr;
        u64 m_recordingAttemptGeneration = 0u;
        u64 m_preparationSerial = 0u;
        bool m_previousPlanWasActive = false;
    };

private:
    // A failed parallel recorder transfers its exact packet claim into this capability before leaving its worker.
    // The ready-frontier owner can then invoke typed discard callbacks serially after every worker has joined.
    class PacketRecordingAbort final : NoCopy{
        friend class GpuTaskGraph;

    public:
        PacketRecordingAbort() = default;
        PacketRecordingAbort(PacketRecordingAbort&& other)noexcept;
        ~PacketRecordingAbort();


    public:
        [[nodiscard]] bool valid()const noexcept{
            return m_packet.valid()
                && m_planGeneration != 0u
                && m_recordingAttemptGeneration != 0u
                && m_claimGeneration != 0u
            ;
        }


    private:
        void reset()noexcept{
            m_packet = {};
            m_planGeneration = 0u;
            m_recordingAttemptGeneration = 0u;
            m_claimGeneration = 0u;
        }


    private:
        GpuSubmissionPacketId m_packet;
        u64 m_planGeneration = 0u;
        u64 m_recordingAttemptGeneration = 0u;
        u64 m_claimGeneration = 0u;
    };

    // A packet claim is an opaque runtime capability. Only the recorder that acquired it can invoke task thunks,
    // complete native recording, or abandon that exact packet attempt.
    class PacketRecordingLease final : NoCopy{
        friend class GpuTaskGraph;
        friend class PacketRecordingAccess;

    public:
        PacketRecordingLease() = default;
        PacketRecordingLease(PacketRecordingLease&&) = delete;
        PacketRecordingLease& operator=(PacketRecordingLease&&) = delete;


    public:
        [[nodiscard]] bool valid()const noexcept{
            return m_packet.valid()
                && m_planGeneration != 0u
                && m_recordingAttemptGeneration != 0u
                && m_claimGeneration != 0u
            ;
        }
        [[nodiscard]] bool validFor(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuCompiledGraph::ReadView& planAccess,
            GpuSubmissionPacketId packet
        )const noexcept;


    private:
        void reset()noexcept{
            m_packet = {};
            m_planGeneration = 0u;
            m_recordingAttemptGeneration = 0u;
            m_claimGeneration = 0u;
        }


    private:
        GpuSubmissionPacketId m_packet;
        u64 m_planGeneration = 0u;
        u64 m_recordingAttemptGeneration = 0u;
        u64 m_claimGeneration = 0u;
    };

    // Authenticates one exact recording claim once, then carries the lexical graph/plan read proof through packet
    // preflight and native barrier/state lowering without repeatedly taking the graph lifecycle mutex.
    class PacketRecordingAccess final : NoCopy{
        friend class GpuTaskGraph;
        friend class GpuNativePacketRecorder;

    public:
        PacketRecordingAccess(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuCompiledGraph::ReadView& planAccess,
            const PacketRecordingLease& lease
        )noexcept;


    public:
        [[nodiscard]] bool validFor(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuCompiledGraph::ReadView& planAccess,
            GpuSubmissionPacketId packet
        )const noexcept;
        [[nodiscard]] bool validForTask(
            const GpuTaskGraph& graph,
            const GpuCompiledGraph& compiledGraph,
            const GpuCompiledGraph::ReadView& planAccess,
            const GpuTaskId& task
        )const noexcept;


    private:
        const GpuTaskGraph* m_graph = nullptr;
        const GpuCompiledGraph* m_compiledGraph = nullptr;
        const PacketRecordingLease* m_lease = nullptr;
        GpuSubmissionPacketId m_packet;
        u64 m_planGeneration = 0u;
        u64 m_recordingAttemptGeneration = 0u;
        u64 m_claimGeneration = 0u;
    };

    // Native submission has the same exclusive ownership rule as native recording: cancellation cannot discard
    // graph payload while Device::executeCommandLists() owns the packet, and only the owning transaction resolves it.
    class PacketSubmissionLease final : NoCopy{
        friend class GpuTaskGraph;
        friend class GpuGraphSubmissionTransaction;

    public:
        PacketSubmissionLease() = default;
        PacketSubmissionLease(PacketSubmissionLease&&) = delete;
        PacketSubmissionLease& operator=(PacketSubmissionLease&&) = delete;


    public:
        [[nodiscard]] bool valid()const noexcept{
            return m_packet.valid()
                && m_planGeneration != 0u
                && m_recordingAttemptGeneration != 0u
                && m_claimGeneration != 0u
                && m_submissionBinding.valid()
            ;
        }


    private:
        void reset()noexcept{
            m_packet = {};
            m_planGeneration = 0u;
            m_recordingAttemptGeneration = 0u;
            m_claimGeneration = 0u;
            m_submissionBinding = {};
        }


    private:
        GpuSubmissionPacketId m_packet;
        u64 m_planGeneration = 0u;
        u64 m_recordingAttemptGeneration = 0u;
        u64 m_claimGeneration = 0u;
        GpuGraphSubmissionBinding m_submissionBinding;
    };

private:
    struct GpuTaskNode{
        Name identity = NAME_NONE;
        GpuQueueRequest queue;
        GpuTaskSchedulingHint scheduling;
        GpuTaskTimingMetadata timing;
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
        // Lifecycle callbacks are scoped to one graph-owned recording attempt. A retry only re-arms every task
        // after the preceding attempt fully discarded, so a stale native packet cannot publish a later attempt.
        mutable TaskLifecycleState lifecycleState = TaskLifecycleState::Declared;
        mutable u64 lifecycleAttemptGeneration = 0u;
        mutable u64 recordingClaimGeneration = 0u;
        mutable u64 submissionClaimGeneration = 0u;
        mutable u64 discardNotificationGeneration = 0u;
        mutable bool recordThunkInProgress = false;
        mutable bool recordThunkCompleted = false;
    };

    struct GpuGraphResourceNode{
        Name identity = NAME_NONE;
        TextureHandle texture;
        BufferHandle buffer;
        RayTracingAccelStructHandle accelStruct;
        u16 deviceGeneration = 0u;
        // The graph retains its own immutable copy for late recording. Repeated typed imports compare this owned
        // value, never the producer allocation address, so retired snapshot storage cannot alias through ABA reuse.
        CommandListResourceStateHandoff* initialOwnerStateSource = nullptr;
        GpuExternalCompletionId initialOwnerCompletion;
        QueueSubmissionToken initialOwnerMinimumCompletionToken;
        GpuExternalCompletionId initialAvailabilityCompletion;
        ResourceStates::Mask initialState = ResourceStates::Unknown;
        ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 initialOwnerHandoffSourceOffset = 0u;
        u32 initialOwnerHandoffSourceCount = 0u;
        u32 queueFamilyIndexOffset = 0u;
        u32 queueFamilyIndexCount = 0u;
        GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
        GpuPhysicalQueueId initialOwnerQueue;
        GpuPhysicalQueueId initialOwnerReleaseDestinationQueue;
        GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
        ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
        bool usesConcurrentSharing = false;
        bool hasQueueAdmission = false;
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
        Name identity = NAME_NONE;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        u32 memberOffset = 0u;
        u32 memberCount = 0u;
    };

    struct GpuGraphPipelineNode{
        Name identity = NAME_NONE;
        GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        GraphicsPipelineHandle graphicsPipeline;
        ComputePipelineHandle computePipeline;
        MeshletPipelineHandle meshletPipeline;
        RayTracingPipelineHandle rayTracingPipeline;
        u16 deviceGeneration = 0u;
    };

    struct GpuExternalCompletionNode{
        Name identity = NAME_NONE;
        QueueSubmissionToken token;
        u32 markerLabelOffset = 0u;
        u32 markerLabelSize = 0u;
        bool hasToken = false;
    };

    struct GpuUploadBlobNode{
        explicit GpuUploadBlobNode(GraphicsArena& arena)
            : bytes(arena)
        {}

        GraphicsBytes bytes;
    };


private:
    using ResourceIdentityIndex = HashMap<NameHash, u32, GraphicsArena>;
    using ResourcePointerIndex = HashMap<
        ResourcePointerKey, u32, ResourcePointerHasher, ResourcePointerEqual, GraphicsArena
    >;


private:
    static constexpr usize s_InlineImportIndexCount = 32u;
    static constexpr u32 s_InvalidImportIndex = Limit<u32>::s_Max;

    [[nodiscard]] static u64 allocateGeneration()noexcept;
    [[nodiscard]] static ResourcePointerKey resourcePointerKey(const GpuGraphResourceNode& resource)noexcept;


public:
    explicit GpuTaskGraph(GraphicsArena& arena);
    ~GpuTaskGraph()noexcept(false);


public:
    // Metadata-only tasks support graph analysis and scheduling. A task recorded by GpuNativePacketRecorder must
    // provide a payload thunk through the templated overload below.
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc);

    // Adds a graph-owned native buffer-copy task. The helper derives CopySource/CopyDest resource uses from its
    // regions and retains the imported buffers through recording, so desc must declare Transfer capability and
    // must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addCopyBufferTask(const GpuTaskDesc& desc, const GpuCopyBufferTaskDesc& copyDesc);

    // Adds a graph-owned native texture-copy task. The helper derives CopySource/CopyDest resource uses from its
    // regions and retains the imported textures through recording, so desc must declare Transfer capability and
    // must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addCopyTextureTask(const GpuTaskDesc& desc, const GpuCopyTextureTaskDesc& copyDesc);

    // Adds a graph-owned native texture-resolve task. The helper derives ResolveSource/ResolveDest resource uses
    // from its regions and retains the imported textures through recording, so desc must declare Graphics capability
    // and must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addResolveTextureTask(const GpuTaskDesc& desc, const GpuResolveTextureTaskDesc& resolveDesc);

    // Copies caller-owned bytes into graph-owned CPU storage. `alignment` must be a nonzero power of two; blobs
    // expose only an opaque byte view, so no typed-alignment promise escapes the graph. Built-in upload tasks resolve
    // the immutable blob while recording, then use the ordinary CommandList staging allocator for GPU lifetime.
    [[nodiscard]] GpuUploadBlobId copyUploadData(
        const void* data,
        usize byteSize,
        usize alignment = alignof(u8)
    );

    // Adds graph-owned buffer/texture uploads. Ordered uses expose the native CopyDest write and optional local
    // final transition, so packet preflight and later graph packets observe the complete state contract.
    [[nodiscard]] GpuTaskId addUploadBufferTask(const GpuTaskDesc& desc, const GpuUploadBufferTaskDesc& uploadDesc);
    [[nodiscard]] GpuTaskId addUploadTextureTask(const GpuTaskDesc& desc, const GpuUploadTextureTaskDesc& uploadDesc);

    // Adds a graph-owned native uint-buffer clear. The helper retains the imported buffer and derives its CopyDest
    // write declaration, so desc must declare Transfer capability and must not provide separate resource uses.
    [[nodiscard]] GpuTaskId addClearBufferTask(const GpuTaskDesc& desc, const GpuClearBufferTaskDesc& clearDesc);

    // Adds a graph-owned native texture clear. The helper retains the imported texture and derives its CopyDest
    // write declaration, so desc must declare Transfer capability and must not provide separate resource uses.
    // It adds Compute for ordinary color unless Graphics is declared, and adds Graphics for depth/stencil.
    [[nodiscard]] GpuTaskId addClearTextureTask(const GpuTaskDesc& desc, const GpuClearTextureTaskDesc& clearDesc);

    // Adds a graph-owned native rectangular unsigned-integer texture clear. The helper retains the imported texture
    // and derives its exact subresource CopyDest write declaration, so desc must declare Transfer capability and
    // must not provide separate resource uses. Partial regions additionally require Compute or Graphics.
    [[nodiscard]] GpuTaskId addClearTextureRectUIntTask(
        const GpuTaskDesc& desc,
        const GpuClearTextureRectUIntTaskDesc& clearDesc
    );

    template<typename TaskT>
    [[nodiscard]] GpuTaskId addTask(const GpuTaskDesc& desc, typename TaskT::Payload&& payload){
        using Payload = typename TaskT::Payload;
        static_assert(IsNothrowDestructible_V<Payload>, "GPU task payload cleanup must remain no-throw");

        DeclarationMutationScope mutation(*this);
        if(!mutation.valid())
            return {};

        Payload* const payloadObject = NewArenaObject<Payload>(m_arena, Move(payload));
        if(!payloadObject)
            return {};
        ProvisionalPayloadOwner<Payload> storedPayload(m_arena, payloadObject);

        GpuTaskRecordThunk recordPayload = nullptr;
        if constexpr(GpuGraphTaskContract::RecordApi<TaskT>)
            recordPayload = &RecordPayload<TaskT>;

        GpuTaskAcceptedThunk acceptPayload = nullptr;
        if constexpr(GpuGraphTaskContract::AcceptedApi<TaskT>)
            acceptPayload = &AcceptPayload<TaskT>;

        GpuTaskDiscardedThunk discardPayload = nullptr;
        if constexpr(GpuGraphTaskContract::DiscardedApi<TaskT>)
            discardPayload = &DiscardPayload<TaskT>;

        const GpuTaskId task = appendTaskWithinMutation(
            desc,
            storedPayload.get(),
            recordPayload,
            acceptPayload,
            discardPayload,
            &DestroyPayload<Payload>,
            sizeof(Payload),
            mutation
        );
        if(task.valid())
            storedPayload.publish();
        else
            discardAndDestroyUnappendedPayload(storedPayload.release(), discardPayload, &DestroyPayload<Payload>);
        return task;
    }

    // This form is useful for abstract resources and conservative bindless hazard domains during the metadata-only
    // phase. Tasks that will be recorded later must use a typed import overload so the graph retains the resource.
    [[nodiscard]] GpuGraphResourceId importResource(const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importTexture(const TextureHandle& texture, const GpuGraphResourceDesc& desc);
    [[nodiscard]] GpuGraphResourceId importBuffer(const BufferHandle& buffer, const GpuGraphResourceDesc& desc);
    // Reuses a typed texture import whose identity may have been chosen by an earlier producer. This lets later
    // consumers add resource uses for the same physical texture without inventing incompatible graph metadata.
    // Reuses a typed buffer import whose identity may have been chosen by an earlier producer. This lets later
    // consumers add resource uses for the same physical buffer without inventing incompatible graph metadata.
    [[nodiscard]] GpuGraphResourceId importAccelStruct(
        const RayTracingAccelStructHandle& accelStruct,
        const GpuGraphResourceDesc& desc
    );
    [[nodiscard]] GpuGraphResourceId importHazardDomain(const GpuGraphResourceDesc& desc);
    // Declares a distinct semantic value for one exact physical resource range. Repeating an identical descriptor
    // intentionally produces a different version ID.
    [[nodiscard]] GpuGraphResourceVersionId declareResourceVersion(const GpuGraphResourceVersionDesc& desc);
    // Stores an immutable dynamic resource collection. Task resource-set declarations expand to the set's concrete
    // members at task creation, so compilation and recording keep their existing resource-level contracts.
    [[nodiscard]] GpuGraphResourceSetId importResourceSet(const GpuGraphResourceSetDesc& desc);
    // Pipeline IDs are graph-local side-table entries.  Metadata-only entries support analysis/capture setup;
    // typed imports retain a stable engine handle until native recording or later IR replay resolves it.
    [[nodiscard]] GpuGraphPipelineId importPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] GpuGraphPipelineId importGraphicsPipeline(
        const GraphicsPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importComputePipeline(
        const ComputePipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importMeshletPipeline(
        const MeshletPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuGraphPipelineId importRayTracingPipeline(
        const RayTracingPipelineHandle& pipeline,
        const GpuGraphPipelineDesc& desc
    );
    [[nodiscard]] GpuExternalCompletionId importExternalCompletion(const GpuExternalCompletionDesc& desc);
    // One graph may publish one presentation completion. The compiler validates the retained typed backbuffer,
    // its single-sink acquisition/final-state contract, every user-to-producer dependency, at least one real
    // writer, and exact physical Graphics routing before exposing it to native presentation policy.
    [[nodiscard]] bool declarePresentEndpoint(const GpuPresentEndpoint& endpoint);

    // Reset is externally serialized against *starting* native recording/submission entrypoints. A bound partial
    // attempt and every transient packet claim refuse teardown; callers must resolve the owner before retrying.
    [[nodiscard]] bool tryReset();
    void reset();


private:
    [[nodiscard]] u64 recordingAttemptGeneration()const noexcept;
    // Starts or validates one native-recording attempt for the compiler-owned packet. A retry can begin only after
    // every task from the previous attempt was discarded; accepted-frontier recovery remains in that same attempt.
    [[nodiscard]] bool beginRecordingAttempt(
        const GpuCompiledGraph& compiledGraph,
        GpuSubmissionPacketId packet,
        const DeclarationReadView& declarationAccess,
        const GpuCompiledGraph::ReadView& planAccess,
        RecordingAttemptScope& outAttempt
    )const noexcept;
    void cancelRecordingAttempt(RecordingAttemptScope& attempt)const noexcept;
    void completeRecordingPreparation(RecordingAttemptScope& attempt)const noexcept;
    [[nodiscard]] bool matchesRecordingAttempt(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )const noexcept;
    [[nodiscard]] bool resolveRecordingAttemptIfTerminal(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration
    )const noexcept;
    [[nodiscard]] bool bindSubmissionTransaction(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding,
        const RecordingAttemptScope* preparationAttempt = nullptr
    )const noexcept;
    [[nodiscard]] bool matchesSubmissionTransaction(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;
    [[nodiscard]] bool resolveSubmissionTransaction(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;
    [[nodiscard]] bool beginSubmissionExceptionClosing(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;
    [[nodiscard]] bool waitForSubmissionExceptionRecordingClaims(
        const GpuCompiledGraph& compiledGraph,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;

private:
    [[nodiscard]] bool recordTask(
        const GpuTaskId& task,
        CommandList& commandList,
        const GpuTaskRecordContext& context,
        const PacketRecordingLease& lease,
        bool& outRecordThunkInvoked
    )const;

    // Claims all packet tasks before native command recording begins. One recording attempt can therefore have
    // exactly one native artifact per packet even when callers use separate recorded-graph outputs concurrently.
    // The returned opaque lease authenticates the one recorder that may invoke task thunks, complete, or abort it.
    [[nodiscard]] bool beginPacketRecording(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        PacketRecordingLease& outLease
    )const noexcept;
    // A claimed native packet becomes submission-eligible only after every one of its task record thunks completed.
    [[nodiscard]] bool completePacketRecording(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketRecordingLease& lease
    )const noexcept;
    // Transfers a failed recorder's exact claim without invoking user code or changing task lifecycle on its worker.
    [[nodiscard]] bool deferPacketRecordingAbort(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketRecordingLease& lease,
        PacketRecordingAbort& outAbort
    )const noexcept;
    // Invokes typed discard callbacks and finalizes one transferred abort. Callers choose the serial drain order.
    [[nodiscard]] bool completePacketRecordingAbort(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        PacketRecordingAbort& abort
    )const;
    // Exception unwind consumes exact recording capabilities without invoking extensible discard observers.
    [[nodiscard]] bool abandonPacketRecordingWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketRecordingLease& lease
    )const noexcept;
    [[nodiscard]] bool abandonPacketRecordingAbortWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        PacketRecordingAbort& abort
    )const noexcept;
    // Abandons an active packet claim after its owning recorder has stopped invoking task thunks. Ordinary
    // transaction cleanup deliberately cannot discard a Recording packet, because that would allow a retry to
    // re-arm graph payload while the original recorder still owns it.
    void abortPacketRecording(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketRecordingLease& lease
    )const;
    [[nodiscard]] bool packetReadyForSubmission(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration
    )const noexcept;

private:
    [[nodiscard]] bool beginPacketSubmission(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding,
        PacketSubmissionLease& outLease
    )const noexcept;
    void beginPacketSubmissionAcceptance(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        PacketSubmissionLease& lease
    )const noexcept;
    void notifyPacketSubmissionAccepted(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        const QueueSubmissionToken& token,
        const PacketSubmissionLease& lease
    )const;
    void completePacketSubmissionAcceptance(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketSubmissionLease& lease
    )const noexcept;
    void abortPacketSubmission(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketSubmissionLease& lease
    )const;
    [[nodiscard]] bool abandonPacketSubmissionWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        PacketSubmissionLease& lease
    )const noexcept;
    // Atomically discards one non-recording packet. A packet with an in-flight Recording claim is left intact so
    // transaction cancellation cannot race native command recording or reopen the graph for a retry.
    [[nodiscard]] bool discardUnacceptedPacket(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const;
    // Unexpected observer failure must resolve the graph-owned attempt without invoking another extensible
    // callback while the original exception is active. Payload destruction remains owned by normal graph teardown.
    [[nodiscard]] bool abandonUnacceptedPacketWithoutCallbacks(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        GpuSubmissionPacketId packet,
        u64 recordingAttemptGeneration,
        const GpuGraphSubmissionBinding& submissionBinding
    )const noexcept;

private:
    // Lowers a compiler-owned packet-boundary barrier through the existing CommandList state tracker.  Task thunks
    // retain responsibility only for barriers internal to their own command sequence.
    [[nodiscard]] bool applyCompiledBarrier(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const PacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        const GpuCompiledBarrier& barrier,
        CommandList& commandList
    )const;
    // Materializes retained resource states in the native tracker after compiler barriers lower. This preserves
    // graph-owned packet handoffs when a required state already matches an imported automatic-state resource and
    // therefore needs no Vulkan transition command.
    [[nodiscard]] bool seedTaskRetainedResourceStates(
        const GpuCompiledGraph& compiledGraph,
        const GpuCompiledGraph::ReadView& planAccess,
        const PacketRecordingAccess& recordingAccess,
        const GpuTaskId& task,
        CommandList& commandList
    )const;

private:
    template<typename TaskT>
    static bool RecordPayload(
        const void* const payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        using Payload = typename TaskT::Payload;
        return TaskT::record(*static_cast<const Payload*>(payload), commandList, context);
    }
    template<typename TaskT>
    static void AcceptPayload(void* const payload, const QueueSubmissionToken& token){
        using Payload = typename TaskT::Payload;
        TaskT::accepted(*static_cast<Payload*>(payload), token);
    }
    template<typename TaskT>
    static void DiscardPayload(void* const payload){
        using Payload = typename TaskT::Payload;
        TaskT::discarded(*static_cast<Payload*>(payload));
    }
    template<typename PayloadT>
    static void DestroyPayload(GraphicsArena& arena, void* payload)noexcept{
        static_assert(noexcept(DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload))));
        DestroyArenaObjectNoexcept(arena, static_cast<PayloadT*>(payload));
    }

private:
    [[nodiscard]] bool validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] bool validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] bool validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] bool validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept;
    [[nodiscard]] bool validResourceSet(const GpuGraphResourceSetId& id)const noexcept;
    [[nodiscard]] bool validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] bool validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] bool validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] GpuTaskGraphTaskView taskAt(usize index)const;
    [[nodiscard]] GpuTaskGraphResourceView resourceAt(usize index)const;
    [[nodiscard]] GpuTaskGraphResourceVersionView resourceVersionAt(usize index)const;
    [[nodiscard]] GpuTaskGraphResourceSetView resourceSetAt(usize index)const;
    [[nodiscard]] GpuTaskGraphPipelineView pipelineAt(usize index)const;
    [[nodiscard]] GpuTaskGraphExternalCompletionView externalCompletionAt(usize index)const;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(
        const GpuExternalCompletionId& completion
    )const noexcept;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const noexcept;
    [[nodiscard]] const void* uploadBlobData(const GpuUploadBlobId& blob, usize& outByteSize)const noexcept;
    [[nodiscard]] GraphicsPipeline* graphicsPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] MeshletPipeline* meshletPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;
    [[nodiscard]] RayTracingPipeline* rayTracingPipelineFor(const GpuGraphPipelineId& pipeline)const noexcept;

    [[nodiscard]] GpuTaskId appendTaskWithinMutation(
        const GpuTaskDesc& desc,
        void* payload,
        GpuTaskRecordThunk recordPayload,
        GpuTaskAcceptedThunk acceptPayload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload,
        usize payloadObjectSize,
        const DeclarationMutationScope& mutationAccess
    );
    void discardAndDestroyUnappendedPayload(
        void* payload,
        GpuTaskDiscardedThunk discardPayload,
        GpuTaskPayloadDestroyThunk destroyPayload
    );
    [[nodiscard]] u32 findResourceIdentity(const Name& identity)const noexcept;
    // A null identity requests only the first matching typed pointer, without import metadata validation.
    [[nodiscard]] ResourceImportMatch findResourceImportMatch(
        const NameHash* identity,
        const ResourcePointerKey& pointer
    )const noexcept;
    [[nodiscard]] u32 findResourceSetIdentity(const Name& identity)const noexcept;
    void prepareResourceIndexes(const ResourcePointerKey& pendingPointer);
    void prepareResourceSetIndex();
    [[nodiscard]] GpuGraphResourceId appendResourceWithinMutation(
        const GpuGraphResourceDesc& desc,
        const ResourceQueueAdmissionSnapshot* queueAdmission,
        const ResourceBinding& binding,
        const DeclarationMutationScope& mutationAccess
    );
    [[nodiscard]] GpuGraphResourceVersionId appendResourceVersion(const GpuGraphResourceVersionDesc& desc);
    [[nodiscard]] GpuGraphResourceSetId appendResourceSet(const GpuGraphResourceSetDesc& desc);
    [[nodiscard]] GpuGraphPipelineId appendPipeline(const GpuGraphPipelineDesc& desc);
    [[nodiscard]] GpuExternalCompletionId appendExternalCompletion(const GpuExternalCompletionDesc& desc);
    [[nodiscard]] const GpuUploadBlobNode* findUploadBlob(const GpuUploadBlobId& blob)const noexcept;
    [[nodiscard]] bool appendMarkerLabel(AStringView text, u32& outOffset, u32& outSize);
    [[nodiscard]] AStringView markerLabel(u32 offset, u32 size)const;
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
    GraphicsVector<GpuExternalCompletionId> m_externalDependencies;
    GraphicsVector<GpuTaskExternalStateSource> m_externalStateSources;
    GraphicsVector<CommandListResourceStateHandoff*> m_externalStateSnapshots;
    GraphicsVector<GpuTaskResourceUse> m_resourceUses;
    GraphicsVector<GpuTaskResourceVersionUse> m_resourceVersionUses;
    GraphicsVector<GpuGraphResourceNode> m_resources;
    // Ordinals refer to immutable declarations in the current generation. Small graphs use their bounded existing
    // node prefix; promoted indexes retain capacity across reset without retaining extra resource handles or Names.
    Optional<ResourceIdentityIndex> m_resourceIdentityIndex;
    Optional<ResourcePointerIndex> m_resourcePointerIndex;
    GraphicsVector<GpuGraphResourceVersionNode> m_resourceVersions;
    GraphicsVector<GpuTaskGraphInitialOwnerHandoffSourceView> m_initialOwnerHandoffSources;
    GraphicsVector<u32> m_queueFamilyIndices;
    GraphicsVector<GpuGraphResourceSetNode> m_resourceSets;
    Optional<ResourceIdentityIndex> m_resourceSetIdentityIndex;
    GraphicsVector<GpuGraphResourceId> m_resourceSetMembers;
    GraphicsVector<GpuGraphPipelineNode> m_pipelines;
    GraphicsVector<GpuExternalCompletionNode> m_externalCompletions;
    GraphicsVector<GpuUploadBlobNode> m_uploadBlobs;
    GraphicsBytes m_markerText;
    GpuPresentEndpoint m_presentEndpoint;
    u64 m_generation = 0u;
    u64 m_declarationRevision = 0u;
    mutable u32 m_activeDeclarationAccessCount = 0u;
    mutable u32 m_activeDeclarationReadCount = 0u;
    mutable u32 m_activeDiscardNotificationCount = 0u;
    mutable Atomic<u32> m_activePacketRecordingClaimCount{ 0u };
    mutable u64 m_activeRecordingAttemptGeneration = 0u;
    mutable u64 m_activeRecordingPlanGeneration = 0u;
    mutable u64 m_activeRecordingPreparationSerial = 0u;
    mutable const GpuCompiledGraph* m_activeCompiledGraph = nullptr;
    mutable GpuGraphSubmissionBinding m_activeSubmissionBinding;
    mutable SubmissionBindingState m_submissionBindingState = SubmissionBindingState::None;
    bool m_hasPresentEndpoint = false;
    mutable bool m_teardownInProgress = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

