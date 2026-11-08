// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "frame_graph_runtime_statistics.h"

#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


class RendererGraphRuntimeLabel final{
public:
    [[nodiscard]] static b8 append(
        const Core::GpuTaskGraphRuntimeStatistics& deferredRuntimeStatistics,
        const Core::GpuPhysicalQueueTopology& runtimeQueueTopology,
        Span<const Core::Telemetry::FrameGraphPhysicalQueueRuntimeStatistics> physicalQueueRuntimeStatistics,
        const Core::GpuCompiledGraph::ReadView& deferredCompiledPlan,
        const Core::GpuRecordedGraph& recordedGraph,
        Core::Device& device,
        AString<Core::Alloc::GlobalArena>& label
    );
};


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
