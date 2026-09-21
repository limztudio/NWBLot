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

// Shared stage-use core for the hardware/software resolve-chain input structs below. Both chains carry the same
// prepare plus five-wavelet plus upsample use ranges and differ only in naming, queues, and state sources.
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
    DeferredFrameTargets* targets = nullptr;
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
    bool* producerDispatched = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* resolveTiming = nullptr;
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
    CausticsResolveChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererRayTracingSystem& raytracingSystem
    );


public:
    [[nodiscard]] bool declare(
        const CausticsResolveChainInputs& inputs,
        const CausticsResolveChainNaming& naming,
        const Core::GpuQueueRequest& stageQueue,
        const Core::GpuQueueRequest& timingCloseQueue,
        CausticsResolveChainResult& outResult,
        Core::Alloc::ScratchArena& scratchArena
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererRayTracingSystem& m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

