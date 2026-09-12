// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;

struct DeferredFrameTargets;

// Hardware caustics resolve chain owns prepare plus wavelet plus upsample plus timing-close declaration.
struct HardwareCausticsResolveChainInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    const Core::GpuTaskResourceUse* prepareUses = nullptr;
    usize prepareUseCount = 0u;
    const Core::GpuTaskResourceUse* waveletUses = nullptr;
    usize waveletUseCount = 0u;
    const Core::GpuTaskResourceUse* secondWaveletUses = nullptr;
    usize secondWaveletUseCount = 0u;
    const Core::GpuTaskResourceUse* thirdWaveletUses = nullptr;
    usize thirdWaveletUseCount = 0u;
    const Core::GpuTaskResourceUse* fourthWaveletUses = nullptr;
    usize fourthWaveletUseCount = 0u;
    const Core::GpuTaskResourceUse* fifthWaveletUses = nullptr;
    usize fifthWaveletUseCount = 0u;
    const Core::GpuTaskResourceUse* upsampleUses = nullptr;
    usize upsampleUseCount = 0u;
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
        HardwareCausticsResolveChainResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

