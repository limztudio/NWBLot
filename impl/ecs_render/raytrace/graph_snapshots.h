// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/types.adhd.h>
#include <impl/ecs_render/shadow/light_space_shadow.h>
#include <impl/ecs_render/refraction/statistics_readback.h>

#include <core/task/gpu/task_graph.h>

#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Freeze adaptive software-shadow compaction before graph compilation so its append-counter clear is a graph primitive.
struct PreparedShadowMaterialContextUploads{
    Core::GpuUploadBlobId instanceMaterial;
    Core::GpuUploadBlobId instance;
    Core::GpuUploadBlobId materialTyped;
};

struct PreparedSceneBvhUploads{
    Core::GpuUploadBlobId node;
    Core::GpuUploadBlobId instance;
};


// Shadow Visibility freezes its route policy before the coordinator declares topology. Handles stay in snapshots below.
struct RayTracingShadowVisibilityGraphPlanSnapshot{
    LightSpaceShadowSnapshot lightSpace;
    GraphOwnedAdaptiveShadowPlan adaptivePlan;

    b8 hardwareTransparentTrace = false;
    b8 softTransparentFoldReady = false;
    b8 combinedSoftUpsample = false;
    b8 combinedSoftWaveletReady = false;
    b8 combinedSoftTemporalReady = false;
    b8 softShadowHistoryReadable = false;
    b8 opaqueTemporalMergeReady = false;
    b8 transparentTemporalMergeReady = false;
    b8 historyFrontIsA = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One packet may advance several temporal domains at once. Keep exact CPU mirrors so each rejected stage rolls back only itself.
struct RayTracingFrameCpuStateSnapshot{
    Core::QueueSubmissionToken surfelCountReadbackSubmissionToken;

    u32 softShadowFrameIndex = 0u;
    u32 surfelFrameIndex = 0u;
    u32 surfelCountReadbackFrame = 0u;
    u32 softShadowSlotMask = 0u;
    u32 causticLightCount = 0u;

    b8 swShadowDispatchLogged = false;
    b8 causticAccumulatorInitialized = false;
    b8 swCausticDispatchLogged = false;
    b8 hwCausticDispatchLogged = false;
    b8 causticEmissionGateLogged = false;
    b8 surfelSeeded = false;
};


// Shadow Preparation keeps TLAS, software-BVH scratch, adaptive-shadow scratch across packets. Coordinator gets handles only.
struct RayTracingShadowPreparationResourceSnapshot{
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::BufferHandle sceneTlasBackingBuffer;
    Core::BufferHandle bvhSortKeysBuffer;
    Core::BufferHandle bvhSortPayloadBuffer;
    Core::BufferHandle bvhVisitCounterBuffer;
    Core::BufferHandle swShadowEdgeCounterBuffer;
    Core::BufferHandle swShadowEdgeListBuffer;
    Core::BufferHandle swShadowIndirectArgsBuffer;
};


// Graph declaration freezes the descriptor-visible effects resources and route decisions selected by preflight.
// Keeping this read-only snapshot separate from CPU rollback state prevents the coordinator from mutating caches.
struct RayTracingDeferredGraphResourceSnapshot{
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle csgTraceContextBuffer;
    Span<const Core::QueueSubmissionToken> rayTraceContextReadSubmissionTokens{};
    Span<const Core::BufferHandle> csgTraceBoundsBuffers{};
    Core::BufferHandle shadowInstanceMaterialBuffer;
    Core::BufferHandle shadowMaterialTypedBuffer;
    Core::BufferHandle shadowInstanceBuffer;
    Core::BufferHandle causticEmissionTargetBuffer;
    Core::BufferHandle surfelFrameConstantsBuffer;
    Core::BufferHandle hardwareTransparentCrossingsBuffer;
    Core::BufferHandle hardwareTransparentOverflowListBuffer;
    Core::BufferHandle hardwareTransparentOverflowArgsBuffer;
    Core::BufferHandle sceneBvhNodeBuffer;
    Core::BufferHandle sceneInstanceBuffer;
    Core::RayTracingAccelStructHandle sceneTlas;

    b8 causticAccumulatorInitialized = false;
    b8 surfelUsesHardwareTrace = false;
    b8 surfelSplitGraphPipelinesReady = false;
};


// Surfel resources outlive resizable targets. The accepted readback token shares its generation with the counter/readback pair.
struct RayTracingSurfelPersistentResourceSnapshot{
    Core::BufferHandle constantsBuffer;
    Core::BufferHandle poolBuffer;
    Core::BufferHandle guidePoolBuffer;
    Core::BufferHandle cellHeadBuffer;
    Core::BufferHandle counterBuffer;
    Core::BufferHandle traceIndirectArgsBuffer;
    Core::BufferHandle freeListBuffer;
    Core::BufferHandle poolSnapshotBuffer;
    Core::BufferHandle cellHeadSnapshotBuffer;
    Core::BufferHandle counterReadbackBuffer;
    Core::QueueSubmissionToken countReadbackSubmissionToken;
};

// Prepared once before compilation. Recording consumes retained handles only, so HW failure cannot switch sets after declare.
struct RayTracingRefractionGraphResources{
    RefractionStatisticsReadbackSnapshot statistics;
    Core::ComputePipelineHandle pipeline;
    Core::ComputePipelineHandle densePipeline;
    Core::ComputePipelineHandle screenFallbackPipeline;
    Core::ComputePipelineHandle denseScreenFallbackPipeline;
    Core::ComputePipelineHandle classifyTilesPipeline;
    Core::ComputePipelineHandle finalizeTilesPipeline;
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle viewBuffer;
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::GpuDescriptorHandle tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
    u32 materialContextSlotsHeapSlot = 0u;
    u32 viewHeapSlot = 0u;
    u32 opaqueReflectionSlot = Core::GpuDescriptorHandle::s_Invalid;
    u32 certifiedInstanceIndex = Limit<u32>::s_Max;
    b8 refractionEnabled = true;
    b8 usesHardwareTrace = false;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return pipeline && densePipeline && classifyTilesPipeline && finalizeTilesPipeline && viewBuffer && (!usesHardwareTrace || (sceneTlas && tlasHeapHandle.valid() && materialContextSlotsBuffer));
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
