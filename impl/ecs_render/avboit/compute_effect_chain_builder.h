// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererTaskTimingFeedback;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT compute-effect chain owns depth-warp plus integration task declaration.
struct AvboitDepthWarpStageInputs{
    AvboitFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId coverage;
    Core::GpuGraphResourceId depthWarp;
    Core::GpuGraphResourceId control;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuTaskId occupancyTask;
    Core::GpuTimingSubmissionTicket* depthWarpTimingTicket = nullptr;
    RendererTaskTimingFeedback* timingFeedback = nullptr;
    bool hasTransparentRenderers = false;
};

struct AvboitDepthWarpStageResult{
    Core::GpuTaskId completionTask;
};

struct AvboitIntegrationStageInputs{
    AvboitFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId extinction;
    Core::GpuGraphResourceId control;
    Core::GpuGraphResourceId extinctionOverflow;
    Core::GpuGraphResourceId transmittance;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuTaskId extinctionTask;
    Core::GpuTimingSubmissionTicket* integrationTimingTicket = nullptr;
    RendererTaskTimingFeedback* timingFeedback = nullptr;
};

struct AvboitIntegrationStageResult{
    Core::GpuTaskId integrationTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitComputeEffectChainBuilder final : NoCopy{
public:
    AvboitComputeEffectChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declareDepthWarp(
        const AvboitDepthWarpStageInputs& inputs,
        AvboitDepthWarpStageResult& outResult
    );

    [[nodiscard]] bool declareIntegration(
        const AvboitIntegrationStageInputs& inputs,
        AvboitIntegrationStageResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererAvboitSystem& m_avboitSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

