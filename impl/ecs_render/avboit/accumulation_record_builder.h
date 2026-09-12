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
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT accumulation record owns compute-emulation plus shared-phase plus raster declaration.
struct AvboitAccumulationRecordInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId refractionDepth;
    Core::GpuGraphResourceId avboitDepthWarp;
    Core::GpuGraphResourceId avboitTransmittance;
    Core::GpuGraphResourceId avboitControl;
    Core::GpuGraphResourceId avboitForegroundColor;
    Core::GpuGraphResourceId avboitForegroundExtinction;
    Core::GpuGraphResourceId avboitAccumColor;
    Core::GpuGraphResourceId avboitAccumExtinction;
    Core::GpuGraphResourceId avboitMaterialDomain;
    Core::GpuGraphResourceId avboitCsgDomain;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuGraphResourceId csgRemovedIntervalDepth;
    Core::GpuGraphResourceId csgRemovedIntervalCapNormal;
    Core::GpuGraphResourceId csgRemovedIntervalData;
    Core::GpuGraphResourceId csgRemovedIntervalCount;
    Core::TextureSubresourceSet csgRemovedIntervalSubresources;
    Core::TextureSubresourceSet csgRemovedIntervalCountSubresources;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuTaskId integrationTask;
    Core::GpuTaskId uploadTask;
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    Core::BufferRange instanceRange;
    Core::BufferRange materialTypedRange;
    Core::BufferRange receiverRange;
    Core::BufferRange cutterRange;
    bool intervalOutputsGraphOwned = false;
    bool csgStreamsUploaded = false;
    bool streamsUploaded = false;
    bool regularComputeEmulationPlanCaptured = false;
    bool csgComputeEmulationPlanCaptured = false;
    bool sharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedComputeEmulationPlan;
    usize sharedComputeEmulationInstanceCount = 0u;
    usize sharedComputeEmulationMaterialTypedByteCount = 0u;
    Core::GpuTimingSubmissionTicket* accumulationTimingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* accumulationComputeEmulationTiming = nullptr;

    explicit AvboitAccumulationRecordInputs(Core::Alloc::GlobalArena& arena){
        static_cast<void>(arena);
    }
};

struct AvboitAccumulationRecordResult{
    Core::GpuTaskId accumulationTask;
    bool declared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitAccumulationRecordBuilder final : NoCopy{
public:
    AvboitAccumulationRecordBuilder(
        Core::GpuTaskGraph& graph,
        Core::GraphicsRuntime& graphics,
        RendererMaterialSystem& materialSystem,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declare(
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        AvboitAccumulationRecordInputs& inputs,
        RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
        RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload,
        AvboitAccumulationRecordResult& outResult
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

