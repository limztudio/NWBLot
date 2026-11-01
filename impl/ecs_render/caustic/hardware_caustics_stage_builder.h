// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/caustic/caustic_resolve_state.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/persistent_state.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;

struct DeferredFrameTargets;

// Hardware caustics stage owns photon plus geometry plus resolve-wavelet declaration.
struct HardwareCausticsStageInputs{
    DeferredFrameTargets& targets;
    const DeferredLightingGraphResources& lightingResources;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewSnapshot;
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources;
    const RendererFrameGraphFeatures& features;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId currentCausticIrradiance;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId materialContextSlots;
    Core::GpuGraphResourceId opticalScene;
    Core::GpuGraphResourceSetId hardwareTraceGeometrySet;
    Span<const Core::GpuGraphResourceId> hardwareTraceGeometryResources{};
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet;
    Core::GpuTaskId graphicsPrefixTask;
    const b8& shadowPreparationReady;
    const Core::GpuExternalCompletionId& historyWriterDrainCompletion;
    Core::GpuPersistentResourceStateCache& accumulatorPersistentState;
    b8 declaresHardwareCaustics = false;
    b8& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& photonTiming;
    Optional<Core::GpuTimingMeasure>& resolveTiming;
};

struct HardwareCausticsStageResult{
    Core::GpuTaskId hardwareCausticsTask;
    Core::GpuTaskId causticIrradianceClearTask;
    Core::GpuTaskId causticAccumulatorBootstrapClearTask;
    Core::GpuTaskId causticAccumulatorDecayTask;
    Core::GpuTaskId causticPhotonTask;
    Core::GpuTaskId causticGeometryTask;
    Core::GpuTaskId causticResolvePrepareTask;
    Array<Core::GpuTaskId, s_CausticResolveWaveletPassCount> causticResolveWaveletTasks{};
    Core::GpuTaskId causticResolveUpsampleTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HardwareCausticsStageBuilder final : NoCopy{
public:
    HardwareCausticsStageBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
        : m_graph(graph)
        , m_raytracingSystem(raytracingSystem)
    {}


public:
    [[nodiscard]] Expected<HardwareCausticsStageResult> declare(const HardwareCausticsStageInputs& inputs, Core::Alloc::ScratchArena& scratchArena);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

