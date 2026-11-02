// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/record_inputs_base.h>
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>
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


// AVBOIT accumulation record owns compute-emulation plus shared-phase plus raster declaration.
struct AvboitAccumulationRecordInputs : public AvboitRecordInputsBase{
    Core::GpuGraphResourceId refractionDepth;
    Core::GpuGraphResourceId avboitDepthWarp;
    Core::GpuGraphResourceId avboitTransmittance;
    Core::GpuGraphResourceId avboitControl;
    Core::GpuGraphResourceId avboitForegroundColor;
    Core::GpuGraphResourceId avboitForegroundExtinction;
    Core::GpuGraphResourceId avboitAccumColor;
    Core::GpuGraphResourceId avboitAccumExtinction;
    Core::GpuTaskId integrationTask;
    Core::GpuTimingSubmissionTicket& accumulationTimingTicket;
    Optional<Core::GpuTimingMeasure>& accumulationComputeEmulationTiming;


public:
    AvboitAccumulationRecordInputs(DeferredFrameTargets& targetsIn, Core::GpuTimingSubmissionTicket& timingTicketIn, Optional<Core::GpuTimingMeasure>& computeEmulationTimingIn)noexcept
        : AvboitRecordInputsBase(targetsIn)
        , accumulationTimingTicket(timingTicketIn)
        , accumulationComputeEmulationTiming(computeEmulationTimingIn)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitAccumulationRecordBuilder final : public AvboitRecordBuilderBase{
public:
    using AvboitRecordBuilderBase::AvboitRecordBuilderBase;


public:
    [[nodiscard]] b8 declare(
        Core::Alloc::ScratchArena& scratchArena,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        ObjectGeometryCacheGraph& objectGeometry,
        AvboitAccumulationRecordInputs& inputs,
        RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
        RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

