// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <core/graphics/gpu_timing.h>
#include <impl/ecs_render/csg/task_graph_transparent_interval_tasks.h>
#include <impl/ecs_render/avboit/task_graph_occupancy_tasks.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
class RendererCsgSystem;
class RendererAvboitSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transparent-CSG interval tasks own the AVBOIT interval, receiver-span, and interval-combine graph task declarations.
struct FrameGraphTransparentCsgTaskInputs{
    DeferredFrameTargets* targets = nullptr;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuGraphResourceId csgCapBackNormal;
    Core::GpuGraphResourceId csgIntervalDepth;
    Core::GpuGraphResourceId csgIntervalId;
    Core::GpuGraphResourceId csgReceiverEventData;
    Core::GpuGraphResourceId csgReceiverEventCount;
    Core::GpuGraphResourceId csgReceiverSpanData;
    Core::GpuGraphResourceId csgReceiverSpanCount;
    Core::GpuGraphResourceId csgRemovedIntervalDepth;
    Core::GpuGraphResourceId csgRemovedIntervalCapNormal;
    Core::GpuGraphResourceId csgRemovedIntervalData;
    Core::GpuGraphResourceId csgRemovedIntervalCount;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId avboitMaterialDomain;
    Core::GpuGraphResourceId avboitCsgDomain;
    Core::TextureSubresourceSet csgPeelSubresources;
    Core::TextureSubresourceSet csgReceiverEventDataSubresources;
    Core::TextureSubresourceSet csgReceiverEventCountSubresources;
    Core::TextureSubresourceSet csgReceiverSpanDataSubresources;
    Core::TextureSubresourceSet csgReceiverSpanCountSubresources;
    Core::TextureSubresourceSet csgRemovedIntervalSubresources;
    Core::TextureSubresourceSet csgRemovedIntervalCountSubresources;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* transparentCsgIntervalsTiming = nullptr;
    Core::GpuTaskId transparentCsgUploadTask;
    Core::GpuGraphResourceSetId transparentCsgMaterialGeometrySet;
    Core::GpuGraphResourceSetId transparentCsgMaterialSampledTextureSet;
};

struct FrameGraphTransparentCsgTaskResult{
    Core::GpuTaskId intervalCompletionTask;
    bool intervalOutputsGraphOwned = false;
    bool declared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphTransparentCsgTasks final : NoCopy{
public:
    FrameGraphTransparentCsgTasks(
        Core::GpuTaskGraph& graph,
        RendererMaterialSystem& materialSystem,
        RendererCsgSystem& csgSystem,
        RendererAvboitSystem& avboitSystem
    );


public:
    [[nodiscard]] bool declare(
        const FrameGraphTransparentCsgTaskInputs& inputs,
        RendererTaskGraphDetail::AvboitPreGraphTask::Payload& prePayload,
        ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
        ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
        FrameGraphTransparentCsgTaskResult& outResult
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

