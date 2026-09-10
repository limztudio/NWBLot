// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared-output emulation alternates one generate and one raster phase per draw.
// Keep supported range in contract; domains stay independent of RendererFramePipeline.
inline constexpr usize s_SharedComputeEmulationPhasesPerDraw = 2u;
inline constexpr usize s_SharedComputeEmulationMinimumDrawCount = 2u;
inline constexpr usize s_SharedComputeEmulationMaximumDrawCount = 5u;
inline constexpr usize s_SharedComputeEmulationMinimumPhaseCount =
    s_SharedComputeEmulationMinimumDrawCount * s_SharedComputeEmulationPhasesPerDraw;
inline constexpr usize s_SharedComputeEmulationMaximumPhaseCount =
    s_SharedComputeEmulationMaximumDrawCount * s_SharedComputeEmulationPhasesPerDraw;

[[nodiscard]] inline constexpr bool IsSupportedSharedComputeEmulationDrawCount(const usize drawCount)noexcept{
    return drawCount >= s_SharedComputeEmulationMinimumDrawCount
        && drawCount <= s_SharedComputeEmulationMaximumDrawCount;
}

[[nodiscard]] inline constexpr usize SharedComputeEmulationPhaseCountForDrawCount(const usize drawCount)noexcept{
    return drawCount * s_SharedComputeEmulationPhasesPerDraw;
}

[[nodiscard]] inline constexpr bool IsSupportedSharedComputeEmulationPhaseCount(const usize phaseCount)noexcept{
    return phaseCount >= s_SharedComputeEmulationMinimumPhaseCount
        && phaseCount <= s_SharedComputeEmulationMaximumPhaseCount
        && (phaseCount % s_SharedComputeEmulationPhasesPerDraw) == 0u;
}

// Immutable frame facts for graph declaration; queue assignment stays a compiler result.
struct RendererFrameGraphFeatures{
    bool frameLaggedAsyncLightingEnabled = false;
    bool laggedLightingHistoryReady = false;
    bool laggedLightingHistoryReadReady = false;
    // A prior history-copy tail still reads live targets; next writers must wait for it.
    bool laggedLightingHistoryWriterWaitPending = false;
    bool hasTransparentRenderers = false;
    bool hardwareCaustics = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Domains publish this boundary after declaring topology; downstream avoids local coupling.
struct RendererTaskGraphTransparencyStage{
    Core::GpuTaskId firstTask;
    Core::GpuTaskId completionTask;
    bool hasTransparentTasks = false;

    [[nodiscard]] bool valid()const noexcept{ return firstTask.valid() && completionTask.valid(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

