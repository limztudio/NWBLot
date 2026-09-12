// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/reflection/composite_inputs.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/rhi/framebuffer.h>
#include <core/graphics/rhi/presentation.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/presentation_contributor.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Composite/present suffix owns terminal color resolve and presentation handoff.
struct DeferredGraphSuffixInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId opaqueColor;
    Core::GpuGraphResourceId avboitAccumColor;
    Core::GpuGraphResourceId avboitAccumExtinction;
    Core::GpuGraphResourceId avboitForegroundColor;
    Core::GpuGraphResourceId avboitForegroundExtinction;
    Core::GpuGraphResourceId refractionResolve;
    Core::GpuGraphResourceId currentBindlessSlots;
    RendererTaskGraphDetail::ReflectionGraphResult reflectionGraph;
    ReflectionCompositeInputs reflectionCompositeInputs;
    Core::GpuTaskId lightingTask;
    Core::GpuTaskId avboitFinalTask;
    Core::GpuTaskId refractionResolveTask;
    Core::GpuTaskId surfelGiTask;
    const Core::AcquiredPresentationFrame* presentationFrame = nullptr;
    const Core::FramebufferDesc* presentationFramebufferDesc = nullptr;
    bool useLaggedLightingHistory = false;
};

struct DeferredGraphSuffixResult{
    Core::GpuTaskId compositeTask;
    Core::GpuTaskId presentTask;
    Core::GpuTaskId overlayTask;
    Core::GpuTaskId frameTimingEndTask;
    Core::GpuGraphResourceId compositeColor;
    Core::GpuGraphResourceId compositeBindlessSlots;
    Core::GpuGraphResourceId backbuffer;
    bool overlayRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredGraphSuffixBuilder final : NoCopy{
public:
    DeferredGraphSuffixBuilder(
        Core::GpuTaskGraph& graph,
        RendererDeferredSystem& deferredSystem,
        Core::GraphicsRuntime& graphics,
        Core::IGpuTaskGraphPresentationContributor* presentationContributor
    );


public:
    [[nodiscard]] bool declare(
        const DeferredGraphSuffixInputs& inputs,
        DeferredFrameTargets& targets,
        const ReflectionCompositeInputs& compositeInputs,
        Core::GpuTimingSubmissionTicket& compositeTimingTicket,
        Core::GpuTimingSubmissionTicket& presentTimingTicket,
        Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
        const Core::GpuTaskId& shadowVisibilityTask,
        Core::GpuTimingFrameTransaction& frameTimingTransaction,
        DeferredGraphSuffixResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererDeferredSystem& m_deferredSystem;
    Core::GraphicsRuntime& m_graphics;
    Core::IGpuTaskGraphPresentationContributor* m_presentationContributor;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

