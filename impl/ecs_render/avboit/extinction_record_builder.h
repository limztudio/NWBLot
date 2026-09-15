// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/record_inputs_base.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT extinction record owns compute-emulation plus shared-phase plus raster declaration.
struct AvboitExtinctionRecordInputs : public AvboitRecordInputsBase{
    Core::GpuGraphResourceId avboitLowRaster;
    Core::GpuGraphResourceId avboitDepthWarp;
    Core::GpuGraphResourceId avboitExtinction;
    Core::GpuGraphResourceId avboitControl;
    Core::GpuGraphResourceId avboitExtinctionOverflow;
    Core::GpuTaskId depthWarpCompletionTask;
    bool streamsUploaded = false;
    Core::GpuTimingSubmissionTicket* extinctionTimingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* extinctionComputeEmulationTiming = nullptr;

    explicit AvboitExtinctionRecordInputs(Core::Alloc::GlobalArena& arena)
        : AvboitRecordInputsBase(arena){
    }
};


struct AvboitExtinctionRecordResult{
    Core::GpuTaskId extinctionTask;
    bool declared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitExtinctionRecordBuilder final : NoCopy{
public:
    AvboitExtinctionRecordBuilder(
        Core::GpuTaskGraph& graph,
        Core::GraphicsRuntime& graphics,
        RendererMaterialSystem& materialSystem,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declare(
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        AvboitExtinctionRecordInputs& inputs,
        RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
        RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload,
        AvboitExtinctionRecordResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererMaterialSystem& m_materialSystem;
    RendererAvboitSystem& m_avboitSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

