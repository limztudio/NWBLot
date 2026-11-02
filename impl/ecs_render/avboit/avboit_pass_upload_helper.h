// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_render/avboit/compute_emulation_record.h>
#include <impl/ecs_render/avboit/frame_graph_avboit_upload_result.h>
#include <impl/ecs_render/avboit/geometry_preparation_builder.h>
#include <impl/ecs_render/avboit/material_upload_builder.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/avboit/avboit_frame_targets.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitGeneratedGeometryReuse;
class RendererCsgSystem;
class RendererMaterialSystem;

// Shared AVBOIT pass upload owns gather plus readiness checks for occupancy, extinction, and accumulation.
struct AvboitPassUploadInputs{
    Core::Framebuffer& framebuffer;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::AvboitOccupancy;
    const CsgFrameState& csgFrameState;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const MeshViewGpuData& meshViewState;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
};

struct FrameGraphAvboitPassPhaseDesc{
    Core::Framebuffer& framebuffer;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::AvboitOccupancy;
    AvboitGeometryPhase::Enum phase = AvboitGeometryPhase::Occupancy;
};

// Parameterized AVBOIT pass upload chain owns gather plus geometry, material-upload, snapshot, and emulation capture.
struct FrameGraphAvboitPassUploadInputs{
    DeferredFrameTargets& targets;
    const CsgFrameState& csgFrameState;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const MeshViewGpuData& meshViewState;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuTaskId uploadTask;
    b8 intervalOutputsGraphOwned = false;
    b8 hasTransparentRenderers = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitPassUploadHelper final : NoCopy{
public:
    AvboitPassUploadHelper(RendererMaterialSystem& materialSystem)noexcept
        : m_materialSystem(materialSystem)
    {}


public:
    [[nodiscard]] Expected<AvboitPassUploadResult> gather(
        const AvboitPassUploadInputs& inputs,
        MaterialPassDrawItemPartitions& drawItems,
        InstanceGpuDataVector& instanceData,
        CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
        MaterialTypedByteDataVector& materialTypedBytes
    );


private:
    RendererMaterialSystem& m_materialSystem;
};

class FrameGraphAvboitPassUploadChain final : NoCopy{
public:
    FrameGraphAvboitPassUploadChain(Core::GpuTaskGraph& graph, RendererMaterialSystem& materialSystem, RendererCsgSystem& csgSystem)noexcept
        : m_graph(graph)
        , m_materialSystem(materialSystem)
        , m_csgSystem(csgSystem)
    {}


public:
    [[nodiscard]] Expected<FrameGraphAvboitMaterialUploadResult> declare(
        Core::Alloc::ScratchArena& scratchArena,
        const FrameGraphAvboitPassPhaseDesc& phaseDesc,
        const FrameGraphAvboitPassUploadInputs& inputs,
        AvboitRasterPassTaskPayload& rasterPayload,
        AvboitComputeEmulationTaskPayload& computeEmulationPayload,
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

