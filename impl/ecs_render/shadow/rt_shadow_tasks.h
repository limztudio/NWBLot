// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/task/gpu/compiled_graph.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingShadowVisibilityTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both opaque resolve callbacks share one all-lit fallback: clear visibility, realign UAV state, retire timing.
template<typename PayloadT>
inline void RecoverOpaqueSoftShadowAllLit(const PayloadT& payload, Core::CommandList& commandList){
    payload.raytracingSystem.clearShadowVisibility(commandList, payload.targets);
    // Skipped transparent callbacks still declare this output as UAV. Realign the tracker after the typed fallback clear.
    commandList.setTextureState(payload.targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    payload.opaqueProduced = false;
    if(payload.asyncTiming.has_value()){
        payload.asyncTiming.value().discardTiming();
        payload.asyncTiming.reset();
    }
    if(payload.shadowVisibilityTiming.has_value()){
        payload.shadowVisibilityTiming.value().discardTiming();
        payload.shadowVisibilityTiming.reset();
    }
    payload.opaqueResolveTiming.value().discardTiming();
    payload.opaqueResolveTiming.reset();
}

template<typename PayloadT>
inline void DiscardOpaqueSoftShadowResolve(PayloadT& payload)noexcept{
    payload.opaqueProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.asyncTiming);
    Core::DiscardGpuTimingMeasure(&payload.shadowVisibilityTiming);
    Core::DiscardGpuTimingMeasure(&payload.opaqueResolveTiming);
}

template<typename PayloadT>
inline void DiscardTransparentSoftShadowResolve(PayloadT& payload)noexcept{
    payload.transparentTraceProduced = false;
    Core::DiscardGpuTimingMeasure(&payload.transparentResolveTiming);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A prepared frame records production, resolve, trace, fold in one packet. Shared state is stack-owned; never survives retry.
struct ShadowVisibilityOpaqueGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: opaque producer always records against this frame's ticket and brackets the shared async/shadow timing.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        b8 hardwareShadowSupported = false;
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        const b8& prepared;
        b8& opaqueProduced;
        u32& opaqueFrameIndex;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Discarded(Payload& payload)noexcept;
};


// The opaque producer leaves the trace and current geometry scratch in their declared states.
// This adjacent task owns their sampled entry before temporal merge and, unless fused later, the first wavelet.
// Its tail retains dynamic ping-pong and upsample work, while the terminal transparent fold remains the output/acceptance owner.
struct ShadowVisibilityOpaqueFirstWaveletGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: first wavelet always brackets the opaque-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
        b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8 hardwareShadowSupported = false;
        b8 graphOwnsOpaqueTemporalMergeEntryStates = false;
        b8 deferUpsample = false;
        b8 deferWavelet = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardOpaqueSoftShadowResolve(payload);
    }
};


struct ShadowVisibilityOpaqueResolveTailGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: resolve tail always closes the opaque-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& opaqueResolveTiming;
        b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardOpaqueSoftShadowResolve(payload);
    }
};


// The prepared path keeps trace and resolve apart so the graph lowers transparent output from UAV to shader-read.
struct ShadowTransparentSoftTraceGraphTask{
    // Transparent hardware fallback also initializes its indirect dispatch arguments through a buffer upload.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };


public:
    // All bindings are required: transparent trace always records against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        LightSpaceShadowSnapshot lightSpace;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& opaqueProduced;
        const u32& opaqueFrameIndex;
        b8& transparentTraceProduced;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        payload.transparentTraceProduced = false;
    }
};


// The transparent trace publishes half-res RGB visibility. On temporal frames it freezes the history pair, publishes the next pair, starts resolve.
struct ShadowTransparentSoftTemporalMergeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: temporal merge always brackets the transparent-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 graphOwnsTransparentTemporalMergeEntryStates = false;
        b8 combinedTemporal = false;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardTransparentSoftShadowResolve(payload);
    }
};


// Trace or merge publishes the first wavelet input. The terminal fold keeps the final upsample plus output/acceptance.
struct ShadowTransparentSoftFirstWaveletGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: first wavelet always brackets the transparent-resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 graphOwnsTransparentWaveletInputBoundary = false;
        b8 startsTransparentResolveTiming = true;
        b8 combinedWavelet = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        DiscardTransparentSoftShadowResolve(payload);
    }
};


struct ShadowTransparentSoftFoldGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // All bindings are required: terminal fold always closes the shared async/shadow/resolve timing against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        Optional<Core::GpuTimingMeasure>& asyncTiming;
        Optional<Core::GpuTimingMeasure>& shadowVisibilityTiming;
        Optional<Core::GpuTimingMeasure>& transparentResolveTiming;
        const b8& opaqueProduced;
        b8& transparentTraceProduced;
        const u32& opaqueFrameIndex;
        b8 combinedUpsample = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Discarded(Payload& payload)noexcept;
};


// Shadow visibility owns its graph task; pipeline composes the caustics successor.
struct ShadowVisibilityGraphTask{
    // Transparent hardware fallback also initializes its indirect dispatch arguments through a buffer upload.
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, };


public:
    // All bindings are required: visibility always records against this frame's bindings.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GraphicsRuntime& graphics;
        DeferredFrameTargets& targets;
        DeferredLightingGraphResources deferredLightingResources;
        Core::GpuTimingSubmissionTicket& timingTicket;
        const b8& prepared;
        GraphOwnedAdaptiveShadowPlan graphOwnedAdaptivePlan;
        b8 hardwareShadowSupported = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload)noexcept{
        payload.raytracingSystem.discardSoftShadowTemporalHistory();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

