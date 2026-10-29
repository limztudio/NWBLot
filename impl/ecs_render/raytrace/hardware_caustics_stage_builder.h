// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/task_graph_stage.h>

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
    DeferredFrameTargets* targets = nullptr;
    const DeferredLightingGraphResources* lightingResources = nullptr;
    const ECSRenderDetail::MeshViewBufferSnapshot* meshViewSnapshot = nullptr;
    const RayTracingDeferredGraphResourceSnapshot* rayTracingResources = nullptr;
    const ECSRenderDetail::RendererFrameGraphFeatures* features = nullptr;
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
    const b8* shadowPreparationReady = nullptr;
    const Core::GpuExternalCompletionId* historyWriterDrainCompletion = nullptr;
    Core::GpuPersistentResourceStateCache* accumulatorPersistentState = nullptr;
    b8 declaresHardwareCaustics = false;
    b8* producerDispatched = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* photonTiming = nullptr;
    Optional<Core::GpuTimingMeasure>* resolveTiming = nullptr;
};

struct HardwareCausticsStageResult{
    Core::GpuTaskId hardwareCausticsTask;
    Core::GpuTaskId causticIrradianceClearTask;
    Core::GpuTaskId causticAccumulatorBootstrapClearTask;
    Core::GpuTaskId causticAccumulatorDecayTask;
    Core::GpuTaskId causticPhotonTask;
    Core::GpuTaskId causticGeometryTask;
    Core::GpuTaskId causticResolvePrepareTask;
    Core::GpuTaskId causticResolveWaveletTask;
    Core::GpuTaskId causticResolveSecondWaveletTask;
    Core::GpuTaskId causticResolveThirdWaveletTask;
    Core::GpuTaskId causticResolveFourthWaveletTask;
    Core::GpuTaskId causticResolveFifthWaveletTask;
    Core::GpuTaskId causticResolveUpsampleTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HardwareCausticsStageBuilder final : NoCopy{
public:
    HardwareCausticsStageBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept;


public:
    [[nodiscard]] Expected<HardwareCausticsStageResult> declare(const HardwareCausticsStageInputs& inputs, Core::Alloc::ScratchArena& scratchArena);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

