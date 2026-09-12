// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem;
struct DeferredFrameTargets;


namespace AvboitMaterialUploadPhase{ enum Enum : u8{
    Occupancy,
    Extinction,
    Accumulation,
}; };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared AVBOIT material upload owns instance plus typed plus CSG buffer uploads.
struct AvboitMaterialUploadInputs{
    const DeferredFrameTargets* targets = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuTaskId uploadTask;
    AvboitMaterialUploadPhase::Enum phase = AvboitMaterialUploadPhase::Occupancy;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitMaterialUploadBuilder final : NoCopy{
public:
    AvboitMaterialUploadBuilder(
        Core::GpuTaskGraph& graph,
        RendererCsgSystem& csgSystem
    );


public:
    [[nodiscard]] bool declare(
        const AvboitMaterialUploadInputs& inputs,
        const InstanceGpuDataVector& instanceData,
        const MaterialTypedByteDataVector& materialTypedBytes,
        const CsgFrameGpuData& csgFrameData,
        bool hasCsgDrawItems,
        Core::GpuTaskId& inOutUploadTask,
        bool& outCsgStreamsUploaded
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererCsgSystem& m_csgSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
