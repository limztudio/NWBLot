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


// Shared prepare plus five-wavelet plus upsample plus timing-close resolve declaration.
struct CausticsResolveStageUses{
    const Core::GpuTaskResourceUse* uses = nullptr;
    usize useCount = 0u;
};

struct CausticsResolveChainStageUseInputs{
    CausticsResolveStageUses prepare;
    CausticsResolveStageUses wavelet;
    CausticsResolveStageUses secondWavelet;
    CausticsResolveStageUses thirdWavelet;
    CausticsResolveStageUses fourthWavelet;
    CausticsResolveStageUses fifthWavelet;
    CausticsResolveStageUses upsample;
};

struct CausticsResolveStageNaming{
    Name identity;
    AStringView label;
    TStringView warnText;
};

struct CausticsResolveChainInputs{
    DeferredFrameTargets& targets;
    Core::GpuTaskId geometryTask;
    Core::GpuTaskSchedulingHint baseScheduling;
    CausticsResolveStageUses prepare;
    CausticsResolveStageUses wavelet;
    CausticsResolveStageUses secondWavelet;
    CausticsResolveStageUses thirdWavelet;
    CausticsResolveStageUses fourthWavelet;
    CausticsResolveStageUses fifthWavelet;
    CausticsResolveStageUses upsample;
    const Core::GpuTaskExternalStateSource* stateSources = nullptr;
    usize stateSourceCount = 0u;
    bool applyStateSourcesToPrepareOnly = true;
    bool& producerDispatched;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& resolveTiming;

public:
    CausticsResolveChainInputs(DeferredFrameTargets& targetsIn, bool& producerDispatchedIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& resolveTimingIn)noexcept
        : targets(targetsIn)
        , producerDispatched(producerDispatchedIn)
        , timingTicket(timingTicketIn)
        , resolveTiming(resolveTimingIn)
    {}
};

struct CausticsResolveChainResult{
    Core::GpuTaskId causticResolvePrepareTask;
    Core::GpuTaskId causticResolveWaveletTask;
    Core::GpuTaskId causticResolveSecondWaveletTask;
    Core::GpuTaskId causticResolveThirdWaveletTask;
    Core::GpuTaskId causticResolveFourthWaveletTask;
    Core::GpuTaskId causticResolveFifthWaveletTask;
    Core::GpuTaskId causticResolveUpsampleTask;
    Core::GpuTaskId causticsTask;
};

struct CausticsResolveChainNaming{
    CausticsResolveStageNaming prepare;
    CausticsResolveStageNaming wavelet;
    CausticsResolveStageNaming secondWavelet;
    CausticsResolveStageNaming thirdWavelet;
    CausticsResolveStageNaming fourthWavelet;
    CausticsResolveStageNaming fifthWavelet;
    CausticsResolveStageNaming upsample;
    CausticsResolveStageNaming timingClose;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CausticsResolveChainBuilder final : NoCopy{
public:
    CausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept;


public:
    [[nodiscard]] Expected<CausticsResolveChainResult> declare(const CausticsResolveChainInputs& inputs, const CausticsResolveChainNaming& naming, Core::Alloc::ScratchArena& scratchArena);


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

