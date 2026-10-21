// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include "frame_graph_avboit_upload_result.h"

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
#include <impl/ecs_render/avboit/task_graph_accumulation_tasks.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererCsgSystem;
class RendererAvboitSystem;
class AvboitGeneratedGeometryReuse;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Accumulation upload chain owns AVBOIT accumulation gather plus geometry, material-upload, snapshot, and emulation capture.
struct FrameGraphAvboitAccumulationUploadInputs{
    DeferredFrameTargets& targets;
    const CsgFrameState& csgFrameState;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const ECSRenderDetail::MeshViewGpuData& meshViewState;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuTaskId uploadTask;
    bool intervalOutputsGraphOwned = false;
};

using FrameGraphAvboitAccumulationUploadResult = FrameGraphAvboitMaterialUploadResult;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphAvboitAccumulationUploadChain final : NoCopy{
public:
    FrameGraphAvboitAccumulationUploadChain(Core::GpuTaskGraph& graph, RendererMaterialSystem& materialSystem, RendererCsgSystem& csgSystem)noexcept;


public:
    [[nodiscard]] Expected<FrameGraphAvboitAccumulationUploadResult> declare(
        Core::Alloc::ScratchArena& scratchArena,
        const FrameGraphAvboitAccumulationUploadInputs& inputs,
        RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
        RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload,
        AvboitGeneratedGeometryReuse& generatedGeometry
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

