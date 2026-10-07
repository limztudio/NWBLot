// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct CsgFrameState;


// Shared AVBOIT pass upload owns gather plus readiness checks for occupancy, extinction, and accumulation.
struct AvboitPassUploadInputs{
    Core::Framebuffer* framebuffer = nullptr;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::AvboitOccupancy;
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
};

struct AvboitPassUploadResult{
    bool hasDrawItems = false;
    bool hasCsgDrawItems = false;
    bool ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitPassUploadHelper final : NoCopy{
public:
    AvboitPassUploadHelper(
        RendererMaterialSystem& materialSystem
    );


public:
    [[nodiscard]] bool gather(
        const AvboitPassUploadInputs& inputs,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        MaterialTypedByteDataVector& materialTypedBytes,
        AvboitPassUploadResult& outResult
    );


private:
    RendererMaterialSystem& m_materialSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

