// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_contract.h"
#include "task_desc.h"
#include "task_graph_declaration_view.h"

#include <global/arena_object.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

struct TaskLifecycleState{
    enum Enum : u8{
        Declared,
        Recording,
        Recorded,
        Submitting,
        Accepting,
        Accepted,
        Discarded,
    };
};

struct SubmissionBindingState{
    enum Enum : u8{
        None,
        Active,
        ExceptionClosing,
        Resolved,
    };
};

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
    mutable TaskLifecycleState::Enum lifecycleState = TaskLifecycleState::Declared;
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
    GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::Count;
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
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::Count;
    GraphicsPipelineHandle graphicsPipeline;
    ComputePipelineHandle computePipeline;
    MeshletPipelineHandle meshletPipeline;
    RayTracingPipelineHandle rayTracingPipeline;
};

struct PipelinePointerKey{
    const void* pointer = nullptr;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::Count;
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

struct MarkerLabelRange{
    u32 offset = 0u;
    u32 size = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

