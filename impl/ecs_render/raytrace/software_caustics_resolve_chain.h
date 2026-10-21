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
    DeferredFrameTargets& targets;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    const Core::GpuTaskExternalStateSource* stateSources = nullptr;
    usize stateSourceCount = 0u;
    bool& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& resolveTiming;

public:
    SoftwareCausticsResolveChainInputs(DeferredFrameTargets& targetsIn, bool& producerDispatchedIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& resolveTimingIn)noexcept
        : targets(targetsIn)
        , producerDispatched(producerDispatchedIn)
        , timingTicket(timingTicketIn)
        , resolveTiming(resolveTimingIn)
    {}
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
    SoftwareCausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept;


public:
    [[nodiscard]] Expected<SoftwareCausticsResolveChainResult> declare(const SoftwareCausticsResolveChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

