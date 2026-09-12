// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <global/timer.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererFramePipeline;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deferred frame tail owns history-copy plus recovery plus compile declaration.
struct DeferredFrameTailInputs{
    const RayTracingSurfelPersistentResourceSnapshot* surfelResources = nullptr;
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
    const Timer* declarationBegin = nullptr;
    bool capturesLaggedLightingHistory = false;
};

struct DeferredFrameTailResult{
    bool compiled = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeferredFrameTailBuilder final : NoCopy{
public:
    explicit DeferredFrameTailBuilder(NotNull<RendererFramePipeline*> pipeline);
    [[nodiscard]] bool declare(
        const DeferredFrameTailInputs& inputs,
        DeferredFrameTailResult& outResult
    );


private:
    NotNull<RendererFramePipeline*> m_pipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
