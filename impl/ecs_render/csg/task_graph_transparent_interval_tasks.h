// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererCsgSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TransparentCsgIntervalRecordInputs{
    RendererMaterialSystem& materialSystem;
    RendererCsgSystem& csgSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTiming;
    MeshFrameBindingSnapshot frameBindings;
    CsgGraphResourceSnapshot csgResources;
    TransparentCsgIntervalGraphSnapshot transparentCsgSnapshot;
    bool csgFrameBuffersUploaded = false;


public:
    explicit TransparentCsgIntervalRecordInputs(
        Core::Alloc::GlobalArena& arena,
        RendererMaterialSystem& materialSystemIn,
        RendererCsgSystem& csgSystemIn,
        DeferredFrameTargets& targetsIn,
        Core::GpuTimingSubmissionTicket& timingTicketIn,
        Optional<Core::GpuTimingMeasure>& transparentCsgIntervalsTimingIn
    )noexcept;
};


struct AvboitCsgReceiverSpanGraphTask{
    // The material/CSG systems, frame targets, timing ticket, and intervals timing slot are required: the transparent interval passes always bracket dispatches with the frame timing.
    using Payload = TransparentCsgIntervalRecordInputs;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    }
};


// Interval combine maps visible span/peel inputs to removed-interval outputs.
struct AvboitCsgIntervalCombineGraphTask{
    // The material/CSG systems, frame targets, timing ticket, and intervals timing slot are required: the transparent interval passes always bracket dispatches with the frame timing.
    using Payload = TransparentCsgIntervalRecordInputs;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);

    static void Discarded(Payload& payload){
        Core::DiscardGpuTimingMeasure(&payload.transparentCsgIntervalsTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

