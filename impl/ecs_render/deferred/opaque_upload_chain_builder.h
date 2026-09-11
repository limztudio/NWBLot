// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem;
class RendererMaterialSystem;


// Opaque upload chain owns material plus CSG frame buffer uploads feeding G-buffer.
struct OpaqueUploadChainInputs{
    DeferredFrameTargets* targets = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const MaterialPassDrawItemPartitions* drawItems = nullptr;
    InstanceGpuDataVector* instanceData = nullptr;
    const CsgFrameGpuData* csgFrameData = nullptr;
#if defined(NWB_DEBUG)
    const ECSRenderDetail::MaterialTypedInstanceRangeVector* materialTypedRanges = nullptr;
#endif
    const MaterialTypedByteDataVector* materialTypedBytes = nullptr;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuTaskId dependencyTask;
};

struct OpaqueUploadChainResult{
    Core::GpuTaskId materialUploadTask;
    Core::GpuTaskId csgUploadTask;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;
    bool hasOpaqueDrawItems = false;
    bool hasCsgFrameGpuWork = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpaqueUploadChainBuilder final : NoCopy{
public:
    OpaqueUploadChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem
    );

public:
    [[nodiscard]] bool declare(
        const OpaqueUploadChainInputs& inputs,
        OpaqueUploadChainResult& outResult
    );

private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
