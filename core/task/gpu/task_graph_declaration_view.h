// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compiled_graph.h"
#include "task_desc.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTaskGraph;
class GpuTaskGraphAnalysis;
class GpuTaskGraphQueueAssignments;
class GpuTaskGraphQueueAssignmentTelemetryTracker;

namespace Telemetry{
    class FrameGraphBuilder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuTaskGraphTaskView{
    GpuTaskId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    GpuTaskCommandRequirements commands;
    GpuTaskSchedulingHint scheduling;
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
    GpuTaskTimingMetadata timing;
    bool hasPayload = false;
    bool hasRecordPayload = false;
    bool hasAcceptedPayload = false;
};

// Immutable view of an externally released resource range, copied at declaration so producer storage may retire.
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
    Name identity = s_NameNone;
    AStringView markerLabel;
    // Each first-use range must be covered by one source naming its exact physical consumer queue.
    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    GpuExternalCompletionId initialAvailabilityCompletion;
    // Typed resource imports own an exact copy of the backend's immutable physical admission facts. Metadata-only resources have no snapshot and retain the logical queue-sharing resolver.
    ResourceQueueAdmissionSnapshot queueAdmission;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    GpuPhysicalQueueId initialOwnerQueue;
    GpuPhysicalQueueId directConsumerQueue;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
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
    Name identity = s_NameNone;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;
};

struct GpuTaskGraphPipelineView{
    GpuGraphPipelineId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    bool hasBackendPipeline = false;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::kCount;
};

struct GpuTaskGraphExternalCompletionView{
    GpuExternalCompletionId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    bool hasToken = false;
    QueueSubmissionToken token;
};

// Graph-declared presentation completion: retained backbuffer in Unknown/Present, exported to Present only. Users reach the producer on its exact queue; producers may skip use for publish.
struct GpuPresentEndpoint{
    GpuTaskId producer;
    GpuGraphResourceId backBuffer;
};

struct GpuTaskGraphTelemetryOptions{
    const GpuTaskGraphQueueAssignments* queueAssignments = nullptr;
    const GpuCompiledGraph::ReadView* compiledPlan = nullptr;
    const GpuTaskGraphQueueAssignmentTelemetryTracker* queueAssignmentTelemetry = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Lexical proof that immutable declaration storage cannot be reset or mutated. Compiler/tooling callers acquire a waiting view before any other operation gate
class GpuTaskGraphDeclarationReadView final : NoCopy{
    friend class GpuCompiledGraph::ReadView;
    friend class GpuTaskGraph;


private:
    struct TryAcquireTag{};


public:
    [[nodiscard]] static GpuTaskGraphDeclarationReadView TryAcquire(const GpuTaskGraph& graph)noexcept;


public:
    explicit GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph);
    GpuTaskGraphDeclarationReadView(GpuTaskGraphDeclarationReadView&&) = delete;
    ~GpuTaskGraphDeclarationReadView()noexcept;
private:
    GpuTaskGraphDeclarationReadView(const GpuTaskGraph& graph, TryAcquireTag)noexcept;


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
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(const GpuExternalCompletionId& completion)const & noexcept;
    [[nodiscard]] const QueueSubmissionToken* externalCompletionToken(const GpuExternalCompletionId& completion)const && = delete;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] Texture* textureForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] Buffer* bufferForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const & noexcept;
    [[nodiscard]] RayTracingAccelStruct* accelStructForResource(const GpuGraphResourceId& resource)const && = delete;
    [[nodiscard]] Expected<BinaryByteView> uploadBlobData(const GpuUploadBlobId& blob)const & noexcept;
    [[nodiscard]] Expected<BinaryByteView> uploadBlobData(const GpuUploadBlobId& blob)const && = delete;
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

    [[nodiscard]] bool appendFrameGraphTelemetry(Telemetry::FrameGraphBuilder& builder, const GpuTaskGraphAnalysis& analysis, Alloc::ScratchArena& scratchArena, const GpuTaskGraphTelemetryOptions& options = {})const;


private:
    void acquire(const GpuTaskGraph& graph)noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

