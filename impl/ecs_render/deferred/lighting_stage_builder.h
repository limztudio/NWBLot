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


class RendererDeferredSystem;


// Deferred lighting stage owns lagged-history selector upload plus lighting task declaration.
struct DeferredLightingStageInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId shadowVisibility;
    Core::GpuGraphResourceId causticIrradiance;
    Core::GpuGraphResourceId surfelIrradiance;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId bindlessSlots;
    Core::GpuGraphResourceId opaqueColor;
    Core::GpuTaskId graphicsPrefixTask;
    Core::GpuTaskId shadowVisibilityTask;
    Core::GpuTaskId surfelGiTask;
    Core::GpuTaskId avboitFinalTask;
    Core::GpuTaskId hardwareCausticsTask;
    Core::GpuTaskId softwareCausticsTask;
    Core::GpuExternalCompletionId historyReadReadyCompletion;
    const DeferredLaggedLightingHistoryResources* history = nullptr;
    bool useLaggedLightingHistory = false;
    bool declaresHardwareCaustics = false;
    bool hasTransparentRenderers = false;
};

struct DeferredLightingStageResult{
    Core::GpuTaskId lightingTask;
    Core::GpuTaskId historySlotsUploadTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredLightingStageBuilder final : NoCopy{
public:
    DeferredLightingStageBuilder(
        Core::GpuTaskGraph& graph,
        RendererDeferredSystem& deferredSystem
    );

public:
    [[nodiscard]] bool declare(
        const DeferredLightingStageInputs& inputs,
        Core::GpuTaskId& outUploadTask,
        Core::GpuTimingSubmissionTicket& timingTicket,
        DeferredLightingStageResult& outResult
    );

private:
    Core::GpuTaskGraph& m_graph;
    RendererDeferredSystem& m_deferredSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
