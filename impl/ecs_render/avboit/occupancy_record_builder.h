// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/record_inputs_base.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ObjectGeometryCacheGraph;
class RendererAvboitSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT occupancy record owns compute-emulation plus shared-phase plus raster declaration.
struct AvboitOccupancyRecordInputs : public AvboitRecordInputsBase{
    Core::GpuGraphResourceId avboitLowRaster;
    Core::GpuGraphResourceId avboitCoverage;
    Core::GpuTaskId clearTask;
    Core::GpuTimingSubmissionTicket& preTimingTicket;
    Optional<Core::GpuTimingMeasure>& occupancyComputeEmulationTiming;


public:
    AvboitOccupancyRecordInputs(DeferredFrameTargets& targetsIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& computeEmulationTimingIn)noexcept
        : AvboitRecordInputsBase(targetsIn)
        , preTimingTicket(timingTicketIn)
        , occupancyComputeEmulationTiming(computeEmulationTimingIn)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitOccupancyRecordBuilder final : public AvboitRecordBuilderBase{
public:
    using AvboitRecordBuilderBase::AvboitRecordBuilderBase;


public:
    [[nodiscard]] b8 declare(
        Core::Alloc::ScratchArena& scratchArena,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        ObjectGeometryCacheGraph& objectGeometry,
        AvboitOccupancyRecordInputs& inputs,
        RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
        RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

