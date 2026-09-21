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

// Hardware caustics resolve chain owns prepare plus wavelet plus upsample plus timing-close declaration.
struct HardwareCausticsResolveChainInputs : public CausticsResolveChainStageUseInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    bool* producerDispatched = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* resolveTiming = nullptr;
};

struct HardwareCausticsResolveChainResult{
    Core::GpuTaskId hardwareCausticsTask;
    Core::GpuTaskId causticResolvePrepareTask;
    Core::GpuTaskId causticResolveWaveletTask;
    Core::GpuTaskId causticResolveSecondWaveletTask;
    Core::GpuTaskId causticResolveThirdWaveletTask;
    Core::GpuTaskId causticResolveFourthWaveletTask;
    Core::GpuTaskId causticResolveFifthWaveletTask;
    Core::GpuTaskId causticResolveUpsampleTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HardwareCausticsResolveChainBuilder final : NoCopy{
public:
    HardwareCausticsResolveChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererRayTracingSystem& raytracingSystem
    );


public:
    [[nodiscard]] bool declare(
        const HardwareCausticsResolveChainInputs& inputs,
        HardwareCausticsResolveChainResult& outResult,
        Core::Alloc::ScratchArena& scratchArena
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

