// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Adaptive software-shadow diagnostics are private scratch work.  The shared deferred graph freezes this small
// plan before compilation so its counter/stat clears and optional readback can be declared as first-class primitive
// tasks. Enabled may own only the acceptance-time tick when the current frame needs no primitive task.
struct GraphOwnedAdaptiveShadowPlan{
    bool enabled = false;
    bool compact = false;
    bool captureStatsSnapshot = false;
    u32 statsTick = 0u;
    bool* adaptiveRouteRecorded = nullptr;
};


// Shadow Visibility freezes its domain-owned route policy before the coordinator declares target and dependency
// topology. Resource handles remain in the dedicated resource snapshots below.
struct RayTracingShadowVisibilityGraphPlanSnapshot{
    GraphOwnedAdaptiveShadowPlan adaptivePlan;

    bool softTransparentFoldReady = false;
    bool softShadowHistoryReadable = false;
    bool opaqueTemporalMergeReady = false;
    bool transparentTemporalMergeReady = false;
    bool historyFrontIsA = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPreparationOutcome{
    bool resourcesValid = false;
    bool ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A shared packet may optimistically advance several independent temporal domains while recording. Preserve the
// exact CPU mirrors so each rejected semantic stage can roll back only the state it owned.
struct RayTracingFrameCpuStateSnapshot{
    Core::QueueSubmissionToken surfelCountReadbackSubmissionToken;

    u32 softShadowFrameIndex = 0u;
    u32 causticTemporalReuseFrameCount = 0u;
    u32 swCausticFrameIndex = 0u;
    u32 hwCausticFrameIndex = 0u;
    u32 surfelFrameIndex = 0u;
    u32 surfelCountReadbackFrame = 0u;
    u32 softShadowSlotMask = 0u;
    u32 causticLightCount = 0u;

    bool swShadowDispatchLogged = false;
    bool causticAccumulatorInitialized = false;
    bool swCausticDispatchLogged = false;
    bool hwCausticDispatchLogged = false;
    bool causticEmissionGateLogged = false;
    bool surfelSeeded = false;
};


// Shadow Preparation retains native state for the scene TLAS, shared software-BVH scratch, and adaptive-shadow
// scratch across packets. The coordinator receives owning handles without reaching into mutable domain state.
struct RayTracingShadowPreparationResourceSnapshot{
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::BufferHandle sceneTlasBackingBuffer;
    Core::BufferHandle bvhSortKeysBuffer;
    Core::BufferHandle bvhSortPayloadBuffer;
    Core::BufferHandle bvhVisitCounterBuffer;
    Core::BufferHandle swShadowEdgeStatsBuffer;
    Core::BufferHandle swShadowEdgeStatsReadback;
    Core::BufferHandle swShadowEdgeCounterBuffer;
    Core::BufferHandle swShadowEdgeListBuffer;
    Core::BufferHandle swShadowIndirectArgsBuffer;
};


// Graph declaration freezes the descriptor-visible effects resources and route decisions selected by preflight.
// Keeping this read-only snapshot separate from CPU rollback state prevents the coordinator from mutating caches.
struct RayTracingDeferredGraphResourceSnapshot{
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle shadowInstanceMaterialBuffer;
    Core::BufferHandle shadowMaterialTypedBuffer;
    Core::BufferHandle shadowInstanceBuffer;
    Core::BufferHandle causticEmissionTargetBuffer;
    Core::BufferHandle surfelFrameConstantsBuffer;
    Core::BufferHandle sceneBvhNodeBuffer;
    Core::BufferHandle sceneInstanceBuffer;
    Core::RayTracingAccelStructHandle sceneTlas;

    f32 causticTemporalDecay = 0.f;
    bool causticAccumulatorInitialized = false;
    bool surfelUsesHardwareTrace = false;
    bool surfelSplitGraphPipelinesReady = false;
};


// Persistent surfel resources outlive resizable targets. The accepted readback token belongs to the same resource
// generation as the counter/readback pair and is captured with it for graph dependency and rollback planning.
struct RayTracingSurfelPersistentResourceSnapshot{
    Core::BufferHandle constantsBuffer;
    Core::BufferHandle poolBuffer;
    Core::BufferHandle cellHeadBuffer;
    Core::BufferHandle counterBuffer;
    Core::BufferHandle traceIndirectArgsBuffer;
    Core::BufferHandle freeListBuffer;
    Core::BufferHandle poolSnapshotBuffer;
    Core::BufferHandle cellHeadSnapshotBuffer;
    Core::BufferHandle counterReadbackBuffer;
    Core::QueueSubmissionToken countReadbackSubmissionToken;
};

// Prepared once before graph compilation. Recording only consumes these retained handles and slot values, so a
// hardware failure cannot switch the graph to a different resource set after its dependencies have been declared.
struct RayTracingRefractionGraphResources{
    Core::ComputePipelineHandle pipeline;
    Core::ComputePipelineHandle screenFallbackPipeline;
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle viewBuffer;
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::GpuDescriptorHandle tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 materialContextSlotsHeapSlot = 0u;
    u32 viewHeapSlot = 0u;
    u32 opaqueReflectionSlot = 0xffffffffu;
    bool refractionEnabled = true;
    bool usesHardwareTrace = false;

    [[nodiscard]] bool valid()const noexcept{
        return pipeline && viewBuffer
            && (!usesHardwareTrace || (sceneTlas && tlasHeapHandle.valid() && materialContextSlotsBuffer));
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
