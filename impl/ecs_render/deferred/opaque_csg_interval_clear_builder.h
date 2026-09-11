// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;
struct CsgFrameGpuData;


// Opaque CSG interval clears own the interval-id plus receiver-event-count clears.
struct OpaqueCsgIntervalClearInputs{
    DeferredFrameTargets* targets = nullptr;
    const CsgFrameGpuData* csgFrameData = nullptr;
    Core::GpuGraphResourceId csgIntervalId;
    Core::GpuGraphResourceId csgReceiverEventCount;
    Core::TextureSubresourceSet csgPeelSubresources{};
    Core::TextureSubresourceSet csgReceiverEventCountSubresources{};
    Core::GpuTaskId dependencyTask;
    bool hasOpaqueCsgFrameWork = false;
};

struct OpaqueCsgIntervalClearResult{
    Core::GpuTaskId clearTask;
    Core::GpuTaskId clearFirstTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OpaqueCsgIntervalClearBuilder final : NoCopy{
public:
    OpaqueCsgIntervalClearBuilder(
        Core::GpuTaskGraph& graph
    );


public:
    [[nodiscard]] bool declare(
        const OpaqueCsgIntervalClearInputs& inputs,
        GraphClearTimingRecordState& clearTimingState,
        OpaqueCsgIntervalClearResult& outResult
    );


private:
    Core::GpuTaskGraph& m_graph;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

