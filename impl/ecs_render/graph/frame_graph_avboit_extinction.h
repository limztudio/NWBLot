// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/avboit/task_graph_extinction_integration_tasks.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererCsgSystem;
class RendererAvboitSystem;
class AvboitGeneratedGeometryReuse;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extinction upload chain owns AVBOIT extinction gather plus geometry, material-upload, snapshot, and emulation capture.
struct FrameGraphAvboitExtinctionUploadInputs{
    DeferredFrameTargets* targets = nullptr;
    const CsgFrameState* csgFrameState = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const ECSRenderDetail::MeshViewGpuData* meshViewState = nullptr;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuTaskId uploadTask;
    bool intervalOutputsGraphOwned = false;
    bool hasTransparentRenderers = false;
};

struct FrameGraphAvboitExtinctionUploadResult{
    Core::GpuTaskId uploadTask;
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    bool streamsUploaded = false;
    bool csgStreamsUploaded = false;
    bool regularComputeEmulationPlanCaptured = false;
    bool producesReusableGeometry = false;
    Core::GpuTaskId reusedGeometryProducer;
    bool csgComputeEmulationPlanCaptured = false;
    bool sharedComputeEmulationPlanCaptured = false;
    ECSRenderDetail::RegularSharedComputeEmulationGraphPlan sharedComputeEmulationPlan;
    usize sharedComputeEmulationInstanceCount = 0u;
    usize sharedComputeEmulationMaterialTypedByteCount = 0u;
    bool declared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphAvboitExtinctionUploadChain final : NoCopy{
public:
    FrameGraphAvboitExtinctionUploadChain(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem
    );


public:
    [[nodiscard]] bool declare(
        const FrameGraphAvboitExtinctionUploadInputs& inputs,
        RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
        RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload,
        AvboitGeneratedGeometryReuse& generatedGeometry,
        FrameGraphAvboitExtinctionUploadResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
