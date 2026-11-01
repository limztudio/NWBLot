// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/caustic/caustic_resolve_state.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;

struct DeferredFrameTargets;


// Shared prepare plus five-wavelet plus upsample plus timing-close resolve declaration.
using CausticsResolveStageUses = Span<const Core::GpuTaskResourceUse>;

struct CausticsResolveChainStageUseInputs{
    CausticsResolveStageUses prepare{};
    Array<CausticsResolveStageUses, s_CausticResolveWaveletPassCount> wavelets{};
    CausticsResolveStageUses upsample{};
};

struct CausticsResolveStageNaming{
    Name identity;
    AStringView label;
    TStringView warnText;
};

struct CausticsResolveChainInputs : public CausticsResolveChainStageUseInputs{
    DeferredFrameTargets& targets;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    Span<const Core::GpuTaskExternalStateSource> stateSources{};
    b8 applyStateSourcesToPrepareOnly = true;
    b8& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& resolveTiming;


public:
    CausticsResolveChainInputs(DeferredFrameTargets& targetsIn, b8& producerDispatchedIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& resolveTimingIn)noexcept
        : targets(targetsIn)
        , producerDispatched(producerDispatchedIn)
        , timingTicket(timingTicketIn)
        , resolveTiming(resolveTimingIn)
    {}
};

struct CausticsResolveChainStageTasks{
    Core::GpuTaskId causticResolvePrepareTask;
    Array<Core::GpuTaskId, s_CausticResolveWaveletPassCount> causticResolveWaveletTasks{};
    Core::GpuTaskId causticResolveUpsampleTask;
};

struct CausticsResolveChainResult : public CausticsResolveChainStageTasks{
    Core::GpuTaskId causticsTask;
};

struct CausticsResolveChainNaming{
    CausticsResolveStageNaming prepare;
    Array<CausticsResolveStageNaming, s_CausticResolveWaveletPassCount> wavelets{};
    CausticsResolveStageNaming upsample;
    CausticsResolveStageNaming timingClose;
};

struct CausticsStageTargetNaming{
    Name accumulator;
    Name history;
    Name resolveHalf;
    Name resolveGeometry;
    Name sceneGeometryDomain;
    AStringView sceneGeometryDomainLabel;
    TStringView importWarnText;
};

struct CausticsStageTargets{
    Core::GpuGraphResourceId causticAccumulator;
    Core::GpuGraphResourceId causticHistory;
    Core::GpuGraphResourceId causticResolveHalf;
    Core::GpuGraphResourceId causticResolveGeometry;
    Core::GpuGraphResourceId sceneGeometryDomain;
};

struct CausticsStagePrefixInputs{
    DeferredFrameTargets& targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewSnapshot;
    const b8& shadowPreparationReady;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& photonTiming;
    Span<const Core::GpuTaskId> initialDependencies{};
    Span<const Core::GpuExternalCompletionId> externalDependencies{};
    Span<const Core::GpuTaskExternalStateSource> irradianceClearStateSources{};
    Span<const Core::GpuTaskExternalStateSource> accumulatorStateSources{};
    Core::GpuGraphResourceId causticIrradiance;
    Core::GpuGraphResourceId causticAccumulator;
    b8 causticAccumulatorInitialized = false;
    b8 hardwareCaustics = false;
};

struct CausticsStagePrefixResult{
    Core::GpuTaskId causticIrradianceClearTask;
    Core::GpuTaskId causticAccumulatorBootstrapClearTask;
    Core::GpuTaskId causticAccumulatorDecayTask;
    Core::GpuTaskId photonDependencyTask;
    b8 graphOwnsAccumulatorBootstrapClear = false;
};

struct CausticsGeometryAndResolveInputs{
    DeferredFrameTargets& targets;
    b8& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& resolveTiming;
    CausticsStageTargets stageTargets;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId causticIrradiance;
    Core::GpuTaskId photonTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    Span<const Core::GpuTaskExternalStateSource> geometryStateSources{};
    Span<const Core::GpuTaskExternalStateSource> resolveStateSources{};
    b8 hardwareCaustics = false;
};

struct CausticsGeometryAndResolveResult : public CausticsResolveChainResult{
    Core::GpuTaskId causticGeometryTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CausticsResolveChainBuilder final : NoCopy{
public:
    CausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] Expected<CausticsStageTargets> importStageTargets(const DeferredFrameTargets& targets, b8 hardwareCaustics);
    [[nodiscard]] Expected<CausticsStagePrefixResult> declarePrefixTasks(const CausticsStagePrefixInputs& inputs);
    [[nodiscard]] Expected<CausticsGeometryAndResolveResult> declareGeometryAndResolve(const CausticsGeometryAndResolveInputs& inputs);
    [[nodiscard]] Expected<CausticsResolveChainResult> declare(const CausticsResolveChainInputs& inputs, const CausticsResolveChainNaming& naming);
    [[nodiscard]] Expected<CausticsResolveChainResult> declareHardware(const CausticsResolveChainInputs& inputs);
    [[nodiscard]] Expected<CausticsResolveChainResult> declareSoftware(const CausticsResolveChainInputs& inputs);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

