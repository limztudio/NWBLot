// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;
class RendererCsgSystem;
class RendererMaterialSystem;
struct DeferredFrameTargets;
struct CsgFrameState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transparent CSG interval producer owns upload chain and interval-id/event clears.
struct TransparentCsgIntervalProducerInputs{
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
    Core::GpuGraphResourceId csgIntervalId;
    Core::GpuGraphResourceId csgReceiverEventCount;
    Core::TextureSubresourceSet csgPeelSubresources{};
    Core::TextureSubresourceSet csgReceiverEventCountSubresources{};
    Core::GpuTaskId prefixTask;
    bool hasTransparentRenderers = false;
};

struct TransparentCsgIntervalProducerResult{
    Core::GpuTaskId uploadTask;
    Core::GpuGraphResourceSetId materialGeometrySet;
    Core::GpuGraphResourceSetId materialSampledTextureSet;
    bool produced = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TransparentCsgIntervalBuilder final : NoCopy{
public:
    TransparentCsgIntervalBuilder(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declare(
        const TransparentCsgIntervalProducerInputs& inputs,
        RendererTaskGraphDetail::AvboitPreGraphTask::Payload& avboitPrePayload,
        ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
        ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
        GraphClearTimingRecordState& intervalClearTimingState,
        TransparentCsgIntervalProducerResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
    RendererMaterialSystem& m_materialSystem;
    RendererCsgSystem& m_csgSystem;
    RendererAvboitSystem& m_avboitSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

