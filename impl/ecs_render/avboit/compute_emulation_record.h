// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/avboit/avboit_frame_targets.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_desc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Pass traits select timing, pipeline and framebuffer for the common alias-free/CSG record sequence.
struct AvboitComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition& timingScope;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};

// All bindings required: compute emulation always generates against this frame's bindings.
struct AvboitComputeEmulationTaskPayload{
    Core::GraphicsRuntime& graphics;
    RendererMaterialSystem& materialSystem;
    ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
    ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& timing;
    ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan plan;
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan csgPlan;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    b8 materialDrawBuffersUploaded = false;
    b8 csgFrameBuffersUploaded = false;
    b8 materialFrameStatesGraphOwned = false;
    b8 materialGeometryStatesGraphOwned = false;
    b8 conservativeGeometryScissor = false;


public:
    explicit AvboitComputeEmulationTaskPayload(
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphicsIn,
        RendererMaterialSystem& materialSystemIn,
        DeferredFrameTargets& targetsIn,
        Core::GpuTimingSubmissionTicket& timingTicketIn,
        Optional<Core::GpuTimingMeasure>& timingIn
    )noexcept
        : graphics(graphicsIn)
        , materialSystem(materialSystemIn)
        , targets(targetsIn)
        , timingTicket(timingTicketIn)
        , timing(timingIn)
        , plan(arena)
        , csgPlan(arena)
    {}
};

// All bindings required: shared emulation always generates against this frame's bindings.
struct AvboitSharedComputeEmulationTaskPayload{
    Core::GraphicsRuntime& graphics;
    RendererMaterialSystem& materialSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& timing;
    ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan plan;
    usize drawIndex = 0u;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    b8 materialDrawBuffersUploaded = false;
    b8 materialFrameStatesGraphOwned = false;
    b8 materialGeometryStatesGraphOwned = false;
    b8 beginTiming = false;
    b8 finishTiming = false;
    SharedComputeEmulationPhase::Enum phase = SharedComputeEmulationPhase::Generate;
};

// The avboit system, the frame targets, and the timing ticket are required: raster passes always record against this frame's bindings.
struct AvboitRasterPassTaskPayload{
    RendererAvboitSystem& avboitSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
    ECSRenderDetail::TransparentMaterialPassGraphSnapshot snapshot;
    ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
    Optional<Core::GpuTimingMeasure>* computeEmulationTiming = nullptr;
    b8 phasePrepared = false;
    b8 streamsUploaded = false;
    b8 materialFrameStatesGraphOwned = false;
    b8 materialGeometryStatesGraphOwned = false;
    b8 computeEmulationOutputStatesGraphOwned = false;
    b8 csgComputeEmulationOutputStatesGraphOwned = false;
    b8 generatedGeometryReused = false;
    b8 hasTransparentRenderers = false;


public:
    explicit AvboitRasterPassTaskPayload(
        Core::Alloc::GlobalArena& arena,
        RendererAvboitSystem& avboitSystemIn,
        DeferredFrameTargets& targetsIn,
        Core::GpuTimingSubmissionTicket& timingTicketIn
    )noexcept
        : avboitSystem(avboitSystemIn)
        , targets(targetsIn)
        , timingTicket(timingTicketIn)
        , snapshot(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitComputeEmulationGraphTaskBase{
    using Payload = AvboitComputeEmulationTaskPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.timing);
    }
};

struct AvboitSharedComputeEmulationGraphTaskBase : RendererTaskGraphDetail::SharedComputeEmulationPhaseTaskCommandRequirementsBase<AvboitSharedComputeEmulationTaskPayload>{
    using Payload = AvboitSharedComputeEmulationTaskPayload;


public:
    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(&payload.timing);
    }
};

struct AvboitRasterPassGraphTaskBase{
    using Payload = AvboitRasterPassTaskPayload;


public:
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute };


public:
    static void Discarded(Payload& payload)noexcept{
        Core::DiscardGpuTimingMeasure(payload.computeEmulationTiming);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 RecordAvboitComputeEmulation(const AvboitComputeEmulationTaskPayload& inputs, Core::CommandList& commandList, const AvboitComputeEmulationRecordTrait& trait);
[[nodiscard]] b8 RecordAvboitSharedComputeEmulation(const AvboitSharedComputeEmulationTaskPayload& inputs, Core::CommandList& commandList, const AvboitComputeEmulationRecordTrait& trait);

template<typename DispatchFn>
[[nodiscard]] inline b8 RecordAvboitRasterPassFromPayload(
    const AvboitRasterPassTaskPayload& payload,
    Core::CommandList& commandList,
    DispatchFn&& dispatch
){
    if((payload.computeEmulationOutputStatesGraphOwned || payload.csgComputeEmulationOutputStatesGraphOwned) && !payload.generatedGeometryReused && !payload.computeEmulationTiming)
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    MaterialPassDrawItemPartitionsView drawItems{};
    CsgFrameGpuDataView csgFrameData{};
    usize preparedInstanceCount = 0u;
    usize preparedMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!payload.phasePrepared || !payload.snapshot.captured))
        return false;
    if(payload.phasePrepared && payload.snapshot.captured){
        drawItems = payload.snapshot.drawItemsView();
        csgFrameData = payload.snapshot.csgFrameDataView();
        preparedInstanceCount = payload.snapshot.instanceCount;
        preparedMaterialTypedByteCount = payload.snapshot.materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers)
        dispatch(commandList, drawItems, csgFrameData, preparedInstanceCount, preparedMaterialTypedByteCount);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

