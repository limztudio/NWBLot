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
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT occupancy record owns compute-emulation plus shared-phase plus raster declaration.
struct AvboitOccupancyRecordInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId avboitLowRaster;
    Core::GpuGraphResourceId avboitCoverage;
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
    Core::GpuTaskId clearTask;
    Core::GpuTaskId uploadTask;
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    Core::BufferRange instanceRange;
    Core::BufferRange materialTypedRange;
    Core::BufferRange receiverRange;
    Core::BufferRange cutterRange;
    bool intervalOutputsGraphOwned = false;
    bool csgStreamsUploaded = false;
    bool regularComputeEmulationPlanCaptured = false;
    bool csgComputeEmulationPlanCaptured = false;
    bool sharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedComputeEmulationPlan;
    usize sharedComputeEmulationInstanceCount = 0u;
    usize sharedComputeEmulationMaterialTypedByteCount = 0u;
    Core::GpuTimingSubmissionTicket* preTimingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* occupancyComputeEmulationTiming = nullptr;

    explicit AvboitOccupancyRecordInputs(Core::Alloc::GlobalArena& arena){
        static_cast<void>(arena);
    }
};

struct AvboitOccupancyRecordResult{
    Core::GpuTaskId occupancyTask;
    bool declared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitOccupancyRecordBuilder final : NoCopy{
public:
    AvboitOccupancyRecordBuilder(
        Core::GpuTaskGraph& graph,
        Core::GraphicsRuntime& graphics,
        RendererMaterialSystem& materialSystem,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declare(
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
        AvboitOccupancyRecordInputs& inputs,
        RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
        RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload,
        AvboitOccupancyRecordResult& outResult
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

