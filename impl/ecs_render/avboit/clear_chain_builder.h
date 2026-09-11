// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererAvboitSystem;


// AVBOIT clear chain owns serial target clears feeding the occupancy dependency.
struct AvboitClearChainInputs{
    Core::GpuGraphResourceId lowRaster;
    Core::GpuGraphResourceId accumColor;
    Core::GpuGraphResourceId accumExtinction;
    Core::GpuGraphResourceId foregroundColor;
    Core::GpuGraphResourceId foregroundExtinction;
    Core::GpuGraphResourceId transmittance;
    Core::GpuGraphResourceId coverage;
    Core::GpuGraphResourceId depthWarp;
    Core::GpuGraphResourceId control;
    Core::GpuGraphResourceId extinction;
    Core::GpuGraphResourceId extinctionOverflow;
    Core::GpuTaskId uploadTask;
    bool clearTargets = false;
};

struct AvboitClearChainResult{
    Core::GpuTaskId clearTask;
    Core::GpuTaskId clearFirstTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AvboitClearChainBuilder final : NoCopy{
public:
    AvboitClearChainBuilder(
        Core::GpuTaskGraph& graph,
        RendererAvboitSystem& avboitSystem
    );

public:
    [[nodiscard]] bool declare(
        const AvboitClearChainInputs& inputs,
        GraphClearTimingRecordState& clearTimingState,
        AvboitClearChainResult& outResult
    );

private:
    Core::GpuTaskGraph& m_graph;
    RendererAvboitSystem& m_avboitSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

