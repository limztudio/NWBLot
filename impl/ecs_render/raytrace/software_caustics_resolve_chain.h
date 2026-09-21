// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/caustics_resolve_chain.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;

struct DeferredFrameTargets;

// Software caustics resolve chain owns prepare plus wavelet plus upsample plus timing-close declaration.
struct SoftwareCausticsResolveChainInputs : public CausticsResolveChainStageUseInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    const Core::GpuTaskExternalStateSource* stateSources = nullptr;
    usize stateSourceCount = 0u;
    bool* producerDispatched = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* resolveTiming = nullptr;
};

struct SoftwareCausticsResolveChainResult{
    Core::GpuTaskId softwareCausticsTask;
    Core::GpuTaskId causticResolvePrepareTask;
    Core::GpuTaskId causticResolveWaveletTask;
    Core::GpuTaskId causticResolveSecondWaveletTask;
    Core::GpuTaskId causticResolveThirdWaveletTask;
    Core::GpuTaskId causticResolveFourthWaveletTask;
    Core::GpuTaskId causticResolveFifthWaveletTask;
    Core::GpuTaskId causticResolveUpsampleTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SoftwareCausticsResolveChainBuilder final : NoCopy{
public:
    SoftwareCausticsResolveChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererRayTracingSystem& raytracingSystem
    );


public:
    [[nodiscard]] bool declare(
        const SoftwareCausticsResolveChainInputs& inputs,
        SoftwareCausticsResolveChainResult& outResult,
        Core::Alloc::ScratchArena& scratchArena
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

