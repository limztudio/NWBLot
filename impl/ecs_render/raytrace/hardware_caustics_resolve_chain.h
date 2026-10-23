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
    DeferredFrameTargets& targets;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    bool& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& resolveTiming;

public:
    HardwareCausticsResolveChainInputs(DeferredFrameTargets& targetsIn, bool& producerDispatchedIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& resolveTimingIn)noexcept
        : targets(targetsIn)
        , producerDispatched(producerDispatchedIn)
        , timingTicket(timingTicketIn)
        , resolveTiming(resolveTimingIn)
    {}
};

struct HardwareCausticsResolveChainResult : public CausticsResolveChainStageTasks{
    Core::GpuTaskId hardwareCausticsTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class HardwareCausticsResolveChainBuilder final : NoCopy{
public:
    HardwareCausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept;


public:
    [[nodiscard]] Expected<HardwareCausticsResolveChainResult> declare(const HardwareCausticsResolveChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

