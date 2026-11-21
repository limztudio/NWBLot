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
    b8 hasPayload = false;
    b8 hasRecordPayload = false;
    b8 hasAcceptedPayload = false;

    void setResourceUses(Span<const GpuTaskResourceUse> uses)noexcept{
        resourceUses = uses.data();
        resourceUseCount = uses.size();
    }
    [[nodiscard]] b8 hasDependencies()const noexcept{ return dependencies != nullptr; }
    [[nodiscard]] Span<const GpuTaskId> dependenciesSpan()const noexcept{
        return dependencies ? Span<const GpuTaskId>(dependencies, dependencyCount) : Span<const GpuTaskId>();
    }
    [[nodiscard]] b8 hasExternalDependencies()const noexcept{ return externalDependencies != nullptr; }
    [[nodiscard]] Span<const GpuExternalCompletionId> externalDependenciesSpan()const noexcept{
        return externalDependencies ? Span<const GpuExternalCompletionId>(externalDependencies, externalDependencyCount) : Span<const GpuExternalCompletionId>();
    }
    [[nodiscard]] b8 hasExternalStateSources()const noexcept{ return externalStateSources != nullptr; }
    [[nodiscard]] Span<const GpuTaskExternalStateSource> externalStateSourcesSpan()const noexcept{
        return externalStateSources ? Span<const GpuTaskExternalStateSource>(externalStateSources, externalStateSourceCount) : Span<const GpuTaskExternalStateSource>();
    }
    [[nodiscard]] b8 hasResourceUses()const noexcept{ return resourceUses != nullptr; }
    [[nodiscard]] Span<const GpuTaskResourceUse> resourceUsesSpan()const noexcept{
        return resourceUses ? Span<const GpuTaskResourceUse>(resourceUses, resourceUseCount) : Span<const GpuTaskResourceUse>();
    }
    [[nodiscard]] b8 hasResourceVersionUses()const noexcept{ return resourceVersionUses != nullptr; }
    [[nodiscard]] Span<const GpuTaskResourceVersionUse> resourceVersionUsesSpan()const noexcept{
        return resourceVersionUses ? Span<const GpuTaskResourceVersionUse>(resourceVersionUses, resourceVersionUseCount) : Span<const GpuTaskResourceVersionUse>();
    }
};

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
    const GpuTaskGraphInitialOwnerHandoffSourceView* initialOwnerHandoffSources = nullptr;
    usize initialOwnerHandoffSourceCount = 0u;
    GpuExternalCompletionId initialAvailabilityCompletion;
    ResourceQueueAdmissionSnapshot queueAdmission;
    ResourceStates::Mask initialState = ResourceStates::Unknown;
    ResourceStates::Mask externalFinalState = ResourceStates::Unknown;
    GpuPhysicalQueueId externalFinalReleaseDestinationQueue;
    GpuPhysicalQueueId initialOwnerQueue;
    GpuPhysicalQueueId directConsumerQueue;
    GpuGraphResourceType::Enum type = GpuGraphResourceType::HazardDomain;
    ResourceQueueSharing::Mask queueSharing = ResourceQueueSharing::Exclusive;
    b8 hasQueueAdmission = false;
    b8 hasBackendResource = false;
    [[nodiscard]] b8 hasInitialOwnerHandoffSources()const noexcept{ return initialOwnerHandoffSources != nullptr; }
    [[nodiscard]] Span<const GpuTaskGraphInitialOwnerHandoffSourceView> initialOwnerHandoffSourcesSpan()const noexcept{
        return initialOwnerHandoffSources ? Span<const GpuTaskGraphInitialOwnerHandoffSourceView>(initialOwnerHandoffSources, initialOwnerHandoffSourceCount) : Span<const GpuTaskGraphInitialOwnerHandoffSourceView>();
    }
};

struct GpuTaskGraphResourceVersionView{
    GpuGraphResourceVersionId id;
    GpuGraphResourceId resource;
    GpuTaskResourceRange range;
    GpuGraphResourceVersionOrigin::Enum origin = GpuGraphResourceVersionOrigin::Count;
};

struct GpuTaskGraphResourceSetView{
    GpuGraphResourceSetId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    const GpuGraphResourceId* members = nullptr;
    usize memberCount = 0u;

    [[nodiscard]] Span<const GpuGraphResourceId> memberSpan()const noexcept{ return { members, memberCount }; }
    [[nodiscard]] b8 hasMembers()const noexcept{ return members != nullptr; }
    [[nodiscard]] Span<const GpuGraphResourceId> membersSpan()const noexcept{
        return members ? Span<const GpuGraphResourceId>(members, memberCount) : Span<const GpuGraphResourceId>();
    }
};

struct GpuTaskGraphPipelineView{
    GpuGraphPipelineId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    b8 hasBackendPipeline = false;
    GpuGraphPipelineType::Enum type = GpuGraphPipelineType::Count;
};

struct GpuTaskGraphExternalCompletionView{
    GpuExternalCompletionId id;
    Name identity = s_NameNone;
    AStringView markerLabel;
    b8 hasToken = false;
    QueueSubmissionToken token;
};

struct GpuPresentEndpoint{
    GpuTaskId producer;
    GpuGraphResourceId backBuffer;
};

struct GpuTaskGraphTelemetryOptions{
    const GpuTaskGraphQueueAssignments* queueAssignments = nullptr;
    const GpuCompiledGraph::ReadView* compiledPlan = nullptr;
    const GpuTaskGraphQueueAssignmentTelemetryTracker* queueAssignmentTelemetry = nullptr;
    void setQueueAssignments(const GpuTaskGraphQueueAssignments& value)noexcept{ queueAssignments = &value; }
    void setCompiledPlan(const GpuCompiledGraph::ReadView& value)noexcept{ compiledPlan = &value; }
    void setQueueAssignmentTelemetry(const GpuTaskGraphQueueAssignmentTelemetryTracker& value)noexcept{ queueAssignmentTelemetry = &value; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphCompilerDetail{
    struct CompilerGraphAccess;
};


class GpuTaskGraphDeclarationReadView final : NoCopy{
    friend class GpuCompiledGraph::ReadView;
    friend class GpuTaskGraph;
    friend struct GpuTaskGraphCompilerDetail::CompilerGraphAccess;


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
    [[nodiscard]] b8 valid()const noexcept{ return m_graph != nullptr; }

    [[nodiscard]] b8 validFor(const GpuTaskGraph& graph)const noexcept{ return m_graph == &graph; }

    [[nodiscard]] u64 generation()const noexcept;
    [[nodiscard]] u64 declarationRevision()const noexcept;
    [[nodiscard]] b8 validForDeviceGeneration(u16 deviceGeneration)const noexcept;
    [[nodiscard]] b8 validTask(const GpuTaskId& id)const noexcept;
    [[nodiscard]] b8 validResource(const GpuGraphResourceId& id)const noexcept;
    [[nodiscard]] b8 validResourceVersion(const GpuGraphResourceVersionId& id)const noexcept;
    [[nodiscard]] b8 validResourceSet(const GpuGraphResourceSetId& id)const noexcept;
    [[nodiscard]] b8 validUploadBlob(const GpuUploadBlobId& id)const noexcept;
    [[nodiscard]] b8 validPipeline(const GpuGraphPipelineId& id)const noexcept;
    [[nodiscard]] b8 validExternalCompletion(const GpuExternalCompletionId& id)const noexcept;
    [[nodiscard]] usize taskCount()const noexcept{ return m_taskCount; }
    [[nodiscard]] usize resourceCount()const noexcept{ return m_resourceCount; }
    [[nodiscard]] usize resourceVersionCount()const noexcept;
    [[nodiscard]] usize resourceSetCount()const noexcept;
    [[nodiscard]] usize uploadBlobCount()const noexcept;
    [[nodiscard]] usize pipelineCount()const noexcept;
    [[nodiscard]] usize externalCompletionCount()const noexcept;
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
    [[nodiscard]] Optional<QueueSubmissionToken> findCompletionToken(GpuExternalCompletionId completion)const noexcept{
            if(const auto* result = externalCompletionToken(completion))
                return *result;
            return {};
        }
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
    [[nodiscard]] b8 hasComputePipeline(const GpuGraphPipelineId& pipeline)const noexcept{ return computePipelineFor(pipeline) != nullptr; }
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const & noexcept;
    [[nodiscard]] ComputePipeline* computePipelineFor(const GpuGraphPipelineId& pipeline)const && = delete;
    [[nodiscard]] Optional<NotNull<Texture*>> borrowTexture(const GpuGraphResourceId& id)const & noexcept{
        if(Texture* resource = textureForResource(id))
            return MakeNotNull(resource);
        return {};
    }
    [[nodiscard]] Optional<NotNull<Texture*>> borrowTexture(const GpuGraphResourceId&)const && = delete;
    [[nodiscard]] Optional<NotNull<GraphicsPipeline*>> borrowGraphicsPipeline(const GpuGraphPipelineId& id)const & noexcept{
        if(GraphicsPipeline* resource = graphicsPipelineFor(id))
            return MakeNotNull(resource);
        return {};
    }
    [[nodiscard]] Optional<NotNull<GraphicsPipeline*>> borrowGraphicsPipeline(const GpuGraphPipelineId&)const && = delete;
    [[nodiscard]] Optional<NotNull<ComputePipeline*>> borrowComputePipeline(const GpuGraphPipelineId& id)const & noexcept{
        if(ComputePipeline* pipeline = computePipelineFor(id))
            return MakeNotNull(pipeline);
        return {};
    }
    [[nodiscard]] Optional<NotNull<ComputePipeline*>> borrowComputePipeline(const GpuGraphPipelineId&)const && = delete;
    [[nodiscard]] Optional<NotNull<Buffer*>> borrowBuffer(const GpuGraphResourceId& id)const & noexcept{
        if(Buffer* buffer = bufferForResource(id))
            return MakeNotNull(buffer);
        return {};
    }
    [[nodiscard]] Optional<NotNull<Buffer*>> borrowBuffer(const GpuGraphResourceId&)const && = delete;
    [[nodiscard]] GpuGraphResourceId findImportedTexture(const TextureHandle& texture)const noexcept;
    [[nodiscard]] GpuGraphResourceId findImportedBuffer(const BufferHandle& buffer)const noexcept;
    [[nodiscard]] Optional<GpuPresentEndpoint> findPresentEndpoint()const noexcept{
            if(const auto* result = presentEndpoint())
                return *result;
            return {};
        }
        [[nodiscard]] const GpuPresentEndpoint* presentEndpoint()const & noexcept;
    [[nodiscard]] const GpuPresentEndpoint* presentEndpoint()const && = delete;

    [[nodiscard]] b8 appendAcceptedFrameGraphTelemetry(
        const GpuTaskGraphAnalysis& analysis, Telemetry::FrameGraphBuilder& builder, const GpuTaskGraphQueueAssignments& assignments,
        const GpuCompiledGraph::ReadView& compiledPlan, const GpuTaskGraphQueueAssignmentTelemetryTracker& telemetry
    )const;
    [[nodiscard]] b8 appendFrameGraphTelemetry(const GpuTaskGraphAnalysis& analysis, Alloc::ScratchArena& scratchArena, Telemetry::FrameGraphBuilder& builder, const GpuTaskGraphTelemetryOptions& options = {})const;


private:
    void acquire(const GpuTaskGraph& graph)noexcept;


private:
    const GpuTaskGraph* m_graph = nullptr;
    u32 m_taskCount = 0u;
    u32 m_resourceCount = 0u;
    u8 m_readerStripeIndex = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Visit>
[[nodiscard]] b8 VisitGpuTaskTexture(const GpuTaskGraphDeclarationReadView& declarations, const GpuGraphResourceId& resource, Visit&& visit){
    Texture* const texture = declarations.textureForResource(resource);
    return texture && visit(*texture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

